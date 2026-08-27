#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "SoundObject.h"
#include "TrajectoryEngine.h"
#include "AmbisonicsEncoder.h"
#include "AmbisonicsDecoder.h"
#include "PropagationProcessor.h"
#include "GrainCloud.h"
#include "ParameterRegistry.h"

/**
    Input:  N mono channels (N = SAPOC_MAX_LIVE_INPUTS, configurable), each
            assigned to one SoundObject.
    Output: one of AmbisonicsDecoder::Mode's fixed target formats -- raw
            Ambisonics B-format (order 1/2/3, for further processing in a
            DAW/with external tools like SPARTA/IEM Suite), Stereo, or one
            of the AllRAD-decoded speaker layouts (Quad/5.1/7.1/Atmos-bed
            variants), selected via setDecoderMode(). Each mode declares
            its own output bus layout (see AmbisonicsDecoder::
            outputChannelSetFor()) rather than one fixed wide bus -- an
            exact channel count per mode, at the cost of mode switches
            being host-dependent to take effect live (see
            setDecoderMode()'s own comment).

    Raw Ambisonics modes bypass AmbisonicsDecoder entirely and encode
    straight into the output buffer, exactly as before this class existed
    (zero added overhead). Every other mode encodes into an internal
    ambiScratch bus first, then decodes that down to the real output.
*/
class KlangorbitProcessor : public juce::AudioProcessor
{
public:
    KlangorbitProcessor();
    ~KlangorbitProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Klangorbit"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Access for the editor (GUI reads/writes directly on the engine)
    TrajectoryEngine& getTrajectoryEngine() { return trajectoryEngine; }
    int getNumLiveInputs() const { return numLiveInputs; }

    // Current write position in that object's ring buffer, for
    // GrainCloud::setRingBufferContext() -- only the message thread reads
    // this (via the editor's timer), only the audio thread writes it.
    int getGrainRingBufferWriteHead (int objectIndex) const
    {
        return grainRingBufferWriteHead[(size_t) objectIndex].load (std::memory_order_relaxed);
    }

    // Global cap on simultaneously active grains, summed across ALL
    // clouds -- each active grain costs a full Ambisonics encode pass, so
    // this bounds worst-case CPU independent of how many clouds/objects
    // are granulating. Enforced by the editor's timer via
    // GrainCloud::update()'s globalGrainBudget parameter.
    //
    // Raised 32 -> 128 -> 256 over time: a rough operation-count estimate
    // (order-3 Ambisonics encode = 16 channels, block-rate
    // spherical-harmonic coefficients + a cheap per-sample ramp, no
    // per-sample trig) suggests even 256 concurrent grains stays
    // real-time-safe on any reasonably modern CPU -- but that is still a
    // back-of-envelope estimate, not a measurement on real hardware (not
    // available in this environment). getEstimatedCpuLoad() below exists
    // specifically so the user can verify this on their own machine
    // instead of trusting the estimate blindly.
    static constexpr int maxConcurrentGrainsGlobal = 256;

    // Smoothed fraction of each block's available real-time budget
    // actually spent inside processBlock() (measured wall-clock time /
    // block duration; >1 means it isn't keeping up). Written every block
    // from the audio thread, read from the message thread (editor's
    // timer) for the CPU-load display -- see the maxConcurrentGrainsGlobal
    // comment above for why this exists now specifically.
    float getEstimatedCpuLoad() const { return processBlockLoadFraction.load (std::memory_order_relaxed); }

    // --- Output decoder mode -------------------------------------------
    AmbisonicsDecoder::Mode getDecoderMode() const { return decoder.getMode(); }
    // Switches the active output format: re-orders the encoder, rebuilds
    // the decode matrix, and (best-effort) asks the host to renegotiate
    // the output bus to match -- see the .cpp for why that last part isn't
    // guaranteed to take effect live in every host.
    void setDecoderMode (AmbisonicsDecoder::Mode newMode);

    bool isBassManagementEnabled() const { return decoder.isBassManagementEnabled(); }
    void setBassManagementEnabled (bool shouldBeEnabled) { decoder.setBassManagementEnabled (shouldBeEnabled); }

    // Only meaningful while getDecoderMode() == CircularArray -- see
    // AmbisonicsDecoder::setCircularArraySpeakerCount()'s own comment.
    // Stored regardless of the current mode, so switching into
    // CircularArray later reuses whatever count was last configured.
    int getCircularArraySpeakerCount() const { return decoder.getCircularArraySpeakerCount(); }
    void setCircularArraySpeakerCount (int n);

    // --- Parameter registry ----------------------------------------------
    // See ParameterRegistry.h for what this is/isn't. Built once at
    // construction (buildParameterRegistry()), covering every possible
    // object/grain-cloud slot -- read-only from the outside; nothing
    // outside this class registers into it.
    const ParameterRegistry& getParameterRegistry() const { return parameterRegistry; }

