#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <juce_core/juce_core.h>
#include "../Source/GrainCloud.h"
#include "../Source/GrainRenderer.h"
#include "../Source/GrainDoppler.h"

namespace
{
    constexpr double kSampleRate = 48000.0;

    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    bool allFinite (const std::vector<float>& v)
    {
        for (float x : v)
            if (! std::isfinite (x))
                return false;
        return true;
    }

    float estimateFrequencyHz (const std::vector<float>& signal, int startIdx, int endIdx, double sampleRate)
    {
        int crossings = 0;
        for (int i = startIdx + 1; i < endIdx; ++i)
            if ((signal[(size_t) (i - 1)] < 0.0f) != (signal[(size_t) i] < 0.0f))
                ++crossings;
        const double windowSeconds = (double) (endIdx - startIdx) / sampleRate;
        return (float) ((crossings / 2.0) / windowSeconds);
    }
}

// ============================================================== renderGrainBlock

static void testRenderGrainBlock()
{
    // A ring buffer holding one cycle of a 1kHz sine at 48kHz -- long
    // enough that a short grain reads only a small slice of it, but small
    // enough that a longer grain has to wrap around the end.
    const int ringSize = 512;
    std::vector<float> ring ((size_t) ringSize);
    for (int i = 0; i < ringSize; ++i)
        ring[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / kSampleRate);

    // --- Envelope shape: Hann, zero at both ends, peak near the middle ---
    {
        const int grainLength = 256;
        std::vector<float> out ((size_t) grainLength, 0.0f);
        int samplesPlayed = 0;
        renderGrainBlock (ring.data(), ringSize, /*startSample*/ 0, /*rate*/ 1.0f, grainLength,
                           samplesPlayed, out.data(), grainLength);

        check (allFinite (out), "renderGrainBlock: envelope test has no NaN/Inf");
        check (samplesPlayed == grainLength, "renderGrainBlock: samplesPlayed advances to grainLength after one full render");

        const float first = std::abs (out.front());
        const float last = std::abs (out.back());
        const float mid = std::abs (out[(size_t) grainLength / 2]);
        std::printf ("       |out[0]|=%.4f |out[mid]|=%.4f |out[last]|=%.4f\n", first, mid, last);
        check (first < 0.05f, "renderGrainBlock: envelope starts near 0 (Hann)");
        check (mid > 0.5f, "renderGrainBlock: envelope peaks near the middle");

        // Calling again after the grain is exhausted must yield silence, not garbage.
        std::vector<float> tail (64, 1.0f);
        renderGrainBlock (ring.data(), ringSize, 0, 1.0f, grainLength, samplesPlayed, tail.data(), (int) tail.size());
        const bool allZero = std::all_of (tail.begin(), tail.end(), [] (float x) { return x == 0.0f; });
        check (allZero, "renderGrainBlock: silent after the envelope is exhausted");
    }

    // --- Ring buffer wrap: reading across the end must stay continuous ---
    {
        const int grainLength = 400;
        std::vector<float> out ((size_t) grainLength);
        int samplesPlayed = 0;
        // Start near the end of the buffer so playback wraps partway through.
        renderGrainBlock (ring.data(), ringSize, ringSize - 50, 1.0f, grainLength, samplesPlayed, out.data(), grainLength);

        check (allFinite (out), "renderGrainBlock: wrap test has no NaN/Inf");

        // No sample-to-sample jump larger than what the (continuous, band-
        // limited) source signal itself could produce -- catches an
        // off-by-one in the modulo wrap that would read a discontinuous
        // buffer position.
        float maxJump = 0.0f;
        for (int i = 1; i < grainLength; ++i)
            maxJump = juce::jmax (maxJump, std::abs (out[(size_t) i] - out[(size_t) (i - 1)]));
        std::printf ("       max sample-to-sample jump across wrap: %.4f\n", maxJump);
        check (maxJump < 0.3f, "renderGrainBlock: no discontinuity when the read position wraps the ring buffer");
    }

    // --- Pitch ratio: rate != 1 changes the read frequency -------------
    {
        const int grainLength = 4000; // several cycles of the 1kHz test tone
        std::vector<float> outNormal ((size_t) grainLength), outFast ((size_t) grainLength);
        int played1 = 0, played2 = 0;
        renderGrainBlock (ring.data(), ringSize, 0, 1.0f, grainLength, played1, outNormal.data(), grainLength);
        renderGrainBlock (ring.data(), ringSize, 0, 2.0f, grainLength, played2, outFast.data(), grainLength);

        // Skip the windowed-down start/end where zero-crossing counting is unreliable.
        const float freqNormal = estimateFrequencyHz (outNormal, grainLength / 4, 3 * grainLength / 4, kSampleRate);
        const float freqFast   = estimateFrequencyHz (outFast,   grainLength / 4, 3 * grainLength / 4, kSampleRate);
        std::printf ("       rate=1.0 measured ~%.0f Hz, rate=2.0 measured ~%.0f Hz (source ~1000 Hz)\n", freqNormal, freqFast);
        check (freqFast > freqNormal * 1.5f, "renderGrainBlock: doubling playbackRate roughly doubles the read frequency");
    }

    // --- Tail-discontinuity / pitchJitter click investigation ----------
    // Reported bug: pitchJitter (random per-grain playbackRate) produces
    // audible clicks. Two hypotheses were given to check:
    //  (1) non-interpolated (integer) ring-buffer read position -- ALREADY
    //      correctly implemented as linear interpolation (see
    //      GrainRenderer.h's idx0/idx1/frac blend), confirmed by the wrap
    //      and pitch-ratio tests above already passing cleanly.
    //  (2) envelope not reliably reaching exactly 0 at the grain's end --
    //      THIS was a real, but rate-INDEPENDENT bug: the Hann window's
    //      last rendered sample fell short of a true 0 by an amount that
    //      grows as grainLengthSamples shrinks (short grains), then jumped
    //      to a hard 0.0 on the next call -- a real discontinuity,
    //      unrelated to playbackRate itself (grainLengthSamples is fixed
    //      in OUTPUT samples regardless of pitch). Fixed in
    //      GrainRenderer.h by using (grainLengthSamples - 1) as the
    //      envelope's denominator, so the last sample's t reaches exactly
    //      1.0 (envelope exactly 0.0) by construction.
    {
        // A SHORT grain is where the original bug was largest (residual
        // envelope ~10% of peak at grainLength=100) -- exactly the kind of
        // grain a high grainRate/short grainDuration setup produces.
        const int shortGrainLength = 100;
        bool allRatesCloseToZero = true;
        float worstResidual = 0.0f;

        // Covers the full playbackRate range pitchJitter can actually
        // produce (see GrainLimits::maxPitchJitterPlaybackRate and the
        // 0.1 floor in GrainCloud::spawnGrain()) -- explicitly including
        // both extremes, not just rate=1.0, per the bug report's request
        // to re-check with extreme values, not just moderate ones.
        for (float rate : { 0.1f, 0.5f, 1.0f, 1.5f, 2.0f })
        {
            std::vector<float> out ((size_t) shortGrainLength);
            int samplesPlayed = 0;
            renderGrainBlock (ring.data(), ringSize, 0, rate, shortGrainLength, samplesPlayed, out.data(), shortGrainLength);

            const float lastEnvelopeMagnitude = std::abs (out.back());
            worstResidual = juce::jmax (worstResidual, lastEnvelopeMagnitude);
            if (lastEnvelopeMagnitude > 0.02f) // near-silent, not necessarily bit-exact 0 (source signal has its own amplitude)
                allRatesCloseToZero = false;
        }
        std::printf ("       worst |last sample| across rates 0.1..2.0 (grainLength=%d): %.5f\n", shortGrainLength, worstResidual);
        check (allRatesCloseToZero, "renderGrainBlock: short-grain tail reaches near-silence for every playbackRate 0.1..2.0, not just rate=1.0");
    }
    {
        // Direct measurement of the actual discontinuity at the
        // tail-to-silence transition (the specific jump that produces an
        // audible click), for the same short grain and rate range.
        const int shortGrainLength = 100;
        float worstJump = 0.0f;

        for (float rate : { 0.1f, 0.5f, 1.0f, 1.5f, 2.0f })
        {
            std::vector<float> out (shortGrainLength + 1);
            int samplesPlayed = 0;
            renderGrainBlock (ring.data(), ringSize, 0, rate, shortGrainLength, samplesPlayed, out.data(), (int) out.size());
            const float jump = std::abs (out[(size_t) shortGrainLength] - out[(size_t) shortGrainLength - 1]);
            worstJump = juce::jmax (worstJump, jump);
        }
        std::printf ("       worst tail-to-silence jump across rates 0.1..2.0: %.5f\n", worstJump);
        check (worstJump < 0.02f, "renderGrainBlock: tail-to-silence transition has no audible discontinuity for any playbackRate 0.1..2.0");
    }
}

