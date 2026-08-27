#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GrainRenderer.h"
#include "GrainDoppler.h"
#include "MuteSoloLogic.h"
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
}

KlangorbitProcessor::~KlangorbitProcessor() = default;

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

void KlangorbitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto blockStartTicks = juce::Time::getHighResolutionTicks();

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