    // Processor-owned source of truth for "which object is currently
    // selected" -- moved here (rather than staying purely GUI/editor
    // state, which is where it lived before this registry existed) so
    // ParameterRegistry::Scope::SelectedObject parameters keep resolving
    // correctly even without an editor window open. KlangorbitEditor keeps
    // its own selection-highlight bookkeeping for rendering, but now
    // forwards every selection change here too (see PluginEditor.cpp's
    // selectObject()) -- this is the one true value; the editor's own copy
    // is a display convenience, not a second source of truth.
    // -1 = nothing selected.
    int getSelectedObjectIndex() const { return selectedObjectIndex; }
    void setSelectedObjectIndex (int index) { selectedObjectIndex = index; }

private:
    void buildParameterRegistry();

    // Registration helpers for buildParameterRegistry() -- each registers
    // ONE field as multiple ParameterRegistry entries: one
    // Scope::SpecificObject entry per object slot (id
    // "object.<n>.<key>"), plus one Scope::SelectedObject entry (id
    // "selectedObject.<key>") that resolves against
    // getSelectedObjectIndex() at call time. Mirrors ParameterPanel's own
    // addObjectFloatRow()-style helpers (same binding idiom, generalized
    // behind ParameterRegistry's type-erased get/set instead of a
    // UI row).
    void registerObjectFloatParam (const juce::String& key, const juce::String& displayName, const juce::String& category,
                                    float SoundObject::* member, float minValue, float maxValue,
                                    ParameterRegistry::Polarity polarity = ParameterRegistry::Polarity::Unipolar);
    void registerObjectBoolParam (const juce::String& key, const juce::String& displayName, const juce::String& category,
                                   bool SoundObject::* member);
    // Registers member.x/.y/.z as three separate float parameters (id
    // suffixes ".x"/".y"/".z") -- same reasoning as Vec3RowComponent's own
    // three-slider split in ParameterPanel: a single controller axis maps
    // naturally to one Vec3 component, not to all three at once.
    void registerObjectVec3Param (const juce::String& key, const juce::String& displayName, const juce::String& category,
                                   Vec3 SoundObject::* member, float minValue, float maxValue,
                                   ParameterRegistry::Polarity polarity = ParameterRegistry::Polarity::Bipolar);

    void registerGrainFloatParam (const juce::String& key, const juce::String& displayName, const juce::String& category,
                                   float GrainCloudSettings::* member, float minValue, float maxValue,
                                   ParameterRegistry::Polarity polarity = ParameterRegistry::Polarity::Unipolar);
    void registerGrainBoolParam (const juce::String& key, const juce::String& displayName, const juce::String& category,
                                  bool GrainCloudSettings::* member);
    // Int fields (e.g. maxConcurrentGrains) go through the same float
    // get/set path, rounding on write -- same reuse-not-reinvent choice
    // ParameterPanel's own addGrainIntRow() already made for the identical
    // reason (one field, not worth a dedicated int-typed Descriptor kind).
    void registerGrainIntParam (const juce::String& key, const juce::String& displayName, const juce::String& category,
                                 int GrainCloudSettings::* member, float minValue, float maxValue);

    // Scene parameters are Scope::Global -- there is exactly one
    // SceneSettings, registered once, no per-object/selected-object
    // variants.
    void registerSceneFloatParam (const juce::String& key, const juce::String& displayName, const juce::String& category,
                                   float SceneSettings::* member, float minValue, float maxValue,
                                   ParameterRegistry::Polarity polarity = ParameterRegistry::Polarity::Unipolar);
    void registerSceneBoolParam (const juce::String& key, const juce::String& displayName, const juce::String& category,
                                  bool SceneSettings::* member);
    void registerSceneVec3Param (const juce::String& key, const juce::String& displayName, const juce::String& category,
                                  Vec3 SceneSettings::* member, float minValue, float maxValue,
                                  ParameterRegistry::Polarity polarity = ParameterRegistry::Polarity::Bipolar);

    // BusesProperties is a protected nested type of juce::AudioProcessor --
    // only constructible via a method of the derived class, not via a free
    // function.
    static BusesProperties makeBusLayout();

    static constexpr int numLiveInputs = SAPOC_MAX_LIVE_INPUTS;
    // Each cloud's pool is sized to the full global cap since, in the
    // worst case, a single cloud could legitimately use all of it.
    static constexpr int grainPoolSizePerCloud = maxConcurrentGrainsGlobal;
    // Sized from GrainLimits (Grain.h) -- the same constants ParameterPanel
    // uses for its slider ranges -- plus a safety margin, so this can never
    // silently become too small again if either range changes without the
    // other being reconsidered (see GrainLimits::requiredRingBufferSeconds
    // for the worst-case-lookback derivation).
    static constexpr float grainRingBufferSeconds = GrainLimits::requiredRingBufferSeconds + 1.0f;
    static_assert (grainRingBufferSeconds > GrainLimits::requiredRingBufferSeconds,
                   "grainRingBufferSeconds must exceed GrainLimits::requiredRingBufferSeconds -- "
                   "otherwise a grain at the extremes of the allowed duration/rate/jitter ranges "
                   "could read past what the ring buffer actually holds.");

