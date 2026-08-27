#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GrainRenderer.h"
#include "GrainDoppler.h"
#include "MuteSoloLogic.h"
#include "MappingProfileManager.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Cartesian (x=front, y=left, z=up) -> spherical coordinates for the encoder.
    void cartesianToSpherical (Vec3 pos, float& azimuth, float& elevation, float& distance)
    {
        distance = juce::jmax (pos.length(), 0.001f);
        azimuth  = std::atan2 (pos.y, pos.x);
        elevation = std::asin (juce::jlimit (-1.0f, 1.0f, pos.z / distance));
    }

    // Maps SAPOC_DEFAULT_AMBI_ORDER to its raw-passthrough decoder mode --
    // used at startup for both the declared bus layout and the decoder's
    // own initial mode, so the two can never disagree if that build
    // setting is ever changed away from its current default of 3.
    AmbisonicsDecoder::Mode rawModeForOrder (int order)
    {
        using Mode = AmbisonicsDecoder::Mode;
        if (order <= 1) return Mode::AmbisonicsRawOrder1;
        if (order == 2) return Mode::AmbisonicsRawOrder2;
        return Mode::AmbisonicsRawOrder3;
    }
}

KlangorbitProcessor::BusesProperties KlangorbitProcessor::makeBusLayout()
{
    return BusesProperties()
        .withInput  ("Live Inputs", juce::AudioChannelSet::discreteChannels (SAPOC_MAX_LIVE_INPUTS), true)
        .withOutput ("Ambisonics", AmbisonicsDecoder::outputChannelSetFor (rawModeForOrder (SAPOC_DEFAULT_AMBI_ORDER)), true);
}

KlangorbitProcessor::KlangorbitProcessor()
    : juce::AudioProcessor (makeBusLayout())
{
    encoder.setOrder (SAPOC_DEFAULT_AMBI_ORDER);
    decoder.setMode (rawModeForOrder (SAPOC_DEFAULT_AMBI_ORDER));

    // Only object 0 starts active (input channel 0). Further objects are
    // added via the GUI (TrajectoryEngine::activateObject()) -- the input
    // channel assignment stays the simple 1:1 mapping of object index ==
    // channel index.
    trajectoryEngine.activateObject (0);

    previousGainsPerObject.resize ((size_t) numLiveInputs);
    for (auto& g : previousGainsPerObject)
        g.assign ((size_t) encoder.getNumChannels(), 0.0f);

    muteRampGain.assign ((size_t) numLiveInputs, 1.0f); // start unmuted/audible
    sourceMuteRampGain.assign ((size_t) numLiveInputs, 1.0f);

    propagationPerObject.resize ((size_t) numLiveInputs);
    wasActiveLastBlock.assign ((size_t) numLiveInputs, false);

    for (auto& wh : grainRingBufferWriteHead)
        wh.store (0, std::memory_order_relaxed);

    buildParameterRegistry();

    // Wires the mapping engine into the canonical input hub (so it sees
    // every event any driver posts -- currently just gamepadDriver) and
    // gives gamepadDriver a way to ask "is the left stick explicitly
    // bound right now?" (see GamepadDriver::setLeftStickOverrideQuery()'s
    // own comment on why this exists).
    canonicalInputHub.addListener (&mappingEngine);
    gamepadDriver.setLeftStickOverrideQuery ([this] (const juce::String& sourceId)
    {
        return mappingEngine.hasBindingFor (sourceId, mappingEngine.getCurrentBank());
    });

    // Best-effort: load the shipped factory default mapping profile if
    // this build has one (see SAPOC_MAPPING_PROFILES_FACTORY_DIR's own
    // comment in CMakeLists.txt -- a local-dev-only convenience path, not
    // present in an installed plugin). Ships empty by design (see
    // MappingProfiles/factory/default.json's own comment) -- absence or
    // failure to load is silently fine, mappingEngine simply starts with
    // no bindings either way, same end state.
   #if defined (SAPOC_MAPPING_PROFILES_FACTORY_DIR)
    {
        const juce::File defaultProfile = juce::File (SAPOC_MAPPING_PROFILES_FACTORY_DIR).getChildFile ("default.json");
        if (defaultProfile.existsAsFile())
            MappingProfileManager::loadFile (defaultProfile, mappingEngine);
    }
   #endif

    // Starts the control-rate simulation loop -- see the class comment in
    // PluginProcessor.h for why this runs from here rather than the
    // editor. 90Hz matches the rate the editor's own timer previously
    // drove TrajectoryEngine/GrainCloud updates at.
    lastControlRateTimerMs = juce::Time::getMillisecondCounter();
    startTimerHz (90);
}

KlangorbitProcessor::~KlangorbitProcessor()
{
    // Explicit, before any other member starts tearing down -- same
    // ordering reasoning KlangorbitEditor's own destructor already
    // established for its (now editor-only) timer. juce::Timer's own
    // destructor would stop it safely too, but stopping it first here
    // means timerCallback() can never fire mid-teardown of this class's
    // own members (trajectoryEngine, grainRandom, etc.).
    stopTimer();

    // Explicit, before mappingEngine itself is destroyed below (member
    // destruction order), so canonicalInputHub never holds a dangling
    // listener pointer even for the brief window between the two
    // members' destructors -- nothing would actually dereference it
    // there (the timer is already stopped, nothing else calls dispatch()
    // during teardown), but there's no reason to leave a dangling
    // pointer sitting around when removing it costs nothing.
    canonicalInputHub.removeListener (&mappingEngine);
}