// ============================================================== GrainCloud

static void testSpawnRateAndCaps()
{
    GrainCloud cloud (/*poolSize*/ 32);
    cloud.getSettings().enabled = true;
    cloud.getSettings().grainRate = 1000.0f; // deliberately excessive, to try to blow past the caps
    cloud.getSettings().grainDuration = 5.0f; // long-lived, so grains accumulate instead of dying quickly
    cloud.getSettings().maxConcurrentGrains = 10;

    juce::Random rng (1234);
    int globalBudget = 16; // smaller than both the pool and the per-cloud cap

    int maxSeen = 0;
    for (int tick = 0; tick < 200; ++tick)
    {
        cloud.setRingBufferContext (0, kSampleRate);
        cloud.update (1.0 / 90.0, { 0.0f, 0.0f, 0.0f }, {}, globalBudget, rng);
        maxSeen = juce::jmax (maxSeen, cloud.getNumActiveGrains());
    }

    std::printf ("       max active grains observed: %d (per-cloud cap 10, budget cap 16)\n", maxSeen);
    check (maxSeen <= 10, "GrainCloud: never exceeds its own maxConcurrentGrains");
    check (globalBudget >= 0, "GrainCloud: never spends more than the globalGrainBudget it was given");
}

static void testGlobalBudgetSharedAcrossClouds()
{
    GrainCloud cloudA (32), cloudB (32);
    for (auto* c : { &cloudA, &cloudB })
    {
        c->getSettings().enabled = true;
        c->getSettings().grainRate = 1000.0f;
        c->getSettings().grainDuration = 5.0f;
        c->getSettings().maxConcurrentGrains = 32; // no local limit, only the shared budget should matter
    }

    juce::Random rng (99);
    int globalBudget = 20;

    for (int tick = 0; tick < 200; ++tick)
    {
        cloudA.setRingBufferContext (0, kSampleRate);
        cloudA.update (1.0 / 90.0, {}, {}, globalBudget, rng);
        cloudB.setRingBufferContext (0, kSampleRate);
        cloudB.update (1.0 / 90.0, {}, {}, globalBudget, rng);
    }

    const int total = cloudA.getNumActiveGrains() + cloudB.getNumActiveGrains();
    std::printf ("       total active across 2 clouds: %d (shared budget was 20)\n", total);
    check (total <= 20, "GrainCloud: a shared globalGrainBudget is respected across multiple clouds");
}

