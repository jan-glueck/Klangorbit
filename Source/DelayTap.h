#pragma once
#include "Vec3.h"
#include "RandomEngine.h"
#include "VoiceFilter.h"

/**
    Settings/data for DelayInstance (feature/delay-instance, Branch 3 of
    the generative sound design effort) -- a genuinely separate spatial
    sound source, NOT an extension of GrainCloud: real feedback
    (recirculation), so echoes actually decay over repeated passes,
    rather than each "voice" independently re-reading a dry, non-
    recirculating buffer the way grains do. See DelayInstance.h's own
    class comment for the full architecture.
*/
namespace DelayLimits
{
    constexpr int maxTaps = 8;
    constexpr float maxDelayTimeSeconds = 2.0f;     // the base unit -- tap i's delay = delayTime * (i+1)
    constexpr float maxFeedback = 0.95f;            // hard safety ceiling against runaway recirculation
    constexpr float maxTapSpreadRadius = 10.0f;     // m, ParameterPanel's upper slider bound
    constexpr float maxTapOrbitAngularSpeed = 10.0f; // rad/s

    // The recirculating buffer must hold the longest tap's reach
    // (maxTaps repeats of maxDelayTimeSeconds), plus headroom for
    // delayTimeJitter -- same "size the buffer to the slider's own
    // documented maximum" discipline as GrainLimits::
    // requiredRingBufferSeconds (Grain.h).
    constexpr float requiredBufferSeconds = maxDelayTimeSeconds * (float) maxTaps + 1.0f;
}

struct DelayInstanceSettings
{
    bool enabled = false;

    // 1..DelayLimits::maxTaps -- tap i (0-indexed) sits at delayTime*(i+1)
    // seconds behind the live signal. Because the SAME recirculating
    // buffer already contains its own decayed past repeats (see
    // DelayInstance.h), reading further back automatically yields
    // progressively quieter, darker echoes -- taps don't each need their
    // own independent feedback path.
    int numTaps = 3;

    float delayTime = 0.3f;          // seconds, the base unit (see numTaps above)
    // 0..1, per-tap +/- randomization of its own multiple of delayTime,
    // sampled once whenever numTaps/delayTime/delayTimeJitter/seed
    // change (see DelayInstance::resyncTapConfigIfChanged()) -- not
    // re-sampled continuously, so a tap's own delay stays fixed between
    // config changes (predictable rhythm, not audio-rate jitter).
    float delayTimeJitter = 0.0f;

    float feedback = 0.3f;           // 0..DelayLimits::maxFeedback, recirculation gain
    // 0..1 -- darkens the recirculating signal via a shared VoiceFilter
    // (Source/VoiceFilter.h, LowPass) in the feedback path, the classic
    // "repeats get duller as they decay" analog/tape-delay character.
    // 0 = no darkening (cutoff pinned near Nyquist); see DelayInstance::
    // dampingCutoffHzFor() for the mapping curve. One shared filter for
    // the whole feedback bus, NOT one per tap (there is only one
    // recirculating signal; taps are just multiple read points into it).
    float feedbackDamping = 0.0f;

    // Taps are arranged evenly around a circle of this radius, centered
    // on the parent SoundObject's CURRENT (possibly moving) position --
    // same "tracks the live parent" treatment as GrainMovementMode::
    // OrbitAroundParent.
    float tapSpreadRadius = 1.0f;
    // 0 (default) = static angular positions; >0 = the whole ring of
    // taps slowly rotates around the parent at this rate, each tap
    // keeping its own fixed phase offset from the others.
    float tapOrbitAngularSpeed = 0.0f;

    // Seeds this instance's own delayTimeJitter sampling (RandomEngine.h)
    // -- same string-persisted-seed treatment as GrainCloudSettings::seed,
    // for exact reproducibility on reload.
    juce::int64 seed = 0;
};

/** One tap's current, control-rate-decided state -- published via DelayInstance::Snapshot. */
struct DelayTap
{
    Vec3 position {};
    Vec3 velocity {};       // reserved for a future per-tap Doppler option, same shape as Grain::velocity
    float delaySamples = 0.0f; // this tap's own delay (includes delayTimeJitter), in samples
};