void KlangorbitProcessor::timerCallback()
{
    // Moved verbatim from what used to be KlangorbitEditor::timerCallback()
    // (see the class comment in PluginProcessor.h for why) -- same two
    // calls, same control rate, same dt clamping. The editor's own timer
    // (still running, at the same rate, when an editor exists) no longer
    // touches TrajectoryEngine/GrainCloud at all -- only this one does now,
    // so there is no double-update.
    const auto now = juce::Time::getMillisecondCounter();
    const double dt = juce::jlimit (0.0, 0.1, (double) (now - lastControlRateTimerMs) / 1000.0); // clamp against outliers
    lastControlRateTimerMs = now;

    // Polled BEFORE trajectoryEngine.update() below, so this same tick's
    // integration step already reflects the freshly-read stick position
    // (lowest latency, one control-rate tick) rather than lagging by one.
    // See GamepadDriver's own class comment for why this happens here
    // (message-thread timer) and not processBlock().
    gamepadDriver.poll (canonicalInputHub, selectedObjectIndex, dt);

    // Drains whatever CC/Note/PitchBend messages processBlock() queued
    // since the last tick and dispatches them here -- see MidiDriver's
    // own class comment for why (MIDI arrives on the audio thread, but
    // dispatch/mapping application needs to happen on the message
    // thread, same as gamepad polling above). oscDriver needs no
    // equivalent call -- it dispatches directly from its own JUCE-
    // marshaled message-thread callback whenever a packet arrives.
    midiDriver.drainAndDispatch (canonicalInputHub);

    trajectoryEngine.update (dt);

    // GrainCloud: control-rate update, same loop/rate as TrajectoryEngine
    // above. A single global spawn budget is shared across all clouds so
    // the total number of simultaneously active grains never exceeds
    // maxConcurrentGrainsGlobal, no matter how many objects are
    // granulating at once -- each active grain costs a full Ambisonics
    // encode pass in processBlock() below.
    int globalGrainBudget = maxConcurrentGrainsGlobal;
    for (int i = 0; i < trajectoryEngine.getNumGrainClouds(); ++i)
        globalGrainBudget -= trajectoryEngine.getGrainCloud (i).getNumActiveGrains();

    for (int i = 0; i < trajectoryEngine.getNumGrainClouds(); ++i)
    {
        auto& obj = trajectoryEngine.getObject (i);
        if (obj.inputChannel < 0)
            continue; // no active parent -- freeze this cloud instead of updating it with a meaningless position

        auto& cloud = trajectoryEngine.getGrainCloud (i);
        cloud.setRingBufferContext (getGrainRingBufferWriteHead (i), getSampleRate());
        cloud.update (dt, obj.position, obj.velocity, globalGrainBudget, grainRandom);
    }
}

void KlangorbitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    encoder.prepare (sampleRate, samplesPerBlock);
    decoder.prepare (sampleRate);

    ambiScratch.setSize (ambiScratchChannels, samplesPerBlock);

    for (auto& g : previousGainsPerObject)
        g.assign ((size_t) encoder.getNumChannels(), 0.0f);

    for (auto& p : propagationPerObject)
        p.prepare (sampleRate, samplesPerBlock);
    std::fill (wasActiveLastBlock.begin(), wasActiveLastBlock.end(), false);
    std::fill (muteRampGain.begin(), muteRampGain.end(), 1.0f);
    std::fill (sourceMuteRampGain.begin(), sourceMuteRampGain.end(), 1.0f);

    propagationScratch.setSize (1, samplesPerBlock);

    grainRingBuffers.resize ((size_t) numLiveInputs);
    const int ringBufferSamples = juce::jmax (1, (int) (grainRingBufferSeconds * sampleRate));
    for (auto& ring : grainRingBuffers)
    {
        ring.setSize (1, ringBufferSamples);
        ring.clear();
    }
    for (auto& wh : grainRingBufferWriteHead)
        wh.store (0, std::memory_order_relaxed);

    grainAudioState.assign ((size_t) numLiveInputs, std::vector<GrainAudioState> ((size_t) grainPoolSizePerCloud));
    for (auto& perObject : grainAudioState)
    {
        for (auto& state : perObject)
        {
            state.lastSeenGeneration = -1;
            state.samplesPlayed = 0;
            state.previousChannelGains.assign ((size_t) encoder.getNumChannels(), 0.0f);
        }
    }

    grainScratch.setSize (1, samplesPerBlock);
}

void KlangorbitProcessor::releaseResources() {}

bool KlangorbitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Input is always the fixed live-input bus. Output must match one of
    // AmbisonicsDecoder::Mode's fixed target formats -- not the CURRENTLY
    // active mode specifically, but any of them, so a host can propose
    // switching modes via the normal checkBusesLayoutSupported/
    // setBusesLayout negotiation (see setDecoderMode()'s own comment on
    // why a plugin-initiated switch alone isn't reliably enough in every
    // host).
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::discreteChannels (numLiveInputs))
        return false;

    const auto outSet = layouts.getMainOutputChannelSet();
    using Mode = AmbisonicsDecoder::Mode;
    static constexpr Mode fixedModes[] = {
        Mode::AmbisonicsRawOrder1, Mode::AmbisonicsRawOrder2, Mode::AmbisonicsRawOrder3,
        Mode::Stereo, Mode::Quad, Mode::Surround5_1, Mode::Surround7_1,
        Mode::Atmos5_1_2, Mode::Atmos5_1_4, Mode::Atmos7_1_2, Mode::Atmos7_1_4,
        Mode::Octophonic,
    };
    for (auto m : fixedModes)
        if (outSet == AmbisonicsDecoder::outputChannelSetFor (m))
            return true;

    // CircularArray: any channel count in its supported range, since the
    // user picks numSpeakers independently of selecting the mode itself
    // (see setCircularArraySpeakerCount()). Note this range includes 8,
    // which is ALSO Octophonic's own (fixed) channel count -- an
    // accepted, documented overlap, see AmbisonicsDecoder::
    // outputChannelSetFor()'s own comment.
    for (int n = AmbisonicsDecoder::minCircularSpeakers; n <= AmbisonicsDecoder::maxCircularSpeakers; ++n)
        if (outSet == juce::AudioChannelSet::discreteChannels (n))
            return true;

    return false;
}

void KlangorbitProcessor::setDecoderMode (AmbisonicsDecoder::Mode newMode)
{
    if (newMode == decoder.getMode())
        return;

    encoder.setOrder (AmbisonicsDecoder::ambisonicsOrderFor (newMode));
    decoder.setMode (newMode);

    // A changed Ambisonics order invalidates any in-flight gain ramp
    // target -- reset rather than resize-and-keep.
    for (auto& g : previousGainsPerObject)
        g.assign ((size_t) encoder.getNumChannels(), 0.0f);
    for (auto& perObject : grainAudioState)
        for (auto& state : perObject)
            state.previousChannelGains.assign ((size_t) encoder.getNumChannels(), 0.0f);

    // Best-effort request for the host to renegotiate the output bus to
    // match. This JUCE version's VST3 wrapper has no dedicated "please
    // rescan my bus layout" restart flag a plugin can raise on its own
    // (Vst::kIoChanged is never sent) -- updateHostDisplay() below can at
    // most mark the plugin's non-parameter state dirty, which some hosts
    // (Reaper, confirmed) use as a cue to recheck buses live; others will
    // only pick up the new layout the next time they call
    // checkBusesLayoutSupported() themselves (e.g. on project reload, or
    // after the plugin is removed and reinserted). Calling setBusesLayout()
    // here still matters even then: it keeps this AudioProcessor's own
    // reported bus layout consistent with the active decode mode, so a
    // host that DOES rescan sees the right answer immediately. This
    // tradeoff (per-mode bus layouts, live-switch reliability varies by
    // host) was chosen deliberately over a fixed always-16-channel bus --
    // see CHANGELOG.
    auto layouts = getBusesLayout();
    if (! layouts.outputBuses.isEmpty())
        layouts.outputBuses.getReference (0) = AmbisonicsDecoder::outputChannelSetFor (newMode, decoder.getCircularArraySpeakerCount());
    setBusesLayout (layouts);
    updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
}