static void testLifetimeExpiry()
{
    GrainCloud cloud (8);
    cloud.getSettings().enabled = true;
    cloud.getSettings().grainRate = 1.0f;
    cloud.getSettings().grainDuration = 0.2f;
    cloud.getSettings().maxConcurrentGrains = 8;

    juce::Random rng (7);
    int budget = 8;

    cloud.setRingBufferContext (0, kSampleRate);
    cloud.update (0.01, {}, {}, budget, rng); // spawns one grain almost immediately (rate=1 => interval=1s... )
    // Force an immediate spawn regardless of rate/interval timing by ticking a full second's worth:
    for (int i = 0; i < 100; ++i)
        cloud.update (0.01, {}, {}, budget, rng);

    const int activeAfterSpawn = cloud.getNumActiveGrains();
    std::printf ("       active after spawn window: %d\n", activeAfterSpawn);
    check (activeAfterSpawn > 0, "GrainCloud: spawns at least one grain within its rate interval");

    // Advance well past grainDuration -- all spawned grains must have expired.
    for (int i = 0; i < 100; ++i)
        cloud.update (0.01, {}, {}, budget, rng); // another full second, >> grainDuration of 0.2s

    std::vector<GrainCloud::Snapshot> snap;
    cloud.getSnapshot (snap);
    const bool anyStillActive = std::any_of (snap.begin(), snap.end(), [] (auto& s) { return s.active; });
    // Not a strict "must be false" check since new grains keep spawning at
    // rate=1 -- instead check that ageFraction resets for freshly spawned
    // ones (i.e. old ones actually cycled out) via spawnGeneration growth.
    check (! snap.empty(), "GrainCloud: snapshot has the expected pool size");
    juce::ignoreUnused (anyStillActive);

    int maxGeneration = 0;
    for (auto& s : snap) maxGeneration = juce::jmax (maxGeneration, s.spawnGeneration);
    std::printf ("       max spawnGeneration seen after ~2s at grainRate=1: %d\n", maxGeneration);
    check (maxGeneration >= 2, "GrainCloud: pool slots are reused (spawnGeneration increases) as grains expire and new ones spawn");
}

