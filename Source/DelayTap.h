#pragma once
#include "Vec3.h"
#include "RandomEngine.h"
#include "VoiceFilter.h"
#include "Modifier.h"

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

    // --- Modifier stack (feature/modifier-stack) -------------------------
    // Ordered, stackable list of per-tap modifiers (Gain/TimeOffset/
    // Filter -- NOT Pitch, see this file's own note below) applied ON
    // TOP OF everything above. Empty by default, so an instance with no
    // stack configured is identical to before this field existed.
    //
    // Pitch is deliberately NOT applied to delay taps (even though
    // ModifierType::Pitch exists and can technically be added to this
    // stack): a tap is a continuous, fixed-rate readback of an
    // already-recirculating signal (see DelayRenderer.h) -- genuinely
    // pitch-shifting that without either drifting away from the
    // configured delay time or needing a whole granular/overlapping-
    // window resampler (effectively a second GrainCloud) is real,
    // separate DSP work this branch doesn't take on. ParameterPanel's
    // Delay category simply doesn't offer Pitch as a modifier type to
    // add, so the gap is never user-visible as a broken control.
    std::vector<Modifier> modifiers;
};

/** One tap's current, control-rate-decided state -- published via DelayInstance::Snapshot. */
struct DelayTap
{
    Vec3 position {};
    Vec3 velocity {};       // reserved for a future per-tap Doppler option, same shape as Grain::velocity
    float delaySamples = 0.0f; // this tap's own delay (includes delayTimeJitter AND modifierTimeOffsetSeconds below), in samples

    // Modifier stack outcome (feature/modifier-stack), sampled once
    // whenever DelayInstance::resyncTapConfigIfChanged() rebuilds this
    // tap's configuration (same event as its own delay-time multiple) --
    // see DelayInstance.cpp. modifierTimeOffsetSeconds folds directly
    // into delaySamples above every tick (DelayInstance::update()),
    // exactly like every other config-time-decided contributor to that
    // value; Gain and the Filter slots need their own storage (applied
    // at render time, not at delay-time computation time).
    float modifierTimeOffsetSeconds = 0.0f;
    float modifierGainMultiplier = 1.0f;
    ModifierStack::FilterBankState modifierFilters;

    // Movement-type slots' own persistent accumulators (feature/
    // modifier-stack, Stage 2) -- grown every control tick by
    // ModifierStack::advanceMovement(), then composed onto `position`
    // above via ModifierStack::applyMovement() in DelayInstance::
    // update(), right after that tick's ring-arrangement position is
    // computed. Safe to overwrite `position` directly with the result
    // here (unlike Grain, see its own comment on why it keeps the two
    // separate): a tap's ring position is ALREADY recomputed from
    // scratch every tick, never persistently accumulated, so there is
    // no feedback-loop risk from folding the overlay straight in.
    ModifierStack::MovementBankState modifierMovement;

    // SpatialOffset-type slots' own aggregate contribution (feature/
    // modifier-stack, Stage 3) -- sampled once at the same
    // resyncTapConfigIfChanged() event as modifierTimeOffsetSeconds
    // above, CONSTANT until the next resync. Composed onto `position`
    // the same way/place as modifierMovement above (safe to add
    // straight in, same reasoning).
    Vec3 modifierSpatialOffset {};

    // Bumped every time DelayInstance::resyncTapConfigIfChanged()
    // resamples this tap's modifier outcome above -- lets the audio
    // thread detect "this tap's filter slots were just (re)configured"
    // (possibly with a different type/count than before) and do a hard
    // VoiceFilter::resetWithValues() instead of nudging coefficients on
    // top of stale state, same role as Grain::spawnGeneration but for
    // config changes rather than a brand-new pool-slot occupant (taps
    // don't rotate through a pool the way grains do).
    int modifierConfigGeneration = 0;
};