void KlangorbitProcessor::setCircularArraySpeakerCount (int n)
{
    const int clamped = juce::jlimit (AmbisonicsDecoder::minCircularSpeakers, AmbisonicsDecoder::maxCircularSpeakers, n);
    if (clamped == decoder.getCircularArraySpeakerCount())
        return;

    decoder.setCircularArraySpeakerCount (clamped); // rebuilds the decode matrix live if CircularArray is already active

    // Only CircularArray's own output channel count depends on this --
    // the encoder side (ambiScratch, gain-ramp state) stays fixed at
    // order 3 regardless, so unlike setDecoderMode() above there is no
    // ramp state to reset here.
    if (decoder.getMode() != AmbisonicsDecoder::Mode::CircularArray)
        return;

    // Same best-effort bus renegotiation as setDecoderMode() -- see its comment.
    auto layouts = getBusesLayout();
    if (! layouts.outputBuses.isEmpty())
        layouts.outputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (clamped);
    setBusesLayout (layouts);
    updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
}

void KlangorbitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto blockStartTicks = juce::Time::getHighResolutionTicks();

    // Cheap, bounded queue push only -- see MidiDriver's own class
    // comment on why the actual translation/dispatch happens later, from
    // timerCallback() on the message thread, not here.
    midiDriver.processMidiBuffer (midiMessages);

    const int numSamples = buffer.getNumSamples();
    const int numInCh    = juce::jmin (numLiveInputs, buffer.getNumChannels());

    // Preserve the input channels before overwriting -- the output buffer
    // is the same memory as the input (in-place), so copy first.
    juce::AudioBuffer<float> inputCopy (numInCh, numSamples);
    for (int ch = 0; ch < numInCh; ++ch)
        inputCopy.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    buffer.clear();

    // Raw passthrough modes encode straight into the output buffer, exactly
    // as before this decoder feature existed (zero added overhead, see
    // AmbisonicsDecoder's class comment). Every other mode encodes into
    // ambiScratch instead, decoded down to the real output once both
    // rendering passes below are done.
    const bool rawPassthrough = AmbisonicsDecoder::isRawPassthrough (decoder.getMode());
    juce::AudioBuffer<float>& encodeTarget = rawPassthrough ? buffer : ambiScratch;
    if (! rawPassthrough)
        ambiScratch.clear();

    std::vector<TrajectoryEngine::Snapshot> snapshot;
    trajectoryEngine.getSnapshot (snapshot);

    // Read-only access to scene-wide acoustic parameters (speedOfSound,
    // temperature, humidity, pressure, wind) from the audio thread. Same
    // established pattern as obj.gain/obj.inputChannel below: plain,
    // unsynchronized reads of values the message thread writes -- see the
    // TrajectoryEngine class comment and README known limitations.
    const auto& sceneSettings = trajectoryEngine.getSceneSettings();
    float* propagated = propagationScratch.getWritePointer (0);

    // Solo: if ANY active object is soloed, every object that is NOT
    // soloed goes silent (classic non-exclusive DAW solo -- several
    // objects can be soloed together and all stay audible). Computed once
    // per block, not per object.
    bool anySoloed = false;
    for (int i = 0; i < trajectoryEngine.getNumObjects(); ++i)
    {
        auto& o = trajectoryEngine.getObject (i);
        if (o.inputChannel >= 0 && o.soloed) { anySoloed = true; break; }
    }

    for (int objIdx = 0; objIdx < trajectoryEngine.getNumObjects(); ++objIdx)
    {
        auto& obj = trajectoryEngine.getObject (objIdx);
        const bool active = obj.inputChannel >= 0 && obj.inputChannel < numInCh
                             && objIdx < (int) snapshot.size() && snapshot[(size_t) objIdx].active;

        // Keep this object's grain-cloud ring buffer filled with its live
        // input every block, regardless of whether granulation is
        // currently enabled -- so turning it on always has recent history
        // to read from, no silent warm-up period.
        if (active)
        {
            auto& ring = grainRingBuffers[(size_t) objIdx];
            const int ringSize = ring.getNumSamples();
            if (ringSize > 0)
            {
                int head = grainRingBufferWriteHead[(size_t) objIdx].load (std::memory_order_relaxed);
                const float* src = inputCopy.getReadPointer (obj.inputChannel);
                float* dst = ring.getWritePointer (0);

                for (int i = 0; i < numSamples; ++i)
                {
                    dst[head] = src[i];
                    head = (head + 1) % ringSize;
                }
                grainRingBufferWriteHead[(size_t) objIdx].store (head, std::memory_order_relaxed);
            }
        }

        if (! active)
        {
            wasActiveLastBlock[(size_t) objIdx] = false;
            continue;
        }

        // Solo/mute: own mute always wins over solo (an object can't be
        // simultaneously "definitely silent" and "definitely audible");
        // otherwise silenced if some other object is soloed and this one
        // isn't. Smoothly ramped over muteRampSeconds rather than switched
        // instantly, so toggling never clicks (the encoder's own
        // per-sample gain interpolation from previousChannelGains smooths
        // it further still, within the ramp).
        const bool effectivelyMuted = MuteSoloLogic::isEffectivelyMuted (obj.muted, obj.soloed, anySoloed);
        const float muteTarget = effectivelyMuted ? 0.0f : 1.0f;
        auto& ramp = muteRampGain[(size_t) objIdx];
        const float rampStep = (float) ((double) numSamples / juce::jmax (1.0, currentSampleRate)) / muteRampSeconds;
        if (ramp < muteTarget)      ramp = juce::jmin (muteTarget, ramp + rampStep);
        else if (ramp > muteTarget) ramp = juce::jmax (muteTarget, ramp - rampStep);

        // GrainCloudSettings::sourceMuted: mutes just this object's own dry
        // audio (this loop), never its grains (the separate loop below,
        // which only ever looks at muted/soloed above) -- lets the grains
        // be heard in isolation. Same ramped treatment, own ramp array so
        // it can't affect the grain loop's muteRampGain read.
        const bool sourceMuted = trajectoryEngine.getGrainCloud (objIdx).getSettings().sourceMuted;
        const float sourceMuteTarget = sourceMuted ? 0.0f : 1.0f;
        auto& sourceRamp = sourceMuteRampGain[(size_t) objIdx];
        if (sourceRamp < sourceMuteTarget)      sourceRamp = juce::jmin (sourceMuteTarget, sourceRamp + rampStep);
        else if (sourceRamp > sourceMuteTarget) sourceRamp = juce::jmax (sourceMuteTarget, sourceRamp - rampStep);

        // Only once a ramp has actually reached silence (not just close to
        // it) do we skip propagation+encoding entirely for this object --
        // the actual performance win, rather than paying full cost every
        // block just to encode silence while muted/soloed/source-muted-out.
        const bool audioProcessingActive = ! (effectivelyMuted && ramp <= 0.0f)
                                         && ! (sourceMuted && sourceRamp <= 0.0f);

        if (! audioProcessingActive)
        {
            // Treated like "became inactive" for PropagationProcessor's
            // internal delay-line state -- resuming later re-syncs
            // (.reset()) instead of resuming from state that's now stale
            // relative to however far the object has actually moved.
            wasActiveLastBlock[(size_t) objIdx] = false;
            continue;
        }

        if (! wasActiveLastBlock[(size_t) objIdx])
            propagationPerObject[(size_t) objIdx].reset();
        wasActiveLastBlock[(size_t) objIdx] = true;

        float directivityGain = 1.0f;
        propagationPerObject[(size_t) objIdx].process (inputCopy.getReadPointer (obj.inputChannel),
                                                         propagated, numSamples,
                                                         snapshot[(size_t) objIdx].position,
                                                         obj, sceneSettings,
                                                         directivityGain);

        float azimuth, elevation, distance;
        cartesianToSpherical (snapshot[(size_t) objIdx].position, azimuth, elevation, distance);

        encoder.encodeBlock (propagated,
                              numSamples,
                              azimuth, elevation, distance,
                              obj.gain * directivityGain * ramp * sourceRamp,
                              encodeTarget,
                              previousGainsPerObject[(size_t) objIdx]);
    }

    // --- GrainCloud rendering ------------------------------------------
    // Grain positions/trigger parameters come from a control-rate snapshot
    // (like SoundObjects); the audio-thread-owned GrainAudioState tracks
    // each active grain's sample-accurate envelope/pitch progress
    // independently of that coarser update rate. Deliberately no
    // PropagationProcessor pass for grains (no per-grain delay line/air
    // absorption/directivity) -- with dozens of simultaneous short-lived
    // grains, that would be disproportionately expensive; grains only get
    // AmbisonicsEncoder's built-in spatial encoding + distance gain, plus
    // an optional, much cheaper per-grain Doppler pitch shift (see
    // GrainCloudSettings::dopplerEnabled/GrainDoppler.h below -- a single
    // per-block ratio, not a delay line).
    float* grainOut = grainScratch.getWritePointer (0);

    for (int objIdx = 0; objIdx < trajectoryEngine.getNumObjects(); ++objIdx)
    {
        auto& obj = trajectoryEngine.getObject (objIdx);
        if (obj.inputChannel < 0)
            continue; // no parent, no cloud -- see GrainCloud/PluginEditor: an inactive object's cloud is frozen, not rendered

        // Mute/solo applies to a muted object's grains too (see
        // SoundObject::muted/soloed) -- reuses the ramp already advanced
        // for the parent object in the loop above (same block, same
        // smoothed fade), skipping this object's grains entirely once
        // that ramp has actually reached silence, same performance
        // reasoning as the main-object loop above. Deliberately NOT
        // gated by GrainCloudSettings::sourceMuted (that one only silences
        // the main-object loop's own dry-audio rendering above) -- so
        // muting just the source still leaves these grains audible.
        const bool effectivelyMuted = MuteSoloLogic::isEffectivelyMuted (obj.muted, obj.soloed, anySoloed);
        const float muteRamp = muteRampGain[(size_t) objIdx];
        if (effectivelyMuted && muteRamp <= 0.0f)
            continue;

        std::vector<GrainCloud::Snapshot> grainSnapshot;
        trajectoryEngine.getGrainCloud (objIdx).getSnapshot (grainSnapshot);

        auto& ring = grainRingBuffers[(size_t) objIdx];
        const int ringSize = ring.getNumSamples();
        if (ringSize <= 0)
            continue;
        const float* ringData = ring.getReadPointer (0);

        auto& audioStatesForObject = grainAudioState[(size_t) objIdx];
        const size_t numSlots = juce::jmin (grainSnapshot.size(), audioStatesForObject.size());

        for (size_t slot = 0; slot < numSlots; ++slot)
        {
            auto& snap = grainSnapshot[slot];
            auto& state = audioStatesForObject[slot];

            if (! snap.active)
            {
                state.lastSeenGeneration = -1; // force a reset if this slot is reused later
                continue;
            }

            if (snap.spawnGeneration != state.lastSeenGeneration)
            {
                state.lastSeenGeneration = snap.spawnGeneration;
                state.samplesPlayed = 0;
                std::fill (state.previousChannelGains.begin(), state.previousChannelGains.end(), 0.0f);
            }

            if (state.samplesPlayed >= snap.grainLengthSamples)
                continue; // this grain's audio envelope has already finished (physics may still be fading out)

            // Optional per-grain Doppler (default off, see
            // GrainCloudSettings::dopplerEnabled) -- a per-block-constant
            // pitch ratio multiplied into the existing pitchJitter-based
            // rate, deliberately not a delay line (see GrainDoppler.h).
            float effectivePlaybackRate = snap.playbackRate;
            if (trajectoryEngine.getGrainCloud (objIdx).getSettings().dopplerEnabled)
                effectivePlaybackRate *= GrainDoppler::computeDopplerRatio (snap.position, snap.velocity,
                                                                             obj.dopplerFactor, sceneSettings.speedOfSound);

            renderGrainBlock (ringData, ringSize, snap.bufferReadStartSample, effectivePlaybackRate,
                               snap.grainLengthSamples, state.samplesPlayed, grainOut, numSamples);

            float azimuth, elevation, distance;
            cartesianToSpherical (snap.position, azimuth, elevation, distance);

            encoder.encodeBlock (grainOut, numSamples, azimuth, elevation, distance,
                                  obj.gain * muteRamp, encodeTarget, state.previousChannelGains);
        }
    }

    // Decode ambiScratch down to the real output bus -- skipped entirely
    // for raw passthrough modes, which already wrote straight into buffer
    // above.
    if (! rawPassthrough)
        decoder.decode (ambiScratch, buffer, numSamples);

    // Real, measured CPU-load estimate -- see maxConcurrentGrainsGlobal's
    // comment in the header for why this exists (128 concurrent grains is
    // a rough estimate, not a profiled number; this lets the user check
    // for themselves instead of trusting the estimate blindly). Smoothed
    // (exponential moving average) so the UI reading doesn't flicker
    // block-to-block.
    const auto blockEndTicks = juce::Time::getHighResolutionTicks();
    const double elapsedSeconds = juce::Time::highResolutionTicksToSeconds (blockEndTicks - blockStartTicks);
    const double blockDurationSeconds = (double) numSamples / juce::jmax (1.0, currentSampleRate);
    const float instantLoad = (float) (elapsedSeconds / blockDurationSeconds);

    constexpr float smoothing = 0.9f;
    const float previousLoad = processBlockLoadFraction.load (std::memory_order_relaxed);
    processBlockLoadFraction.store (previousLoad * smoothing + instantLoad * (1.0f - smoothing), std::memory_order_relaxed);
}