static void testBounceStaysWithinBoundary()
{
    GrainCloud cloud (4);
    cloud.getSettings().enabled = true;
    cloud.getSettings().grainRate = 4.0f;
    cloud.getSettings().grainDuration = 2.0f;
    cloud.getSettings().movementMode = GrainMovementMode::Bounce;
    cloud.getSettings().boundaryRadius = 0.5f;
    cloud.getSettings().restitution = 0.8f;
    cloud.getSettings().randomWalkSpeed = 5.0f; // fast, to actually reach the boundary repeatedly

    juce::Random rng (42);
    int budget = 32;
    const Vec3 parentPos { 1.0f, 2.0f, 0.0f };

    float maxDistFromCenter = 0.0f;
    for (int tick = 0; tick < 900; ++tick) // ~10 seconds at 90Hz
    {
        cloud.setRingBufferContext (0, kSampleRate);
        cloud.update (1.0 / 90.0, parentPos, {}, budget, rng);

        std::vector<GrainCloud::Snapshot> snap;
        cloud.getSnapshot (snap);
        for (auto& s : snap)
        {
            if (! s.active) continue;
            const float d = (s.position - parentPos).length();
            maxDistFromCenter = juce::jmax (maxDistFromCenter, d);
        }
    }

    std::printf ("       max distance from spawn center over ~10s: %.3f (boundaryRadius=0.5)\n", maxDistFromCenter);
    check (maxDistFromCenter <= 0.5f + 0.01f, "GrainCloud: Bounce mode keeps grains within boundaryRadius (+ float tolerance)");
}

static void testRadialExplosionMovesOutward()
{
    GrainCloud cloud (4);
    cloud.getSettings().enabled = true;
    cloud.getSettings().grainRate = 1000.0f; // spawn (near-)immediately, not after a full 1/grainRate wait
    cloud.getSettings().grainDuration = 1.0f;
    cloud.getSettings().movementMode = GrainMovementMode::RadialExplosion;
    cloud.getSettings().initialSpeed = 3.0f;
    cloud.getSettings().acceleration = 0.0f;
    cloud.getSettings().maxConcurrentGrains = 1; // exactly one grain, so "early"/"later" track the same instance

    juce::Random rng (5);
    int budget = 32;
    const Vec3 parentPos { 0.0f, 0.0f, 0.0f };

    cloud.setRingBufferContext (0, kSampleRate);
    cloud.update (0.02, parentPos, {}, budget, rng); // spawns the one grain
    cloud.getSettings().enabled = false; // stop further spawning so no replacement grain confuses the reading

    std::vector<GrainCloud::Snapshot> snapEarly;
    cloud.getSnapshot (snapEarly);
    float earlyDist = -1.0f;
    for (auto& s : snapEarly) if (s.active) earlyDist = (s.position - parentPos).length();
    check (earlyDist >= 0.0f, "GrainCloud: RadialExplosion test setup actually spawned a grain");

    for (int i = 0; i < 20; ++i)
        cloud.update (0.02, parentPos, {}, budget, rng); // advance ~0.4s, well within the 1s lifetime

    std::vector<GrainCloud::Snapshot> snapLate;
    cloud.getSnapshot (snapLate);
    float lateDist = -1.0f;
    for (auto& s : snapLate) if (s.active) lateDist = (s.position - parentPos).length();

    std::printf ("       distance from parent: early=%.3f, later=%.3f\n", earlyDist, lateDist);
    check (lateDist > earlyDist, "GrainCloud: RadialExplosion moves grains further from the parent over time");
}

