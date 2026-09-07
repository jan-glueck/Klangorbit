#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "SoundObject.h"
#include "TrajectoryEngine.h"
#include "AmbisonicsEncoder.h"
#include "AmbisonicsDecoder.h"
#include "HrtfDataset.h"
#include "BinauralDecoder.h"
#include "PropagationProcessor.h"
#include "GrainCloud.h"
#include "ParameterRegistry.h"
#include "CanonicalInput.h"
#include "GamepadDriver.h"
#include "MidiDriver.h"
#include "OscDriver.h"
#include "MappingEngine.h"

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

    Owns the control-rate simulation loop itself (private juce::Timer,
    90Hz, see timerCallback()) -- TrajectoryEngine::update() (object
    physics) and every GrainCloud::update() (grain spawning/movement) run
    from here, NOT from the editor. This used to be editor-owned
    (KlangorbitEditor::timerCallback(), pre-dating this class taking it
    over), which meant the entire simulation froze whenever the editor
    window was closed -- audio would keep being processed (JUCE calls
    processBlock() independent of any editor), but objects stopped
    moving, panning stopped updating, and no new grains ever spawned.
    Moving timer ownership here means the plugin keeps fully running --
    physics, panning, grains, all of it -- with the editor closed, in the
    background, or (Standalone) minimized; the editor, when open, now
    only handles its own rendering concerns (trails, sling-gesture
    modifier polling, repaint) on its own, separate, lighter timer. Both
    timers still run on the same JUCE message thread (there is only ever
    one), so this introduces no new cross-thread synchronization beyond
    what already existed (SoundObject/GrainCloudSettings fields were
    already read from the audio thread, unsynchronized, exactly as
    before -- only WHICH object schedules the message-thread callback
    changed, not who reads/writes what).