// --- Parameter registry ---------------------------------------------------
// See ParameterRegistry.h for the "why not juce::AudioProcessorParameter"
// reasoning, and PluginProcessor.h for what each helper below does. Every
// field registered here is one that already had a slider/toggle in
// ParameterPanel -- pure runtime physics STATE (position, velocity,
// orbitPhase, etc.) is deliberately excluded, same scope ParameterPanel
// itself already draws the line at.

void KlangorbitProcessor::registerObjectFloatParam (const juce::String& key, const juce::String& displayName,
                                                      const juce::String& category, float SoundObject::* member,
                                                      float minValue, float maxValue, ParameterRegistry::Polarity polarity)
{
    for (int i = 0; i < trajectoryEngine.getNumObjects(); ++i)
    {
        parameterRegistry.registerParameter ({
            "object." + juce::String (i) + "." + key, displayName, category, minValue, maxValue, polarity,
            ParameterRegistry::Scope::SpecificObject, i,
            [this, member, i] { return trajectoryEngine.getObject (i).*member; },
            [this, member, i] (float v) { trajectoryEngine.getObject (i).*member = v; }
        });
    }

    parameterRegistry.registerParameter ({
        "selectedObject." + key, displayName, category, minValue, maxValue, polarity,
        ParameterRegistry::Scope::SelectedObject, -1,
        [this, member]
        {
            const int idx = selectedObjectIndex;
            return (idx >= 0 && idx < trajectoryEngine.getNumObjects()) ? trajectoryEngine.getObject (idx).*member : 0.0f;
        },
        [this, member] (float v)
        {
            const int idx = selectedObjectIndex;
            if (idx >= 0 && idx < trajectoryEngine.getNumObjects())
                trajectoryEngine.getObject (idx).*member = v;
        }
    });
}

