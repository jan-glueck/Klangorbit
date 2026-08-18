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
}

SpatialAudioPOCProcessor::BusesProperties SpatialAudioPOCProcessor::makeBusLayout()
{
    return BusesProperties()
        .withInput  ("Live Inputs", juce::AudioChannelSet::discreteChannels (SAPOC_MAX_LIVE_INPUTS), true)
        .withOutput ("Ambisonics", juce::AudioChannelSet::discreteChannels ((SAPOC_DEFAULT_AMBI_ORDER + 1) * (SAPOC_DEFAULT_AMBI_ORDER + 1)), true);
}

SpatialAudioPOCProcessor::SpatialAudioPOCProcessor()
    : juce::AudioProcessor (makeBusLayout())
{
    encoder.setOrder (SAPOC_DEFAULT_AMBI_ORDER);

    // Only object 0 starts active (input channel 0). Further objects are
    // added via the GUI (TrajectoryEngine::activateObject()) -- the input
    // channel assignment stays the simple 1:1 mapping of object index ==
    // channel index.
    trajectoryEngine.activateObject (0);

    previousGainsPerObject.resize ((size_t) numLiveInputs);
    for (auto& g : previousGainsPerObject)
        g.assign ((size_t) encoder.getNumChannels(), 0.0f);

    muteRampGain.assign ((size_t) numLiveInputs, 1.0f); // start unmuted/audible

    propagationPerObject.resize ((size_t) numLiveInputs);
    wasActiveLastBlock.assign ((size_t) numLiveInputs, false);

    for (auto& wh : grainRingBufferWriteHead)
        wh.store (0, std::memory_order_relaxed);
}

SpatialAudioPOCProcessor::~SpatialAudioPOCProcessor() = default;

void SpatialAudioPOCProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    encoder.prepare (sampleRate, samplesPerBlock);

    for (auto& g : previousGainsPerObject)
        g.assign ((size_t) encoder.getNumChannels(), 0.0f);

    for (auto& p : propagationPerObject)
        p.prepare (sampleRate, samplesPerBlock);
    std::fill (wasActiveLastBlock.begin(), wasActiveLastBlock.end(), false);
    std::fill (muteRampGain.begin(), muteRampGain.end(), 1.0f);

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

void SpatialAudioPOCProcessor::releaseResources() {}

bool SpatialAudioPOCProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // For the POC: fixed layouts as defined in makeBusLayout().
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::discreteChannels (numLiveInputs)
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::discreteChannels (encoder.getNumChannels());
}

void SpatialAudioPOCProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

        // Only once the ramp has actually reached silence (not just close
        // to it) do we skip propagation+encoding entirely for this object
        // -- the actual performance win, rather than paying full cost
        // every block just to encode silence while muted/soloed-out.
        const bool audioProcessingActive = ! (effectivelyMuted && ramp <= 0.0f);

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
                              obj.gain * directivityGain * ramp,
                              buffer,
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
        // reasoning as the main-object loop above.
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
                                  obj.gain * muteRamp, buffer, state.previousChannelGains);
        }
    }

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

juce::AudioProcessorEditor* SpatialAudioPOCProcessor::createEditor()
{
    return new SpatialAudioPOCEditor (*this);
}

void SpatialAudioPOCProcessor::getStateInformation (juce::MemoryBlock&) { /* TODO: save object/trajectory presets */ }
void SpatialAudioPOCProcessor::setStateInformation (const void*, int)   { /* TODO */ }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpatialAudioPOCProcessor();
}