*/
class KlangorbitProcessor : public juce::AudioProcessor,
                             private juce::Timer
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
    // guaranteed to take effect live in every host. A no-op (see the .cpp)
    // if isOutputModeAvailable(newMode) is false.
    void setDecoderMode (AmbisonicsDecoder::Mode newMode);

    // True for every mode when NOT running as AU (VST3/Standalone
    // behavior is completely unchanged -- every mode has always been
    // switchable there). For AU: false for the 5 modes that can never
    // work in Logic (raw Ambisonics Order 1/2/3, Octophonic,
    // CircularArray -- unnamed channel sets Logic's own layout-tag
    // matching can't recognize, see isBusesLayoutSupported()'s own AU
    // branch), and otherwise only true if the mode's own channel count
    // fits within whatever output width Logic already negotiated at
    // insertion (getTotalNumOutputChannels()) -- this plugin never asks
    // an AU host to widen that after the fact, see setDecoderMode()'s own
    // comment. Used both by OutputPanel (to grey out combo items) and by
    // setDecoderMode() itself (defensive guard, in case something
    // requests an unavailable mode some other way).
    bool isOutputModeAvailable (AmbisonicsDecoder::Mode mode) const;

    bool isBassManagementEnabled() const { return decoder.isBassManagementEnabled(); }
    void setBassManagementEnabled (bool shouldBeEnabled) { decoder.setBassManagementEnabled (shouldBeEnabled); }

    // Only meaningful while getDecoderMode() == CircularArray -- see
    // AmbisonicsDecoder::setCircularArraySpeakerCount()'s own comment.
    // Stored regardless of the current mode, so switching into
    // CircularArray later reuses whatever count was last configured.
    int getCircularArraySpeakerCount() const { return decoder.getCircularArraySpeakerCount(); }
    void setCircularArraySpeakerCount (int n);

    // --- Binaural (HRTF) dataset selection --------------------------------
    // Only meaningful while getDecoderMode() == Mode::Binaural -- stored
    // regardless of the current mode (same "remember it for later" pattern
    // as circularSpeakerCount above), so switching into Binaural later
    // reuses whatever was last configured. Kemar/SadieD1/Ku100 are bundled
    // (see BinaryData::kemar_44100_sofa/sadie_d1_44100_sofa/
    // ku100_48000_sofa, each written once to a cached temp file since
    // libmysofa needs a real filesystem path, see the .cpp); CustomFile is
    // loaded from a user-chosen SOFA file via loadCustomSofaFile(). Ku100
    // is the TH Koeln/Bernschuetz "Spherical Far Field HRIR Compilation of
    // the Neumann KU 100" (CC BY 3.0, see THIRD_PARTY_LICENSES.md) -- a
    // denser measurement grid (16020 points, 2deg Gauss-Legendre) than
    // either KEMAR or SADIE II D1, natively 48kHz (unlike the other two
    // bundled datasets' 44.1kHz source files -- doesn't matter functionally,
    // HrtfDataset::load() resamples to whatever rate is requested
    // regardless of a file's own native rate; the filename suffix is purely
    // documentation of what was actually downloaded).
    enum class BinauralDatasetSource { Kemar, SadieD1, Ku100, CustomFile };
    BinauralDatasetSource getBinauralDatasetSource() const { return binauralDatasetSource; }
    // Switches the active dataset and rebuilds binauralDecoder's decode
    // matrix/convolution engines immediately (same "rebuild now, not per
    // block" idiom as setDecoderMode()/setCircularArraySpeakerCount()). A
    // no-op if newSource == CustomFile and no custom file has been loaded
    // yet (see loadCustomSofaFile()) -- falls back to leaving whatever
    // dataset was already active in place rather than silently going
    // silent.
    void setBinauralDataset (BinauralDatasetSource newSource);
    // Loads `file` as the custom dataset, switches to it immediately (as
    // if setBinauralDataset(CustomFile) had just been called), and returns
    // whether it loaded successfully -- false leaves the previously active
    // dataset (bundled or a prior custom file) untouched and unswitched,
    // so a bad file the user picks never silences the plugin.
    bool loadCustomSofaFile (const juce::File& file);
    juce::File getCustomSofaFilePath() const { return customSofaFilePath; }

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

    // --- Canonical controller input ---------------------------------------
    // See CanonicalInput.h for what this is/isn't. Owned here (like
    // parameterRegistry above) so a driver and a mapping consumer, added
    // in later branches, both have a single, already-wired hub to post to
    // /listen on without either one owning it. No producers or consumers
    // exist yet in this branch.
    CanonicalInputHub& getCanonicalInputHub() { return canonicalInputHub; }

    // --- Gamepad driver -----------------------------------------------------
    // See GamepadDriver.h for its full fixed default control scheme
    // (left stick movement, X/A/B object cycle/add/remove, Y/LB/RB
    // Free-Throw/Orbit-Shot/Slingshot). Polled from this processor's own
    // timerCallback() below (control rate, ~90Hz) -- per the project's own
    // requirement that gamepad polling live in the AudioProcessor, not the
    // editor, so control keeps working with no editor window open. Drives
    // the SELECTED object's movement directly as its own built-in default
    // behavior, unless mappingEngine below has an explicit binding
    // claiming the left stick (see GamepadDriver::
    // setLeftStickOverrideQuery(), wired up in the constructor). Camera
    // look/zoom (right stick/D-pad) is deliberately NOT part of this
    // driver -- see getGamepadState() below.
    bool isGamepadConnected() const { return gamepadDriver.isConnected(); }

    void setGamepadDeadzone (float newDeadzone) { gamepadDriver.setDeadzone (newDeadzone); }
    float getGamepadDeadzone() const { return gamepadDriver.getDeadzone(); }
    void setGamepadCurveExponent (float newExponent) { gamepadDriver.setCurveExponent (newExponent); }
    float getGamepadCurveExponent() const { return gamepadDriver.getCurveExponent(); }
    void setGamepadMaxSpeed (float newMaxSpeed) { gamepadDriver.setMaxSpeed (newMaxSpeed); }
    float getGamepadMaxSpeed() const { return gamepadDriver.getMaxSpeed(); }
    void setGamepadInertiaModeEnabled (bool shouldBeEnabled) { gamepadDriver.setInertiaModeEnabled (shouldBeEnabled); }
    bool isGamepadInertiaModeEnabled() const { return gamepadDriver.isInertiaModeEnabled(); }
    void setGamepadInertiaAcceleration (float newAccel) { gamepadDriver.setInertiaAcceleration (newAccel); }
    float getGamepadInertiaAcceleration() const { return gamepadDriver.getInertiaAcceleration(); }

    // Launch strength (meters of "pull") at full left-stick deflection for
    // the gamepad's built-in Free Throw/Orbit Shot/Slingshot buttons -- see
    // GamepadDriver::setThrowMaxPullDistance().
    void setGamepadThrowMaxPullDistance (float newMaxPullDistanceMeters) { gamepadDriver.setThrowMaxPullDistance (newMaxPullDistanceMeters); }
    float getGamepadThrowMaxPullDistance() const { return gamepadDriver.getThrowMaxPullDistance(); }

    // Raw, unshaped gamepad snapshot -- see GamepadDriver::getLastState()'s
    // own comment. Returned by value (GamepadState is a small POD) so
    // KlangorbitEditor's own timer can poll the right stick/D-pad for
    // camera control (Camera3D is editor-only view state, see its class
    // comment, so this driver never touches it itself) without holding a
    // reference across the processor/editor boundary.
    GamepadState getGamepadState() const { return gamepadDriver.getLastState(); }

    // --- MIDI driver ---------------------------------------------------
    // See MidiDriver.h. processMidiBuffer() (audio thread) is called from
    // processBlock() below; drainAndDispatch() (message thread) is called
    // from timerCallback() below, same rate as gamepad polling. No
    // meaningful "isConnected" concept for MIDI the way there is for a
    // gamepad or a network port -- device selection happens in the
    // host's/Standalone's own MIDI routing UI, not in this plugin's
    // control, so no accessor for that exists here.

    // --- OSC driver -----------------------------------------------------
    // See OscDriver.h. Listens on a UDP port (default 9000), dispatching
    // straight to canonicalInputHub from its own JUCE-marshaled
    // message-thread callback -- no polling/draining needed from
    // timerCallback() below, unlike gamepad/MIDI.
    bool isOscConnected() const { return oscDriver.isConnected(); }
    int getOscPort() const { return oscDriver.getPort(); }
    bool setOscPort (int newPort) { return oscDriver.setPort (newPort); }

    // --- Controller-mapping engine (Learn mode) ---------------------------
    // See MappingEngine.h. Registered as a CanonicalInputHub listener in
    // the constructor, so it sees every event any driver (GamepadDriver,
    // MidiDriver, OscDriver) posts. Loads MappingProfiles/factory/default.json at
    // startup if present (see the .cpp) -- ships empty by design (see
    // that file's own comment): the left stick's rate-control movement
    // already works out of the box via GamepadDriver's own built-in
    // behavior, with no binding required.
    MappingEngine& getMappingEngine() { return mappingEngine; }