void KlangorbitProcessor::registerObjectBoolParam (const juce::String& key, const juce::String& displayName,
                                                     const juce::String& category, bool SoundObject::* member)
{
    for (int i = 0; i < trajectoryEngine.getNumObjects(); ++i)
    {
        parameterRegistry.registerParameter ({
            "object." + juce::String (i) + "." + key, displayName, category, 0.0f, 1.0f, ParameterRegistry::Polarity::Unipolar,
            ParameterRegistry::Scope::SpecificObject, i,
            [this, member, i] { return trajectoryEngine.getObject (i).*member ? 1.0f : 0.0f; },
            [this, member, i] (float v) { trajectoryEngine.getObject (i).*member = (v >= 0.5f); }
        });
    }

    parameterRegistry.registerParameter ({
        "selectedObject." + key, displayName, category, 0.0f, 1.0f, ParameterRegistry::Polarity::Unipolar,
        ParameterRegistry::Scope::SelectedObject, -1,
        [this, member]
        {
            const int idx = selectedObjectIndex;
            return (idx >= 0 && idx < trajectoryEngine.getNumObjects()) ? (trajectoryEngine.getObject (idx).*member ? 1.0f : 0.0f) : 0.0f;
        },
        [this, member] (float v)
        {
            const int idx = selectedObjectIndex;
            if (idx >= 0 && idx < trajectoryEngine.getNumObjects())
                trajectoryEngine.getObject (idx).*member = (v >= 0.5f);
        }
    });
}

namespace
{
    // One entry per Vec3 axis -- shared by registerObjectVec3Param() and
    // registerSceneVec3Param() below. idSuffix/labelSuffix append onto the
    // field's own key/displayName (e.g. key "orbitCenter" + this ->
    // "orbitCenter.x" / "Orbit Center X"), field is the pointer-to-member
    // used to read/write that one axis of a Vec3 already reached via
    // another pointer-to-member (obj.*vec3Member).*field -- see the
    // callers below for why two chained pointer-to-member dereferences are
    // needed here.
    struct Vec3AxisField { const char* idSuffix; const char* labelSuffix; float Vec3::* field; };
    constexpr Vec3AxisField vec3Axes[] = {
        { "x", " X", &Vec3::x },
        { "y", " Y", &Vec3::y },
        { "z", " Z", &Vec3::z },
    };
}

void KlangorbitProcessor::registerObjectVec3Param (const juce::String& key, const juce::String& displayName,
                                                     const juce::String& category, Vec3 SoundObject::* member,
                                                     float minValue, float maxValue, ParameterRegistry::Polarity polarity)
{
    for (auto& axis : vec3Axes)
    {
        for (int i = 0; i < trajectoryEngine.getNumObjects(); ++i)
        {
            parameterRegistry.registerParameter ({
                "object." + juce::String (i) + "." + key + "." + axis.idSuffix, displayName + axis.labelSuffix, category,
                minValue, maxValue, polarity, ParameterRegistry::Scope::SpecificObject, i,
                [this, member, field = axis.field, i] { return (trajectoryEngine.getObject (i).*member).*field; },
                [this, member, field = axis.field, i] (float v) { (trajectoryEngine.getObject (i).*member).*field = v; }
            });
        }

        parameterRegistry.registerParameter ({
            "selectedObject." + key + "." + axis.idSuffix, displayName + axis.labelSuffix, category,
            minValue, maxValue, polarity, ParameterRegistry::Scope::SelectedObject, -1,
            [this, member, field = axis.field]
            {
                const int idx = selectedObjectIndex;
                return (idx >= 0 && idx < trajectoryEngine.getNumObjects()) ? (trajectoryEngine.getObject (idx).*member).*field : 0.0f;
            },
            [this, member, field = axis.field] (float v)
            {
                const int idx = selectedObjectIndex;
                if (idx >= 0 && idx < trajectoryEngine.getNumObjects())
                    (trajectoryEngine.getObject (idx).*member).*field = v;
            }
        });
    }
}

