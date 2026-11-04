#include "DelayInstance.h"
#include <cmath>

DelayInstance::DelayInstance()
{
    settings.seed = rng.getSeed();
    lastAppliedSeed = settings.seed;
    seedInitialized = true;
    resyncTapConfigIfChanged(); // builds the initial `taps`/snapshotBuffer.taps for the default numTaps
}

void DelayInstance::reset()
{
    settings = DelayInstanceSettings{};
    rng.reseedRandomly();
    settings.seed = rng.getSeed();
    lastAppliedSeed = settings.seed;
    orbitPhase = 0.0f;
    lastAppliedNumTaps = -1; // force resyncTapConfigIfChanged() to rebuild on the next update()
    tapConfigInitialized = false;
    resyncTapConfigIfChanged(); // rebuild taps/tapMultiples NOW, so getSnapshot() is never stale even before the next update()

    juce::ScopedLock lock (snapshotLock);
    snapshotBuffer = Snapshot {};
}

void DelayInstance::resyncSeedIfChanged()
{
    if (! seedInitialized || settings.seed != lastAppliedSeed)
    {
        rng.setSeed (settings.seed);
        lastAppliedSeed = settings.seed;
        seedInitialized = true;
    }
}

void DelayInstance::resyncTapConfigIfChanged()
{
    const int clampedNumTaps = juce::jlimit (1, DelayLimits::maxTaps, settings.numTaps);

    // Deliberate bit-exact float comparison (same spirit as GrainCloud::
    // resyncSeedIfChanged()'s int64 one) -- lastAppliedDelayTime/
    // -DelayTimeJitter are only ever assigned directly FROM settings'
    // own fields below, never computed/rounded, so "did the UI/preset
    // actually write a new value since last tick" is exactly what this
    // checks, not an approximate-equality question.
    // Also resample whenever the modifier stack itself was edited in
    // place (feature/modifier-stack) -- see Modifier::operator==' own
    // comment: taps have no per-spawn "redecide" moment the way a
    // grain does, so this is the only way an edited Gain/TimeOffset/
    // Filter value ever reaches an already-existing tap.
    const bool modifiersChanged = settings.modifiers != lastAppliedModifiers;

    const bool changed = ! tapConfigInitialized
                          || clampedNumTaps != lastAppliedNumTaps
                          || settings.delayTime != lastAppliedDelayTime
                          || settings.delayTimeJitter != lastAppliedDelayTimeJitter
                          || modifiersChanged;
    if (! changed)
        return;

    taps.resize ((size_t) clampedNumTaps);
    tapMultiples.resize ((size_t) clampedNumTaps);
    for (int i = 0; i < clampedNumTaps; ++i)
    {
        float multiple = (float) (i + 1);
        if (settings.delayTimeJitter > 0.0f)
            multiple *= 1.0f + settings.delayTimeJitter * rng.nextBipolar();
        tapMultiples[(size_t) i] = juce::jmax (0.01f, multiple);

        // Modifier stack outcome (feature/modifier-stack): sampled once
        // here, same event as the tap's own delay-time multiple above
        // (see Pitch's exclusion note in DelayTap.h -- not applied here).
        const auto modOutcome = ModifierStack::sample (settings.modifiers, rng);
        taps[(size_t) i].modifierTimeOffsetSeconds = modOutcome.timeOffsetSeconds;
        taps[(size_t) i].modifierGainMultiplier = modOutcome.gainMultiplier;
        taps[(size_t) i].modifierFilters = modOutcome.filters;
        taps[(size_t) i].modifierMovement = modOutcome.movement;
        taps[(size_t) i].modifierSpatialOffset = modOutcome.spatialOffset;
        ++taps[(size_t) i].modifierConfigGeneration;
    }

    lastAppliedNumTaps = clampedNumTaps;
    lastAppliedDelayTime = settings.delayTime;
    lastAppliedDelayTimeJitter = settings.delayTimeJitter;
    lastAppliedModifiers = settings.modifiers;
    tapConfigInitialized = true;
}

