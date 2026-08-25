#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "SoundObject.h"
#include "TrajectoryEngine.h"
#include "AmbisonicsEncoder.h"
#include "PropagationProcessor.h"
#include "GrainCloud.h"

/**
    Input:  N mono channels (N = SAPOC_MAX_LIVE_INPUTS, configurable), each
            assigned to one SoundObject.
    Output: Ambisonics B-format, channel count = (order+1)^2, order
            currently set via the SAPOC_DEFAULT_AMBI_ORDER constant
            (runtime switching is prepared, see AmbisonicsEncoder::setOrder(),
            but bus size is fixed per instance in the plugin context -- for
            runtime order changes, the standalone case is easier since no
            host bus contract exists there).

    No decoding here -- output is emitted as raw B-format and processed
    further in a DAW/with external tools (SPARTA, IEM Suite).
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

private:
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