void KlangorbitProcessor::registerGrainFloatParam (const juce::String& key, const juce::String& displayName,
                                                     const juce::String& category, float GrainCloudSettings::* member,
                                                     float minValue, float maxValue, ParameterRegistry::Polarity polarity)
{
    for (int i = 0; i < trajectoryEngine.getNumGrainClouds(); ++i)
    {
        parameterRegistry.registerParameter ({
            "object." + juce::String (i) + ".grain." + key, displayName, category, minValue, maxValue, polarity,
            ParameterRegistry::Scope::SpecificObject, i,
            [this, member, i] { return trajectoryEngine.getGrainCloud (i).getSettings().*member; },
            [this, member, i] (float v) { trajectoryEngine.getGrainCloud (i).getSettings().*member = v; }
        });
    }

    parameterRegistry.registerParameter ({
        "selectedObject.grain." + key, displayName, category, minValue, maxValue, polarity,
        ParameterRegistry::Scope::SelectedObject, -1,
        [this, member]
        {
            const int idx = selectedObjectIndex;
            return (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds()) ? trajectoryEngine.getGrainCloud (idx).getSettings().*member : 0.0f;
        },
        [this, member] (float v)
        {
            const int idx = selectedObjectIndex;
            if (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds())
                trajectoryEngine.getGrainCloud (idx).getSettings().*member = v;
        }
    });
}

void KlangorbitProcessor::registerGrainBoolParam (const juce::String& key, const juce::String& displayName,
                                                    const juce::String& category, bool GrainCloudSettings::* member)
{
    for (int i = 0; i < trajectoryEngine.getNumGrainClouds(); ++i)
    {
        parameterRegistry.registerParameter ({
            "object." + juce::String (i) + ".grain." + key, displayName, category, 0.0f, 1.0f, ParameterRegistry::Polarity::Unipolar,
            ParameterRegistry::Scope::SpecificObject, i,
            [this, member, i] { return trajectoryEngine.getGrainCloud (i).getSettings().*member ? 1.0f : 0.0f; },
            [this, member, i] (float v) { trajectoryEngine.getGrainCloud (i).getSettings().*member = (v >= 0.5f); }
        });
    }

    parameterRegistry.registerParameter ({
        "selectedObject.grain." + key, displayName, category, 0.0f, 1.0f, ParameterRegistry::Polarity::Unipolar,
        ParameterRegistry::Scope::SelectedObject, -1,
        [this, member]
        {
            const int idx = selectedObjectIndex;
            return (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds()) ? (trajectoryEngine.getGrainCloud (idx).getSettings().*member ? 1.0f : 0.0f) : 0.0f;
        },
        [this, member] (float v)
        {
            const int idx = selectedObjectIndex;
            if (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds())
                trajectoryEngine.getGrainCloud (idx).getSettings().*member = (v >= 0.5f);
        }
    });
}

void KlangorbitProcessor::registerGrainIntParam (const juce::String& key, const juce::String& displayName,
                                                   const juce::String& category, int GrainCloudSettings::* member,
                                                   float minValue, float maxValue)
{
    for (int i = 0; i < trajectoryEngine.getNumGrainClouds(); ++i)
    {
        parameterRegistry.registerParameter ({
            "object." + juce::String (i) + ".grain." + key, displayName, category, minValue, maxValue, ParameterRegistry::Polarity::Unipolar,
            ParameterRegistry::Scope::SpecificObject, i,
            [this, member, i] { return (float) (trajectoryEngine.getGrainCloud (i).getSettings().*member); },
            [this, member, i] (float v) { trajectoryEngine.getGrainCloud (i).getSettings().*member = (int) std::round (v); }
        });
    }

    parameterRegistry.registerParameter ({
        "selectedObject.grain." + key, displayName, category, minValue, maxValue, ParameterRegistry::Polarity::Unipolar,
        ParameterRegistry::Scope::SelectedObject, -1,
        [this, member]
        {
            const int idx = selectedObjectIndex;
            return (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds()) ? (float) (trajectoryEngine.getGrainCloud (idx).getSettings().*member) : 0.0f;
        },
        [this, member] (float v)
        {
            const int idx = selectedObjectIndex;
            if (idx >= 0 && idx < trajectoryEngine.getNumGrainClouds())
                trajectoryEngine.getGrainCloud (idx).getSettings().*member = (int) std::round (v);
        }
    });
}

void KlangorbitProcessor::registerSceneFloatParam (const juce::String& key, const juce::String& displayName,
                                                     const juce::String& category, float SceneSettings::* member,
                                                     float minValue, float maxValue, ParameterRegistry::Polarity polarity)
{
    parameterRegistry.registerParameter ({
        "scene." + key, displayName, category, minValue, maxValue, polarity, ParameterRegistry::Scope::Global, -1,
        [this, member] { return trajectoryEngine.getSceneSettings().*member; },
        [this, member] (float v) { trajectoryEngine.getSceneSettings().*member = v; }
    });
}

void KlangorbitProcessor::registerSceneBoolParam (const juce::String& key, const juce::String& displayName,
                                                    const juce::String& category, bool SceneSettings::* member)
{
    parameterRegistry.registerParameter ({
        "scene." + key, displayName, category, 0.0f, 1.0f, ParameterRegistry::Polarity::Unipolar, ParameterRegistry::Scope::Global, -1,
        [this, member] { return trajectoryEngine.getSceneSettings().*member ? 1.0f : 0.0f; },
        [this, member] (float v) { trajectoryEngine.getSceneSettings().*member = (v >= 0.5f); }
    });
}