float DelayInstance::dampingCutoffHzFor (float feedbackDamping)
{
    const float d = juce::jlimit (0.0f, 1.0f, feedbackDamping);
    constexpr float brightHz = 20000.0f;
    constexpr float darkHz = 500.0f;
    return brightHz * std::pow (darkHz / brightHz, d);
}

void DelayInstance::update (double dtSeconds, Vec3 parentPosition, double sampleRate)
{
    resyncSeedIfChanged();
    resyncTapConfigIfChanged();

    const float fdt = (float) dtSeconds;
    orbitPhase += settings.tapOrbitAngularSpeed * fdt;

    const int numTaps = (int) taps.size();
    const float radius = juce::jmax (0.0f, settings.tapSpreadRadius);

    for (int i = 0; i < numTaps; ++i)
    {
        const float phase = orbitPhase + juce::MathConstants<float>::twoPi * (float) i / (float) numTaps;
        const Vec3 offset { std::cos (phase) * radius, std::sin (phase) * radius, 0.0f };
        taps[(size_t) i].position = parentPosition + offset;

        // Tangential velocity (d/dt of the offset above) -- reserved for
        // a future per-tap Doppler option, same shape as GrainCloud's
        // orbiting grains; zero whenever tapOrbitAngularSpeed is 0
        // (static taps), matching that there is then nothing moving.
        const Vec3 tangent { -std::sin (phase) * radius, std::cos (phase) * radius, 0.0f };
        taps[(size_t) i].velocity = tangent * settings.tapOrbitAngularSpeed;

        // tapMultiples[i] (dimensionless, recomputed only by
        // resyncTapConfigIfChanged()) converted to samples at the
        // CURRENT sample rate every tick -- cheap (one multiply), and
        // always correct even if sampleRate itself changes mid-session.
        // modifierTimeOffsetSeconds (feature/modifier-stack) adds
        // directly on top, same "commutative, folds straight into the
        // delay time" treatment as Modifier.h's own class comment
        // describes for grains' bufferReadStartSample.
        auto& tap = taps[(size_t) i];
        tap.delaySamples = juce::jmax (1.0f, tapMultiples[(size_t) i] * settings.delayTime * (float) sampleRate
                                                  + tap.modifierTimeOffsetSeconds * (float) sampleRate);

        // Modifier stack's own Filter-slot drift (feature/modifier-
        // stack) -- same mechanism as GrainCloud::updateGrain()'s
        // equivalent, just once per tap per tick here instead of once
        // per grain.
        ModifierStack::advanceFilterDrift (settings.modifiers, fdt, rng, tap.modifierFilters, (float) sampleRate);

        // Modifier stack's own Movement-type slots (feature/modifier-
        // stack, Stage 2) -- grows the persistent accumulators, then
        // composes them straight onto tap.position (safe here, unlike
        // Grain -- see DelayTap.h's own comment on why: this position
        // is already recomputed from scratch every tick above, never
        // persistently accumulated).
        ModifierStack::advanceMovement (settings.modifiers, fdt, tap.modifierMovement);
        tap.position = ModifierStack::applyMovement (settings.modifiers, tap.modifierMovement, tap.position, parentPosition)
                            + tap.modifierSpatialOffset;
    }

    juce::ScopedLock lock (snapshotLock);
    snapshotBuffer.enabled = settings.enabled;
    snapshotBuffer.taps = taps;
    snapshotBuffer.feedbackDelaySamples = juce::jmax (1.0f, settings.delayTime * (float) sampleRate);
    snapshotBuffer.feedbackGain = juce::jlimit (0.0f, DelayLimits::maxFeedback, settings.feedback);
    snapshotBuffer.dampingCutoffHz = dampingCutoffHzFor (settings.feedbackDamping);
}

void DelayInstance::getSnapshot (Snapshot& out) const
{
    juce::ScopedLock lock (snapshotLock);
    out = snapshotBuffer;
}
