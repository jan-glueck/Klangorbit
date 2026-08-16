#pragma once
#include <vector>
#include "SoundObject.h"
#include "SceneSettings.h"

/**
    Per-object acoustic propagation effects, applied to the mono source
    signal BEFORE spatial (Ambisonics) encoding: propagation delay,
    Doppler pitch shift, frequency-dependent air absorption, and source
    directivity (angle-dependent gain, returned separately since it's
    cheap to fold into the encoder's existing ramped gain parameter).

    Propagation delay and Doppler shift are implemented as ONE mechanism,
    not two: a variable-length delay line whose target length continuously
    tracks distance/effectiveSpeedOfSound. Reading it through a smoothly
    time-varying (interpolated) delay naturally produces the correct
    Doppler pitch ratio as a side effect of the delay's rate of change --
    no separate pitch-shifter/resampler needed. SoundObject::dopplerFactor
    scales only that rate-of-change ("AC") component; the delay's absolute
    value ("DC" component, i.e. the actual latency) is always slowly
    corrected back toward the true distance-based value regardless of
    dopplerFactor, so long-term latency never drifts away from reality even
    when the audible pitch effect is turned down or exaggerated.

    Air absorption here is a deliberately simplified, "good enough for
    sound design" approximation (a single one-pole lowpass per object,
    cutoff derived from distance/temperature/humidity/pressure via cheap
    closed-form curves) -- NOT an implementation of the full ISO 9613-1
    relaxation-frequency equations. Don't rely on it for acoustic
    measurement accuracy.

    Real-time-safe: prepare() is the only place that allocates. process()
    and reset() never allocate and never lock.
*/
class PropagationProcessor
{
public:
    void prepare (double sampleRate, int maxBlockSize);

    // Clears delay/filter state. Call when an object transitions from
    // inactive to active (see PluginProcessor), so a freshly (re)activated
    // object doesn't inherit stale state from whatever used that slot
    // before. The next process() call after reset() snaps directly to the
    // correct delay with no artifact (see previousTargetDelaySamples).
    void reset();

    /**
        Processes one block. sourceBlock and destBlock must be distinct
        (non-aliasing) buffers of numSamples each.

        objectPosition: current position, listener at the origin.
        directivityGainOut: receives this block's directivity gain: the
            caller is expected to fold it into the gain it passes to
            AmbisonicsEncoder::encodeBlock(), which already ramps gain
            changes smoothly per channel -- so directivity doesn't need
            its own separate smoothing/ramping here.
    */
    void process (const float* sourceBlock,
                   float* destBlock,
                   int numSamples,
                   Vec3 objectPosition,
                   const SoundObject& obj,
                   const SceneSettings& scene,
                   float& directivityGainOut);

private:
    double sampleRate = 48000.0;

    // --- Delay line (propagation delay + Doppler, unified) ---------------
    std::vector<float> delayBuffer;
    int writePos = 0;
    float currentDelaySamples = 0.0f;
    // < 0 acts as a sentinel meaning "no previous block yet" -- process()
    // then snaps currentDelaySamples straight to the target instead of
    // ramping from a meaningless previous value.
    float previousTargetDelaySamples = -1.0f;
    float smoothedDerivative = 0.0f;

    // --- Air absorption (one-pole lowpass) --------------------------------
    float filterState = 0.0f;

    static constexpr float maxDelaySeconds = 2.0f;
    // Rate-limits (not proportionally, but by an absolute cap per second)
    // how quickly the delay's absolute value may re-converge toward the
    // true distance-based target, independent of dopplerFactor -- keeps
    // latency anchored over time without ever contributing more than this
    // dimensionless amount of its own pitch deviation. A proportional
    // correction (e.g. simple exponential smoothing) would re-introduce a
    // full-strength pitch shift during fast, sustained movement even at
    // dopplerFactor=0, since the correction itself would then dominate --
    // capping the rate keeps its contribution negligible (<0.1%) instead,
    // at the cost of large gaps (e.g. a manual scene edit that suddenly
    // moves an object) taking longer to fully settle.
    static constexpr float maxDriftRatePerSecond = 0.001f;
};