private:
    // The control-rate simulation loop -- see the class comment above for
    // why this lives here instead of the editor. 90Hz, same rate the
    // editor's own timer previously drove this at (unchanged behavior,
    // just relocated). Runs TrajectoryEngine::update() (object physics)
    // and every active object's GrainCloud::update() (spawning/movement),
    // exactly the same two calls KlangorbitEditor::timerCallback() used
    // to make -- moved here verbatim, not reimplemented.
    void timerCallback() override;

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

    // --- Binaural (HRTF) decode path -------------------------------------
    // See setBinauralDataset()/loadCustomSofaFile() above. kemarDataset/
    // sadieDataset/ku100Dataset load once from the bundled BinaryData in
    // the constructor (message thread, see the .cpp); customDataset loads
    // on demand from loadCustomSofaFile(). binauralDecoder.prepare() is
    // called from setBinauralDataset()/loadCustomSofaFile()/prepareToPlay()
    // -- message thread only, matches BinauralDecoder::prepare()'s own
    // "not real-time-safe" contract; binauralDecoder.decode() is the only
    // one of these ever called from processBlock()/the audio thread.
    HrtfDataset kemarDataset, sadieDataset, ku100Dataset, customDataset;
    BinauralDecoder binauralDecoder;
    BinauralDatasetSource binauralDatasetSource = BinauralDatasetSource::Kemar;
    // BinaryData::kemar_44100_sofa/sadie_d1_44100_sofa/ku100_48000_sofa
    // written here once (constructor) -- libmysofa needs a real filesystem
    // path, not in-memory data, see the .cpp's
    // writeBinaryDataToTempFileIfNeeded().
    juce::File kemarSofaTempFile, sadieSofaTempFile, ku100SofaTempFile;
    juce::File customSofaFilePath; // empty until loadCustomSofaFile() succeeds at least once
    // (Re)loads whichever dataset binauralDatasetSource currently points
    // at (at currentSampleRate) and rebuilds binauralDecoder from it --
    // called from setBinauralDataset()/loadCustomSofaFile()/
    // prepareToPlay(). Message thread only, see BinauralDecoder::prepare().
    void prepareBinauralDecoder();
    int currentBlockSizeSamples = 512;

    // See getParameterRegistry()/getSelectedObjectIndex() above. Message-
    // thread state only -- GUI selection changes, and now also read every
    // tick by gamepadDriver.poll() below, called from timerCallback() on
    // the same message thread (NOT processBlock()/the audio thread --
    // GameController framework calls aren't real-time-safe, so polling
    // happens from the same message-thread timer that already drives
    // TrajectoryEngine::update(), not the audio callback). No audio-thread
    // synchronization concern as a result.
    ParameterRegistry parameterRegistry;
    int selectedObjectIndex = -1;

    // See getCanonicalInputHub() above. CanonicalInputHub is internally
    // locked and safe to post to/listen on from any thread, so this
    // member itself needs no extra synchronization here.
    CanonicalInputHub canonicalInputHub;

    // See isGamepadConnected()/setGamepadDeadzone() etc. above. Polled
    // from timerCallback() below, message-thread only (see the comment on
    // selectedObjectIndex above for why not the audio thread).
    GamepadDriver gamepadDriver { trajectoryEngine };

    // See MidiDriver.h. Default-constructed (no dependencies) --
    // processMidiBuffer() is called from processBlock() (audio thread),
    // drainAndDispatch() from timerCallback() below (message thread).
    MidiDriver midiDriver;

    // See OscDriver.h/isOscConnected() etc. above. Constructed after
    // canonicalInputHub (declared earlier in this class) since it holds
    // a reference to it, and dispatches to it directly from its own
    // message-thread-marshaled callback -- no polling needed here.
    OscDriver oscDriver { canonicalInputHub };

    // See getMappingEngine() above. Constructed after parameterRegistry
    // (declared earlier in this class) since it holds a reference to it;
    // registered as a canonicalInputHub listener and wired into
    // gamepadDriver's override query in the constructor (.cpp).
    MappingEngine mappingEngine { parameterRegistry };

    // Control-rate timer state -- see timerCallback(). Moved here from
    // what used to be KlangorbitEditor::lastTimerMs/grainRandom (same
    // purpose, same values, just owned by whichever object now drives the
    // loop). grainRandom is message-thread-only (like its editor-owned
    // predecessor), used only inside timerCallback()'s GrainCloud::update()
    // calls.
    juce::uint32 lastControlRateTimerMs = 0;
    juce::Random grainRandom;

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
        // Persistent ring-buffer read position, owned by renderGrainBlock()
        // (see its own comment in GrainRenderer.h for why this must be an
        // accumulator, not recomputed from bufferReadStartSample +
        // samplesPlayed*playbackRate, once per-grain Doppler can change
        // playbackRate block-to-block). Reset to bufferReadStartSample when
        // lastSeenGeneration changes (a new grain spawned into this slot).
        double readPosition = 0.0;
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