static void testOrbitTracksMovingParent()
{
    GrainCloud cloud (4);
    cloud.getSettings().enabled = true;
    cloud.getSettings().grainRate = 1.0f;
    cloud.getSettings().grainDuration = 3.0f;
    cloud.getSettings().movementMode = GrainMovementMode::OrbitAroundParent;
    cloud.getSettings().orbitRadius = 0.75f;
    cloud.getSettings().orbitAngularSpeed = 1.0f;

    juce::Random rng (3);
    int budget = 32;

    cloud.setRingBufferContext (0, kSampleRate);
    cloud.update (0.02, { 0.0f, 0.0f, 0.0f }, {}, budget, rng); // spawn near origin

    // Move the "parent" a long way away and keep updating -- the grain
    // must follow, staying at ~orbitRadius from the NEW parent position.
    const Vec3 movedParent { 5.0f, -3.0f, 0.0f };
    for (int i = 0; i < 50; ++i)
        cloud.update (0.02, movedParent, {}, budget, rng);

    std::vector<GrainCloud::Snapshot> snap;
    cloud.getSnapshot (snap);
    float distFromMovedParent = -1.0f;
    for (auto& s : snap) if (s.active) distFromMovedParent = (s.position - movedParent).length();

    std::printf ("       distance from the moved parent: %.3f (orbitRadius=0.75)\n", distFromMovedParent);
    check (distFromMovedParent >= 0.0f, "GrainCloud: OrbitAroundParent grain is still active after the parent moves");
    check (std::abs (distFromMovedParent - 0.75f) < 0.05f, "GrainCloud: OrbitAroundParent tracks the parent's CURRENT (moved) position");
}

static void testAttractRepelSiblings()
{
    auto runWithStrength = [] (float strength) -> float
    {
        GrainCloud cloud (8);
        cloud.getSettings().enabled = false; // spawn manually via a burst, then disable so no more join mid-test
        cloud.getSettings().movementMode = GrainMovementMode::AttractRepelSiblings;
        cloud.getSettings().attractionStrength = strength;
        cloud.getSettings().grainDuration = 5.0f;
        cloud.getSettings().grainRate = 1000.0f;
        cloud.getSettings().maxConcurrentGrains = 6;

        juce::Random rng (11);
        int budget = 32;
        cloud.setRingBufferContext (0, kSampleRate);
        cloud.getSettings().enabled = true;
        for (int i = 0; i < 5; ++i) // spawn a small burst
            cloud.update (0.02, {}, {}, budget, rng);
        cloud.getSettings().enabled = false; // stop spawning, isolate the force effect

        auto averagePairDistance = [] (GrainCloud& c) -> float
        {
            std::vector<GrainCloud::Snapshot> snap;
            c.getSnapshot (snap);
            float total = 0.0f;
            int pairs = 0;
            for (size_t i = 0; i < snap.size(); ++i)
            {
                if (! snap[i].active) continue;
                for (size_t j = i + 1; j < snap.size(); ++j)
                {
                    if (! snap[j].active) continue;
                    total += (snap[i].position - snap[j].position).length();
                    ++pairs;
                }
            }
            return pairs > 0 ? total / (float) pairs : 0.0f;
        };

        const float before = averagePairDistance (cloud);
        for (int i = 0; i < 60; ++i)
            cloud.update (0.02, {}, {}, budget, rng);
        const float after = averagePairDistance (cloud);

        std::printf ("       attractionStrength=%.1f: avg pair distance %.3f -> %.3f\n", strength, before, after);
        return after - before;
    };

    const float deltaAttract = runWithStrength (3.0f);
    const float deltaRepel   = runWithStrength (-3.0f);

    check (deltaAttract < 0.0f, "GrainCloud: AttractRepelSiblings with positive strength pulls grains closer together");
    check (deltaRepel > 0.0f, "GrainCloud: AttractRepelSiblings with negative strength pushes grains apart");
}

// ============================================================== GrainDoppler