    TrajectoryEngine trajectoryEngine { numLiveInputs, grainPoolSizePerCloud };
    AmbisonicsEncoder encoder;
    AmbisonicsDecoder decoder;

    // See getParameterRegistry()/getSelectedObjectIndex() above. Message-
    // thread state only (GUI selection changes, mapping-consumer reads in
    // a later branch) -- not read from processBlock() by anything in this
    // branch, so no audio-thread synchronization concern yet; a later
    // branch consuming this from the audio thread (e.g. a gamepad driver
    // polling from processBlock(), per the user's own stated requirement)
    // will need to revisit that.
    ParameterRegistry parameterRegistry;
    int selectedObjectIndex = -1;

    // Encode target for every non-raw-passthrough decoder mode -- always
    // exactly 16 channels (order-3 Ambisonics, the fixed internal encode
    // order for every decoded mode, see AmbisonicsDecoder::
    // ambisonicsOrderFor()), regardless of which decoded mode is active.
    // Raw passthrough modes bypass this and encode straight into the
    // output buffer instead (see processBlock()).
    juce::AudioBuffer<float> ambiScratch;
    static constexpr int ambiScratchChannels = 16;

    // Per-object persistent gain state for zipper-free ramping
    std::vector<std::vector<float>> previousGainsPerObject;

    // Per-object smoothed solo/mute multiplier (0..1), ramped a fixed
    // amount per block toward 1 (audible) or 0 (silent) rather than
    // switching instantly -- see SoundObject::muted/soloed and
    // processBlock()'s effective-mute computation. Once an object has
    // fully reached 0 (not just close to it), its propagation/encoding
    // work is skipped entirely for that block -- the actual performance
    // win -- rather than paying the full cost every block just to encode
    // silence.
    std::vector<float> muteRampGain;
    static constexpr float muteRampSeconds = 0.02f; // a few ms, per the design brief -- short enough to feel instant, long enough to never click

    // Same ramped-mute treatment as muteRampGain above, but driven by
    // GrainCloudSettings::sourceMuted instead of SoundObject::muted --
    // silences only the main-object rendering pass below (this object's
    // own dry audio), never the separate grain-rendering pass, so the
    // grains stay audible in isolation. Kept as its own ramp/array rather
    // than folded into muteRampGain because that one is also read by the
    // grain-rendering loop and must stay unaffected by this setting.
    std::vector<float> sourceMuteRampGain;

    // Per-object propagation delay/Doppler/air-absorption state (see
    // PropagationProcessor). Applied to the mono source signal before
    // AmbisonicsEncoder::encodeBlock().
    std::vector<PropagationProcessor> propagationPerObject;
    // Tracks each slot's active state from the previous block, so a
    // newly (re)activated object gets its PropagationProcessor reset
    // instead of inheriting stale delay/filter state from whatever
    // occupied that slot before.
    std::vector<bool> wasActiveLastBlock;
    // Scratch buffer for the propagated mono signal, sized once in
    // prepareToPlay -- never (re)allocated in processBlock.
    juce::AudioBuffer<float> propagationScratch;

    // --- GrainCloud audio-thread state (the GrainCloud objects themselves
    // -- settings, spawn timing, per-grain physics -- live in
    // TrajectoryEngine, see its class comment for why) ----------------------
    // Ring buffers, one per object slot, continuously fed that object's
    // live input every block regardless of whether its cloud is currently
    // enabled -- so turning granulation on always has recent history
    // available to read from immediately, no silent warm-up period.
    std::vector<juce::AudioBuffer<float>> grainRingBuffers; // mono, circular
    std::array<std::atomic<int>, numLiveInputs> grainRingBufferWriteHead {};

    // Audio-thread-owned per-(object, grain-pool-slot) playback state --
    // sample-accurate envelope/pitch progress, independent of the
    // coarser control-rate physics snapshot (see GrainCloud::Snapshot).
    struct GrainAudioState
    {
        int lastSeenGeneration = -1; // detects "this slot was respawned" even without an intervening inactive block
        int samplesPlayed = 0;
        std::vector<float> previousChannelGains;
    };
    std::vector<std::vector<GrainAudioState>> grainAudioState; // [objectIndex][grainPoolSlot]
    // Scratch for rendering one grain's windowed, pitch-shifted mono
    // signal at a time before encoding -- reused serially within a block,
    // sized once in prepareToPlay.
    juce::AudioBuffer<float> grainScratch;

    double currentSampleRate = 48000.0;

    // See getEstimatedCpuLoad() above.
    std::atomic<float> processBlockLoadFraction { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KlangorbitProcessor)
};