void KlangorbitProcessor::registerSceneVec3Param (const juce::String& key, const juce::String& displayName,
                                                    const juce::String& category, Vec3 SceneSettings::* member,
                                                    float minValue, float maxValue, ParameterRegistry::Polarity polarity)
{
    for (auto& axis : vec3Axes)
    {
        parameterRegistry.registerParameter ({
            "scene." + key + "." + axis.idSuffix, displayName + axis.labelSuffix, category,
            minValue, maxValue, polarity, ParameterRegistry::Scope::Global, -1,
            [this, member, field = axis.field] { return (trajectoryEngine.getSceneSettings().*member).*field; },
            [this, member, field = axis.field] (float v) { (trajectoryEngine.getSceneSettings().*member).*field = v; }
        });
    }
}

void KlangorbitProcessor::buildParameterRegistry()
{
    // Ranges/categories mirror ParameterPanel's own rows exactly (see
    // ParameterPanel.cpp) -- both are meant to describe the same set of
    // "things a user/controller can dial in," just through two different
    // control surfaces (mouse-driven sliders vs. this registry's
    // controller-mapping consumers). orbitOrientation is the one
    // exception: it's a real, already-existing SoundObject field with a
    // well-defined range that ParameterPanel's own UI happens not to
    // expose a slider for yet -- included here anyway so a future mapping
    // consumer isn't missing a parameter that genuinely already exists.
    //
    // Deliberately excluded: pure runtime physics STATE (position,
    // velocity, orbitPhase, attractionPulsePhase, orbitRadiusNoiseSmoothed,
    // slingshotTargetId/Strength) and every enum-valued field (SoundObject
    // ::mode/directivityPattern, GrainCloudSettings::windowShape/
    // movementMode/grainReadDepthDistribution, SceneSettings::
    // boundaryBehavior) -- a single float range doesn't naturally fit a
    // fixed choice of N discrete options; see the class comment.

    // --- Scene (Global scope) -------------------------------------------
    // Display names only, below -- the id strings (first argument:
    // "roomSize", "globalField", "windVector") are a stable identifier
    // used by saved mapping profiles/presets and are NOT renamed here,
    // only what's shown to the user in the Learn-mode target picker (see
    // ParameterPanel.cpp's matching label renames for the same reasoning).
    registerSceneFloatParam ("roomSize", "Boundary Size", "Global", &SceneSettings::roomSize, 0.0f, 50.0f);
    registerSceneBoolParam ("showRoomBoundary", "Show Boundary", "Global", &SceneSettings::showRoomBoundary);
    registerSceneVec3Param ("globalField", "Force Field", "Global", &SceneSettings::globalField, -20.0f, 20.0f);
    registerSceneFloatParam ("timeScale", "Time Scale", "Global", &SceneSettings::timeScale, 0.05f, 5.0f);
    registerSceneFloatParam ("speedOfSound", "Speed of Sound", "Global", &SceneSettings::speedOfSound, 1.0f, 400.0f);
    registerSceneFloatParam ("temperature", "Temperature", "Global", &SceneSettings::temperature, -20.0f, 45.0f);
    registerSceneFloatParam ("relativeHumidity", "Relative Humidity", "Global", &SceneSettings::relativeHumidity, 0.0f, 100.0f);
    registerSceneFloatParam ("atmosphericPressure", "Atmospheric Pressure", "Global", &SceneSettings::atmosphericPressure, 80.0f, 110.0f);
    registerSceneVec3Param ("windVector", "Propagation Wind", "Global", &SceneSettings::windVector, -50.0f, 50.0f);

    // --- Object physics ---------------------------------------------------
    registerObjectFloatParam ("mass", "Mass", "Object Physics", &SoundObject::mass, 0.01f, 20.0f);
    registerObjectFloatParam ("gain", "Gain", "Object Physics", &SoundObject::gain, 0.0f, 2.0f);
    registerObjectFloatParam ("damping", "Damping", "Object Physics", &SoundObject::damping, 0.0f, 1.0f);
    registerObjectFloatParam ("maxVelocity", "Max Velocity", "Object Physics", &SoundObject::maxVelocity, 0.0f, 30.0f);
    registerObjectFloatParam ("dragCoefficient", "Drag Coefficient", "Object Physics", &SoundObject::dragCoefficient, 0.0f, 10.0f);
    registerObjectFloatParam ("restitution", "Restitution", "Object Physics", &SoundObject::restitution, 0.0f, 1.0f);
    registerObjectFloatParam ("velocitySnapThreshold", "Stop Threshold", "Object Physics", &SoundObject::velocitySnapThreshold, 0.0f, 1.0f);
    registerObjectBoolParam ("muted", "Mute", "Object Physics", &SoundObject::muted);
    registerObjectBoolParam ("soloed", "Solo", "Object Physics", &SoundObject::soloed);

    // --- Attraction ---------------------------------------------------------
    registerObjectFloatParam ("attractionStrength", "Attraction Strength", "Attraction", &SoundObject::attractionStrength, -10.0f, 10.0f, ParameterRegistry::Polarity::Bipolar);
    registerObjectFloatParam ("forceExponent", "Force Exponent", "Attraction", &SoundObject::forceExponent, 1.0f, 3.0f);
    registerObjectFloatParam ("minDistance", "Min. Distance", "Attraction", &SoundObject::minDistance, 0.01f, 2.0f);
    registerObjectFloatParam ("maxRange", "Max. Range", "Attraction", &SoundObject::maxRange, 0.0f, 20.0f);
    registerObjectFloatParam ("attractionPulseRate", "Pulse Rate", "Attraction", &SoundObject::attractionPulseRate, 0.0f, 5.0f);
    registerObjectFloatParam ("attractionPulseDepth", "Pulse Depth", "Attraction", &SoundObject::attractionPulseDepth, 0.0f, 1.0f);

    // --- Orbit ----------------------------------------------------------
    registerObjectVec3Param ("orbitCenter", "Orbit Center", "Orbit", &SoundObject::orbitCenter, -20.0f, 20.0f);
    registerObjectFloatParam ("orbitRadius", "Orbit Radius", "Orbit", &SoundObject::orbitRadius, 0.05f, 10.0f);
    registerObjectFloatParam ("orbitAngularSpeed", "Orbit Angular Speed", "Orbit", &SoundObject::orbitAngularSpeed, -10.0f, 10.0f, ParameterRegistry::Polarity::Bipolar);
    registerObjectVec3Param ("orbitPlaneNormal", "Orbit Plane Normal", "Orbit", &SoundObject::orbitPlaneNormal, -1.0f, 1.0f);
    registerObjectFloatParam ("orbitEccentricity", "Orbit Eccentricity", "Orbit", &SoundObject::orbitEccentricity, 0.0f, 0.95f);
    registerObjectFloatParam ("orbitOrientation", "Orbit Orientation", "Orbit", &SoundObject::orbitOrientation, 0.0f, juce::MathConstants<float>::twoPi);
    registerObjectFloatParam ("orbitDecay", "Orbit Decay", "Orbit", &SoundObject::orbitDecay, -2.0f, 2.0f, ParameterRegistry::Polarity::Bipolar);
    registerObjectFloatParam ("orbitRadiusBaseline", "Radius Baseline", "Orbit", &SoundObject::orbitRadiusBaseline, 0.05f, 10.0f);
    registerObjectFloatParam ("orbitRadiusReversionRate", "Radius Reversion Rate", "Orbit", &SoundObject::orbitRadiusReversionRate, 0.0f, 5.0f);
    registerObjectFloatParam ("orbitRadiusNoiseAmplitude", "Radius Noise Amplitude", "Orbit", &SoundObject::orbitRadiusNoiseAmplitude, 0.0f, 5.0f);
    registerObjectFloatParam ("orbitRadiusNoiseSmoothing", "Radius Noise Smoothing", "Orbit", &SoundObject::orbitRadiusNoiseSmoothing, 0.0f, 5.0f);

    // --- Doppler ----------------------------------------------------------
    registerObjectBoolParam ("dopplerEnabled", "Doppler Enabled", "Doppler", &SoundObject::dopplerEnabled);
    registerObjectFloatParam ("dopplerFactor", "Doppler Factor", "Doppler", &SoundObject::dopplerFactor, 0.0f, 5.0f);
    registerObjectFloatParam ("dopplerSmoothing", "Doppler Smoothing", "Doppler", &SoundObject::dopplerSmoothing, 0.0f, 2.0f);
    registerObjectVec3Param ("sourceOrientation", "Source Orientation", "Doppler", &SoundObject::sourceOrientation, -1.0f, 1.0f);

    // --- Grain Cloud ------------------------------------------------------
    registerGrainBoolParam ("enabled", "Enabled", "Grain Cloud", &GrainCloudSettings::enabled);
    registerGrainBoolParam ("sourceMuted", "Isolate Grains", "Grain Cloud", &GrainCloudSettings::sourceMuted);
    registerGrainBoolParam ("dopplerEnabled", "Doppler", "Grain Cloud", &GrainCloudSettings::dopplerEnabled);
    registerGrainFloatParam ("grainRate", "Grain Rate", "Grain Cloud", &GrainCloudSettings::grainRate, 0.1f, GrainLimits::maxGrainRate);
    registerGrainFloatParam ("grainRateJitter", "Grain Rate Jitter", "Grain Cloud", &GrainCloudSettings::grainRateJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("grainDuration", "Grain Duration", "Grain Cloud", &GrainCloudSettings::grainDuration, 0.01f, GrainLimits::maxGrainDuration);
    registerGrainFloatParam ("grainDurationJitter", "Grain Duration Jitter", "Grain Cloud", &GrainCloudSettings::grainDurationJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("pitchJitter", "Pitch Jitter", "Grain Cloud", &GrainCloudSettings::pitchJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("positionJitterInBuffer", "Position Jitter In Buffer", "Grain Cloud", &GrainCloudSettings::positionJitterInBuffer, 0.0f, GrainLimits::maxPositionJitterInBuffer);
    registerGrainFloatParam ("grainReadDepthRangeMin", "Read Depth Min", "Grain Cloud", &GrainCloudSettings::grainReadDepthRangeMin, 0.0f, GrainLimits::maxGrainReadDepthRange);
    registerGrainFloatParam ("grainReadDepthRangeMax", "Read Depth Max", "Grain Cloud", &GrainCloudSettings::grainReadDepthRangeMax, 0.0f, GrainLimits::maxGrainReadDepthRange);
    registerGrainIntParam ("maxConcurrentGrains", "Max Concurrent Grains", "Grain Cloud", &GrainCloudSettings::maxConcurrentGrains, 1.0f, 256.0f);
    registerGrainFloatParam ("randomWalkSpeed", "Random Walk Speed", "Grain Cloud", &GrainCloudSettings::randomWalkSpeed, 0.0f, 10.0f);
    registerGrainFloatParam ("boundaryRadius", "Boundary Radius", "Grain Cloud", &GrainCloudSettings::boundaryRadius, 0.05f, 5.0f);
    registerGrainFloatParam ("boundaryRadiusJitter", "Boundary Radius Jitter", "Grain Cloud", &GrainCloudSettings::boundaryRadiusJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("restitution", "Restitution", "Grain Cloud", &GrainCloudSettings::restitution, 0.0f, 1.0f);
    registerGrainFloatParam ("initialSpeed", "Initial Speed", "Grain Cloud", &GrainCloudSettings::initialSpeed, 0.0f, 20.0f);
    registerGrainFloatParam ("initialSpeedJitter", "Initial Speed Jitter", "Grain Cloud", &GrainCloudSettings::initialSpeedJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("acceleration", "Acceleration", "Grain Cloud", &GrainCloudSettings::acceleration, -20.0f, 20.0f, ParameterRegistry::Polarity::Bipolar);
    registerGrainFloatParam ("orbitRadius", "Orbit Radius", "Grain Cloud", &GrainCloudSettings::orbitRadius, 0.05f, 5.0f);
    registerGrainFloatParam ("orbitRadiusJitter", "Orbit Radius Jitter", "Grain Cloud", &GrainCloudSettings::orbitRadiusJitter, 0.0f, 1.0f);
    registerGrainFloatParam ("orbitAngularSpeed", "Orbit Angular Speed", "Grain Cloud", &GrainCloudSettings::orbitAngularSpeed, -10.0f, 10.0f, ParameterRegistry::Polarity::Bipolar);
    registerGrainFloatParam ("orbitSphereSpread", "Orbit Sphere Spread", "Grain Cloud", &GrainCloudSettings::orbitSphereSpread, 0.0f, 1.0f);
    registerGrainFloatParam ("attractionStrength", "Attraction Strength", "Grain Cloud", &GrainCloudSettings::attractionStrength, -10.0f, 10.0f, ParameterRegistry::Polarity::Bipolar);
}

juce::AudioProcessorEditor* KlangorbitProcessor::createEditor()
{
    return new KlangorbitEditor (*this);
}

void KlangorbitProcessor::getStateInformation (juce::MemoryBlock&) { /* TODO: save object/trajectory presets */ }
void KlangorbitProcessor::setStateInformation (const void*, int)   { /* TODO */ }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KlangorbitProcessor();
}