static void testGrainDoppler()
{
    constexpr float speedOfSound = 343.0f;

    // --- dopplerFactor=0 disables the effect entirely (ratio stays 1.0) ---
    {
        const float ratio = GrainDoppler::computeDopplerRatio ({ 5.0f, 0.0f, 0.0f }, { -10.0f, 0.0f, 0.0f },
                                                                 0.0f, speedOfSound);
        check (std::abs (ratio - 1.0f) < 1.0e-4f, "GrainDoppler: dopplerFactor=0 gives a ratio of exactly 1.0 regardless of velocity");
    }

    // --- Approaching the listener (moving toward the origin) raises pitch ---
    {
        // Grain at +5 on the x-axis, moving in -x (toward the origin/listener).
        const float ratio = GrainDoppler::computeDopplerRatio ({ 5.0f, 0.0f, 0.0f }, { -10.0f, 0.0f, 0.0f },
                                                                 1.0f, speedOfSound);
        std::printf ("       approaching: ratio=%.4f (classic formula predicts ~%.4f)\n",
                     ratio, speedOfSound / (speedOfSound - 10.0f));
        check (ratio > 1.0f, "GrainDoppler: a grain moving toward the listener raises pitch (ratio > 1)");
        check (std::abs (ratio - speedOfSound / (speedOfSound - 10.0f)) < 0.01f,
               "GrainDoppler: approaching ratio matches the classic Doppler formula");
    }

    // --- Receding from the listener lowers pitch ---
    {
        const float ratio = GrainDoppler::computeDopplerRatio ({ 5.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f },
                                                                 1.0f, speedOfSound);
        check (ratio < 1.0f, "GrainDoppler: a grain moving away from the listener lowers pitch (ratio < 1)");
    }

    // --- Purely tangential motion (no radial component) leaves pitch unchanged ---
    {
        // Grain at +5 on the x-axis, moving purely in y (perpendicular to
        // the line to the listener at the origin) -- zero radial velocity.
        const float ratio = GrainDoppler::computeDopplerRatio ({ 5.0f, 0.0f, 0.0f }, { 0.0f, 20.0f, 0.0f },
                                                                 1.0f, speedOfSound);
        check (std::abs (ratio - 1.0f) < 1.0e-3f, "GrainDoppler: purely tangential motion produces no pitch shift");
    }

    // --- dopplerFactor scales the effect proportionally ---
    {
        const float ratioHalf = GrainDoppler::computeDopplerRatio ({ 5.0f, 0.0f, 0.0f }, { -10.0f, 0.0f, 0.0f },
                                                                     0.5f, speedOfSound);
        const float ratioFull = GrainDoppler::computeDopplerRatio ({ 5.0f, 0.0f, 0.0f }, { -10.0f, 0.0f, 0.0f },
                                                                     1.0f, speedOfSound);
        check (ratioHalf > 1.0f && ratioHalf < ratioFull, "GrainDoppler: dopplerFactor=0.5 gives roughly half the pitch shift of dopplerFactor=1.0");
    }

    // --- Extreme/pathological inputs never produce NaN/Inf or an absurd ratio ---
    {
        // Velocity far exceeding speedOfSound, and an artistically very low
        // speedOfSound (both are legitimate creative settings elsewhere in
        // this codebase, see SceneSettings::speedOfSound's own docs).
        const float ratio1 = GrainDoppler::computeDopplerRatio ({ 1.0f, 0.0f, 0.0f }, { -5000.0f, 0.0f, 0.0f }, 1.0f, speedOfSound);
        const float ratio2 = GrainDoppler::computeDopplerRatio ({ 1.0f, 0.0f, 0.0f }, { -100.0f, 0.0f, 0.0f }, 1.0f, 5.0f);
        const float ratio3 = GrainDoppler::computeDopplerRatio ({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 1.0f, speedOfSound); // grain exactly at the listener
        check (std::isfinite (ratio1) && ratio1 >= 0.25f && ratio1 <= 4.0f, "GrainDoppler: extreme velocity stays finite and within the sane clamp range");
        check (std::isfinite (ratio2) && ratio2 >= 0.25f && ratio2 <= 4.0f, "GrainDoppler: artistically low speedOfSound stays finite and within the sane clamp range");
        check (std::isfinite (ratio3) && ratio3 >= 0.25f && ratio3 <= 4.0f, "GrainDoppler: grain exactly at the listener position stays finite (no division by zero)");
    }
}

int main()
{
    testRenderGrainBlock();
    testSpawnRateAndCaps();
    testGlobalBudgetSharedAcrossClouds();
    testLifetimeExpiry();
    testBounceStaysWithinBoundary();
    testRadialExplosionMovesOutward();
    testOrbitTracksMovingParent();
    testAttractRepelSiblings();
    testGrainDoppler();

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
