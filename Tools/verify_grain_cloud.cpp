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
        double readPosition = 0.0;
        renderGrainBlock (ring.data(), ringSize, readPosition, /*rate*/ 1.0f, grainLength,
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
        renderGrainBlock (ring.data(), ringSize, readPosition, 1.0f, grainLength, samplesPlayed, tail.data(), (int) tail.size());
        const bool allZero = std::all_of (tail.begin(), tail.end(), [] (float x) { return x == 0.0f; });
        check (allZero, "renderGrainBlock: silent after the envelope is exhausted");
    }

    // --- Ring buffer wrap: reading across the end must stay continuous ---
    {
        const int grainLength = 400;
        std::vector<float> out ((size_t) grainLength);
        int samplesPlayed = 0;
        // Start near the end of the buffer so playback wraps partway through.
        double readPosition = (double) (ringSize - 50);
        renderGrainBlock (ring.data(), ringSize, readPosition, 1.0f, grainLength, samplesPlayed, out.data(), grainLength);

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
        double pos1 = 0.0, pos2 = 0.0;
        renderGrainBlock (ring.data(), ringSize, pos1, 1.0f, grainLength, played1, outNormal.data(), grainLength);
        renderGrainBlock (ring.data(), ringSize, pos2, 2.0f, grainLength, played2, outFast.data(), grainLength);

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
            double readPosition = 0.0;
            renderGrainBlock (ring.data(), ringSize, readPosition, rate, shortGrainLength, samplesPlayed, out.data(), shortGrainLength);

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
            double readPosition = 0.0;
            renderGrainBlock (ring.data(), ringSize, readPosition, rate, shortGrainLength, samplesPlayed, out.data(), (int) out.size());
            const float jump = std::abs (out[(size_t) shortGrainLength] - out[(size_t) shortGrainLength - 1]);
            worstJump = juce::jmax (worstJump, jump);
        }
        std::printf ("       worst tail-to-silence jump across rates 0.1..2.0: %.5f\n", worstJump);
        check (worstJump < 0.02f, "renderGrainBlock: tail-to-silence transition has no audible discontinuity for any playbackRate 0.1..2.0");
    }

    // --- Doppler regression: playbackRate changing MID-GRAIN (block to
    // block, as GrainDoppler.h's per-block ratio does) must not jump the
    // read position -- reported bug ("Doppler... knackt"). With the OLD
    // "start + samplesPlayed*rate" formula, a changed rate on the very next
    // call recomputed the read position for the entire already-elapsed
    // samplesPlayed at the NEW rate, jumping the read pointer
    // discontinuously. The fix makes readPosition a caller-owned
    // accumulator (see GrainRenderer.h) that only changes SLOPE, not
    // position, when the rate changes -- verified directly here by
    // comparing the sample-to-sample jump right at the rate-change boundary
    // against jumps elsewhere in the same continuous render.
    {
        const int grainLength = 4000;
        std::vector<float> out ((size_t) grainLength);
        int samplesPlayed = 0;
        double readPosition = 0.0;

        // First half at rate 1.0, second half at rate 2.5 -- a large,
        // sudden change, larger than one block of real Doppler would ever
        // produce, to make any leftover discontinuity easy to detect.
        const int half = grainLength / 2;
        renderGrainBlock (ring.data(), ringSize, readPosition, 1.0f, grainLength, samplesPlayed, out.data(), half);
        renderGrainBlock (ring.data(), ringSize, readPosition, 2.5f, grainLength, samplesPlayed, out.data() + half, grainLength - half);

        check (allFinite (out), "renderGrainBlock: mid-grain rate change has no NaN/Inf");

        float jumpAtBoundary = std::abs (out[(size_t) half] - out[(size_t) (half - 1)]);
        float maxJumpElsewhere = 0.0f;
        for (int i = 1; i < grainLength; ++i)
        {
            if (i == half)
                continue;
            maxJumpElsewhere = juce::jmax (maxJumpElsewhere, std::abs (out[(size_t) i] - out[(size_t) (i - 1)]));
        }
        std::printf ("       mid-grain rate-change jump: %.4f (max jump elsewhere: %.4f)\n", jumpAtBoundary, maxJumpElsewhere);
        check (jumpAtBoundary < juce::jmax (0.05f, maxJumpElsewhere * 2.0f),
               "renderGrainBlock: playbackRate changing mid-grain (Doppler) produces no larger a jump than the rest of the signal");
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

// Regression for a specific report: "grain duration also affects spawn
// rate". At the scheduling level it never has (see
// GrainCloud::update()'s spawn-interval calculation, which only ever
// reads grainRate) -- but a long grainDuration CAN look that way if it
// pushes the number of simultaneously-alive grains up against
// maxConcurrentGrains, since that cap blocks new spawns until an old
// grain expires. This test uses a duration/rate/cap combination where
// the cap has headroom (grainRate * grainDuration well under
// maxConcurrentGrains), so it isolates the actual claim: a long-lived
// grain must not, by itself, suppress new grains from spawning on
// schedule.
static void testSpawnRateIndependentOfDuration()
{
    // Pool/cap sized to exactly the steady-state overlap (rate*duration =
    // 1*4 = 4), so every slot gets reused repeatedly once that steady
    // state is reached -- letting spawnGeneration prove spawning keeps
    // going at the configured rate well past the initial fill, not just
    // that the first few grains fit.
    GrainCloud cloud (/*poolSize*/ 4);
    cloud.getSettings().enabled = true;
    cloud.getSettings().grainRate = 1.0f;      // one new grain per second
    cloud.getSettings().grainDuration = 4.0f;  // each lives 4x longer than the spawn interval
    cloud.getSettings().maxConcurrentGrains = 4;

    juce::Random rng (2024);
    int budget = 16;
    cloud.setRingBufferContext (0, kSampleRate);

    int maxActive = 0;
    const double dt = 1.0 / 90.0;
    const int numTicks = (int) (4.5 * 90.0); // 4.5s: past the point steady-state overlap should be reached
    for (int tick = 0; tick < numTicks; ++tick)
    {
        cloud.update (dt, { 0.0f, 0.0f, 0.0f }, {}, budget, rng);
        maxActive = juce::jmax (maxActive, cloud.getNumActiveGrains());
    }

    std::printf ("       max simultaneously active grains (rate=1Hz, duration=4s): %d (expected ~4)\n", maxActive);
    check (maxActive >= 3, "GrainCloud: a long grainDuration does not suppress overlapping spawns when maxConcurrentGrains has headroom (rate*duration ~ 4 grains overlap as expected)");

    // Keep running well past the first 4s so every one of the 4 slots must
    // be reused at least once (~8.5s total at 1 spawn/sec is ~8 spawns
    // across 4 slots) -- proving the spawn rate is sustained over time,
    // not just achieved once during the initial fill.
    const int moreTicks = (int) (4.0 * 90.0);
    for (int tick = 0; tick < moreTicks; ++tick)
        cloud.update (dt, { 0.0f, 0.0f, 0.0f }, {}, budget, rng);

    std::vector<GrainCloud::Snapshot> snap;
    cloud.getSnapshot (snap);
    int maxGeneration = 0;
    for (auto& s : snap) maxGeneration = juce::jmax (maxGeneration, s.spawnGeneration);
    std::printf ("       max per-slot spawnGeneration after 8.5s at grainRate=1 (4 slots): %d (expected >= 2)\n", maxGeneration);
    check (maxGeneration >= 2, "GrainCloud: spawn cadence keeps matching grainRate well past the initial fill -- a long grainDuration does not stall it over time");
}

// Deliberately oversubscribed (unlike testSpawnRateIndependentOfDuration
// above, which stayed within maxConcurrentGrains): grainRate*grainDuration
// (5*2=10) exceeds maxConcurrentGrains (4) by more than 2x -- exactly the
// "high rate AND high duration" scenario reported as causing audible
// spawn stalls before voice stealing existed. Counts actual spawn EVENTS
// over time (via each pool slot's spawnGeneration, which climbs every
// time that slot is reused -- by a fresh spawn or a steal) rather than
// peak concurrent count, since the whole point is that new grains keep
// starting continuously, not that more of them fit at once.
static void testVoiceStealingKeepsSpawningContinuous()
{
    GrainCloud cloud (16);
    cloud.getSettings().enabled = true;
    cloud.getSettings().grainRate = 5.0f;
    cloud.getSettings().grainDuration = 2.0f;
    cloud.getSettings().maxConcurrentGrains = 4;

    juce::Random rng (77);
    int budget = 128;
    cloud.setRingBufferContext (0, kSampleRate);

    std::vector<int> lastGenPerSlot (16, 0);
    int totalSpawnsObserved = 0;

    const double dt = 1.0 / 90.0;
    const int numTicks = (int) (4.0 * 90.0); // 4s -> ~20 spawns expected at rate=5
    for (int tick = 0; tick < numTicks; ++tick)
    {
        cloud.update (dt, { 0.0f, 0.0f, 0.0f }, {}, budget, rng);

        std::vector<GrainCloud::Snapshot> snap;
        cloud.getSnapshot (snap);
        for (size_t i = 0; i < snap.size(); ++i)
        {
            if (snap[i].spawnGeneration != lastGenPerSlot[i])
            {
                totalSpawnsObserved += (snap[i].spawnGeneration - lastGenPerSlot[i]);
                lastGenPerSlot[i] = snap[i].spawnGeneration;
            }
        }
    }

    std::printf ("       total spawn events over 4s at rate=5Hz/duration=2s/cap=4 (needs 10 concurrent): %d (expected ~20)\n", totalSpawnsObserved);
    check (totalSpawnsObserved >= 16, "GrainCloud: voice stealing keeps new grains spawning near the configured rate even when rate*duration exceeds maxConcurrentGrains");
}

// Same heavy oversubscription, checking the OTHER half of the trade-off:
// stolen grains must end up genuinely shorter (proving stealing actually
// happened) while never producing a corrupt/invalid grainLengthSamples --
// no new envelope math is involved (see beginVoiceSteal()'s own comment),
// so this only needs to check the length value stays sane, not re-verify
// click-freeness (already covered by testRenderGrainBlock's exhaustive
// per-playbackRate envelope-reaches-zero checks, which apply identically
// regardless of how a grain's length was decided).
static void testVoiceStealingShortensNotCorrupts()
{
    GrainCloud cloud (2);
    cloud.getSettings().enabled = true;
    cloud.getSettings().grainRate = 10.0f;
    cloud.getSettings().grainDuration = 5.0f; // very long relative to the 2-slot cap
    cloud.getSettings().maxConcurrentGrains = 2;

    juce::Random rng (13);
    int budget = 128;
    cloud.setRingBufferContext (0, kSampleRate);

    bool sawShortenedGrain = false;
    bool allLengthsSane = true;

    const double dt = 1.0 / 90.0;
    for (int tick = 0; tick < (int) (2.0 * 90.0); ++tick)
    {
        cloud.update (dt, { 0.0f, 0.0f, 0.0f }, {}, budget, rng);

        std::vector<GrainCloud::Snapshot> snap;
        cloud.getSnapshot (snap);
        for (auto& s : snap)
        {
            if (! s.active) continue;
            if (s.grainLengthSamples <= 0 || s.grainLengthSamples > (int) (5.5 * kSampleRate))
                allLengthsSane = false;
            if (s.grainLengthSamples < (int) (4.9 * kSampleRate)) // meaningfully shorter than the configured 5s
                sawShortenedGrain = true;
        }
    }

    check (allLengthsSane, "GrainCloud: voice-stolen grains always keep a positive, bounded grainLengthSamples (no corruption)");
    check (sawShortenedGrain, "GrainCloud: heavy oversubscription actually triggers voice stealing (some grains end up shorter than the configured Duration)");
}

static void testGrainRateJitterVariesInterval()
{
    auto collectSpawnTicks = [] (float rateJitter, int seed) -> std::vector<int>
    {
        GrainCloud cloud (32);
        cloud.getSettings().enabled = true;
        cloud.getSettings().grainRate = 10.0f;
        cloud.getSettings().grainDuration = 0.05f; // short -- slots free up fast, no voice stealing needed here
        cloud.getSettings().maxConcurrentGrains = 32;
        cloud.getSettings().grainRateJitter = rateJitter;

        juce::Random rng (seed);
        int budget = 128;
        cloud.setRingBufferContext (0, kSampleRate);

        std::vector<int> lastGen (32, 0);
        std::vector<int> spawnTicks;
        const double dt = 1.0 / 500.0; // fine-grained, to resolve individual spawn timing precisely
        for (int tick = 0; tick < 2500; ++tick) // 5s
        {
            cloud.update (dt, {}, {}, budget, rng);
            std::vector<GrainCloud::Snapshot> snap;
            cloud.getSnapshot (snap);
            for (size_t i = 0; i < snap.size(); ++i)
            {
                if (snap[i].spawnGeneration != lastGen[i])
                {
                    lastGen[i] = snap[i].spawnGeneration;
                    spawnTicks.push_back (tick);
                }
            }
        }
        return spawnTicks;
    };

    auto stddevOfDiffs = [] (const std::vector<int>& ticks) -> double
    {
        if (ticks.size() < 3) return 0.0;
        std::vector<double> diffs;
        for (size_t i = 1; i < ticks.size(); ++i)
            diffs.push_back ((double) (ticks[i] - ticks[i - 1]));
        double mean = 0.0;
        for (double d : diffs) mean += d;
        mean /= (double) diffs.size();
        double var = 0.0;
        for (double d : diffs) var += (d - mean) * (d - mean);
        var /= (double) diffs.size();
        return std::sqrt (var);
    };

    const auto regular  = collectSpawnTicks (0.0f, 501);
    const auto jittered = collectSpawnTicks (0.5f, 502);

    const double regularStd  = stddevOfDiffs (regular);
    const double jitteredStd = stddevOfDiffs (jittered);

    std::printf ("       inter-spawn interval stddev (ticks): grainRateJitter=0 -> %.3f, grainRateJitter=0.5 -> %.3f\n", regularStd, jitteredStd);
    check (regularStd < 1.0, "GrainCloud: grainRateJitter=0 produces perfectly regular spawn intervals");
    check (jitteredStd > regularStd, "GrainCloud: grainRateJitter randomizes the spawn interval away from perfectly regular");
}

static void testGrainDurationJitterVariesLifetime()
{
    GrainCloud cloud (64);
    cloud.getSettings().enabled = true;
    cloud.getSettings().grainRate = 200.0f; // spawn plenty fast, want many distinct grains to sample
    cloud.getSettings().grainDuration = 1.0f;
    cloud.getSettings().grainDurationJitter = 0.5f;
    cloud.getSettings().maxConcurrentGrains = 64;

    juce::Random rng (901);
    int budget = 128;
    cloud.setRingBufferContext (0, kSampleRate);

    std::vector<int> lastGen (64, 0);
    std::vector<int> observedLengths;
    const double dt = 1.0 / 90.0;
    for (int tick = 0; tick < 90; ++tick) // ~1s, plenty of spawns at rate=200
    {
        cloud.update (dt, {}, {}, budget, rng);
        std::vector<GrainCloud::Snapshot> snap;
        cloud.getSnapshot (snap);
        for (size_t i = 0; i < snap.size(); ++i)
        {
            if (snap[i].active && snap[i].spawnGeneration != lastGen[i])
            {
                lastGen[i] = snap[i].spawnGeneration;
                observedLengths.push_back (snap[i].grainLengthSamples);
            }
        }
    }

    check (observedLengths.size() > 10, "GrainCloud: grainDurationJitter test setup actually spawned multiple grains");
    int minLen = observedLengths.front(), maxLen = observedLengths.front();
    for (int l : observedLengths) { minLen = juce::jmin (minLen, l); maxLen = juce::jmax (maxLen, l); }
    std::printf ("       grainLengthSamples range with grainDurationJitter=0.5 (base 1s @ %.0fHz): [%d, %d]\n", kSampleRate, minLen, maxLen);
    check (maxLen > minLen, "GrainCloud: grainDurationJitter produces varying grain lengths across spawns, not one fixed value");
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
    // Exactly matches the first update() call's dt below (0.02s), so that
    // call's backlog is exactly one spawn interval -- spawns exactly once
    // with zero backlog left over. A much higher rate (as this test used
    // before voice stealing existed) would queue many more spawns than
    // maxConcurrentGrains=1 allows within that same call, and the newly
    // spawned grain would immediately become the OLDEST active grain for
    // the next queued spawn to steal from -- correct voice-stealing
    // behavior for genuine oversubscription, but not what this test
    // means to exercise (RadialExplosion's outward movement over time).
    cloud.getSettings().grainRate = 50.0f;
    cloud.getSettings().grainDuration = 1.0f;
    cloud.getSettings().movementMode = GrainMovementMode::RadialExplosion;
    cloud.getSettings().initialSpeed = 3.0f;
    cloud.getSettings().acceleration = 0.0f;
    cloud.getSettings().maxConcurrentGrains = 1; // exactly one grain, so "early"/"later" track the same instance

    juce::Random rng (5);
    int budget = 32;
    const Vec3 parentPos { 0.0f, 0.0f, 0.0f };

    cloud.setRingBufferContext (0, kSampleRate);
    cloud.update (0.02, parentPos, {}, budget, rng); // spawns the one grain, no leftover backlog to steal it
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

static void testOrbitSphereSpread()
{
    // orbitSphereSpread=0 (default) must reproduce the original flat
    // x/y-plane orbit exactly -- z stays 0 for every grain, every tick.
    {
        GrainCloud cloud (8);
        cloud.getSettings().enabled = true;
        cloud.getSettings().grainRate = 50.0f;
        cloud.getSettings().grainDuration = 2.0f;
        cloud.getSettings().movementMode = GrainMovementMode::OrbitAroundParent;
        cloud.getSettings().orbitRadius = 1.0f;
        cloud.getSettings().orbitAngularSpeed = 1.0f;
        cloud.getSettings().orbitSphereSpread = 0.0f;
        cloud.getSettings().maxConcurrentGrains = 8;

        juce::Random rng (55);
        int budget = 64;
        cloud.setRingBufferContext (0, kSampleRate);

        bool allFlat = true;
        for (int tick = 0; tick < 90; ++tick)
        {
            cloud.update (1.0 / 90.0, { 0.0f, 0.0f, 0.0f }, {}, budget, rng);
            std::vector<GrainCloud::Snapshot> snap;
            cloud.getSnapshot (snap);
            for (auto& s : snap)
                if (s.active && std::abs (s.position.z) > 1.0e-4f)
                    allFlat = false;
        }
        check (allFlat, "GrainCloud: orbitSphereSpread=0 reproduces the original flat x/y-plane orbit exactly (z stays 0)");
    }

    // orbitSphereSpread=1 -- grains should show real out-of-plane (z)
    // movement across many spawns, while the orbit RADIUS itself (just
    // the plane's orientation is randomized, not the size) stays intact.
    {
        GrainCloud cloud (16);
        cloud.getSettings().enabled = true;
        cloud.getSettings().grainRate = 50.0f;
        cloud.getSettings().grainDuration = 2.0f;
        cloud.getSettings().movementMode = GrainMovementMode::OrbitAroundParent;
        cloud.getSettings().orbitRadius = 1.0f;
        cloud.getSettings().orbitAngularSpeed = 1.0f;
        cloud.getSettings().orbitSphereSpread = 1.0f;
        cloud.getSettings().maxConcurrentGrains = 16;

        juce::Random rng (56);
        int budget = 128;
        cloud.setRingBufferContext (0, kSampleRate);

        float maxAbsZ = 0.0f;
        for (int tick = 0; tick < 90; ++tick)
        {
            cloud.update (1.0 / 90.0, { 0.0f, 0.0f, 0.0f }, {}, budget, rng);
            std::vector<GrainCloud::Snapshot> snap;
            cloud.getSnapshot (snap);
            for (auto& s : snap)
                if (s.active)
                    maxAbsZ = juce::jmax (maxAbsZ, std::abs (s.position.z));
        }
        std::printf ("       max |z| observed with orbitSphereSpread=1.0 (orbitRadius=1.0): %.3f\n", maxAbsZ);
        check (maxAbsZ > 0.2f, "GrainCloud: orbitSphereSpread=1.0 produces grains with real out-of-plane (z) movement");

        std::vector<GrainCloud::Snapshot> finalSnap;
        cloud.getSnapshot (finalSnap);
        bool allNearRadius = true;
        for (auto& s : finalSnap)
            if (s.active && std::abs (s.position.length() - 1.0f) > 0.05f)
                allNearRadius = false;
        check (allNearRadius, "GrainCloud: orbitSphereSpread changes the orbit plane's orientation only, not its radius");
    }
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

// ============================================================== grainReadDepthRange

namespace
{
    // Spawns a burst of grains under the given settings and returns each
    // one's observed read depth in seconds (writeHead - bufferReadStartSample,
    // converted via kSampleRate) -- i.e. how far back into the ring buffer's
    // history that grain's start point was drawn from.
    std::vector<float> collectReadDepths (const GrainCloudSettings& settingsToUse, int numGrains, int rngSeed)
    {
        GrainCloud cloud (numGrains);
        cloud.getSettings() = settingsToUse;
        cloud.getSettings().enabled = true;
        cloud.getSettings().grainRate = 1000.0f; // near-immediate spawns
        cloud.getSettings().grainDuration = 5.0f; // long-lived, don't expire mid-burst
        cloud.getSettings().maxConcurrentGrains = numGrains;

        juce::Random rng (rngSeed);
        int budget = numGrains;
        constexpr int writeHead = 500000; // arbitrary, comfortably larger than any tested depth in samples

        cloud.setRingBufferContext (writeHead, kSampleRate);
        for (int i = 0; i < numGrains; ++i)
            cloud.update (0.001, {}, {}, budget, rng); // one small tick per spawn, avoids exhausting the interval in one call

        std::vector<GrainCloud::Snapshot> snap;
        cloud.getSnapshot (snap);

        std::vector<float> depths;
        for (auto& s : snap)
            if (s.active)
                depths.push_back ((float) (writeHead - s.bufferReadStartSample) / (float) kSampleRate);
        return depths;
    }
}

static void testReadDepthRangeDisabledByDefault()
{
    GrainCloudSettings settings;
    settings.positionJitterInBuffer = 0.05f;
    // grainReadDepthRangeMin/Max left at their 0.0f defaults -- disabled.

    const auto depths = collectReadDepths (settings, 20, 101);
    check (! depths.empty(), "GrainCloud: read-depth test setup actually spawned grains (default settings)");

    float maxDepth = 0.0f;
    for (float d : depths) maxDepth = juce::jmax (maxDepth, d);
    std::printf ("       max observed read depth with depth-range disabled: %.4fs (positionJitterInBuffer=0.05)\n", maxDepth);
    check (maxDepth <= 0.05f + 0.001f, "GrainCloud: grainReadDepthRange at its default (0,0) is a no-op, behaves exactly like before this field existed");
}

static void testReadDepthRangeIsRespected()
{
    GrainCloudSettings settings;
    settings.positionJitterInBuffer = 0.0f; // isolate the depth-range effect
    settings.grainReadDepthRangeMin = 1.0f;
    settings.grainReadDepthRangeMax = 2.0f;
    settings.grainReadDepthDistribution = GrainReadDepthDistribution::Uniform;

    const auto depths = collectReadDepths (settings, 40, 202);
    check (! depths.empty(), "GrainCloud: read-depth test setup actually spawned grains (range test)");

    bool allWithinRange = true;
    for (float d : depths)
        if (d < 1.0f - 0.001f || d > 2.0f + 0.001f)
            allWithinRange = false;

    std::printf ("       %d grains spawned, read depths within [1.0, 2.0]s: %s\n",
                 (int) depths.size(), allWithinRange ? "yes" : "no");
    check (allWithinRange, "GrainCloud: grainReadDepthRangeMin/Max bounds every spawned grain's read depth");
}

static void testReadDepthDistributionBias()
{
    GrainCloudSettings baseSettings;
    baseSettings.positionJitterInBuffer = 0.0f;
    baseSettings.grainReadDepthRangeMin = 0.0f;
    baseSettings.grainReadDepthRangeMax = 4.0f;

    auto averageDepth = [&] (GrainReadDepthDistribution distribution, int seed) -> float
    {
        auto settings = baseSettings;
        settings.grainReadDepthDistribution = distribution;
        const auto depths = collectReadDepths (settings, 300, seed);
        float total = 0.0f;
        for (float d : depths) total += d;
        return depths.empty() ? -1.0f : total / (float) depths.size();
    };

    const float avgRecent  = averageDepth (GrainReadDepthDistribution::WeightedTowardRecent, 303);
    const float avgUniform = averageDepth (GrainReadDepthDistribution::Uniform, 304);
    const float avgOld     = averageDepth (GrainReadDepthDistribution::WeightedTowardOld, 305);

    std::printf ("       average read depth over 300 grains (range [0,4]s): recent=%.3f uniform=%.3f old=%.3f\n",
                 avgRecent, avgUniform, avgOld);
    check (avgRecent < avgUniform, "GrainCloud: WeightedTowardRecent biases the average read depth below Uniform's");
    check (avgOld > avgUniform, "GrainCloud: WeightedTowardOld biases the average read depth above Uniform's");
}

static void testPitchJitterScaleQuantizes()
{
    // Spawns many grains with PitchJitterMode::Scale and collects each
    // one's resulting playbackRate, converted back to a semitone offset
    // (12 * log2(rate)) -- every offset must land (within floating-point
    // tolerance) on an actual degree of the chosen scale, unlike Random
    // mode which can land anywhere in its continuous range.
    auto collectSemitoneOffsets = [] (PitchQuantizeScale scale, float pitchJitter, int seed) -> std::vector<float>
    {
        GrainCloud cloud (64);
        cloud.getSettings().enabled = true;
        cloud.getSettings().grainRate = 40.0f;
        cloud.getSettings().grainDuration = 0.02f; // short -- lots of independent spawns in a short test
        cloud.getSettings().maxConcurrentGrains = 64;
        cloud.getSettings().pitchJitter = pitchJitter;
        cloud.getSettings().pitchJitterMode = PitchJitterMode::Scale;
        cloud.getSettings().pitchQuantizeScale = scale;

        juce::Random rng (seed);
        int budget = 256;
        cloud.setRingBufferContext (0, kSampleRate);

        std::vector<int> lastGen (64, 0);
        std::vector<float> offsets;
        const double dt = 1.0 / 200.0;
        for (int tick = 0; tick < 2000; ++tick) // 10s
        {
            cloud.update (dt, {}, {}, budget, rng);
            std::vector<GrainCloud::Snapshot> snap;
            cloud.getSnapshot (snap);
            for (size_t i = 0; i < snap.size(); ++i)
            {
                if (snap[i].spawnGeneration != lastGen[i])
                {
                    lastGen[i] = snap[i].spawnGeneration;
                    offsets.push_back (12.0f * std::log2 (snap[i].playbackRate));
                }
            }
        }
        return offsets;
    };

    // Major Triad {0,4,7}: every octave-extended candidate within one
    // octave (see maxScaleReachSemitones in GrainCloud.cpp) is one of
    // {-12,-8,-5,0,4,7,12} (root/triad tones plus their octave images).
    {
        const auto offsets = collectSemitoneOffsets (PitchQuantizeScale::MajorTriad, 1.0f, 601);
        check (offsets.size() > 20, "PitchJitterMode::Scale: Major Triad test spawned a usable number of grains");

        const float allowed[] = { -12.0f, -8.0f, -5.0f, 0.0f, 4.0f, 7.0f, 12.0f };
        bool allOnScale = true;
        bool sawNonZero = false;
        for (float off : offsets)
        {
            bool matched = false;
            for (float a : allowed)
                if (std::abs (off - a) < 0.01f) { matched = true; break; }
            if (! matched) { allOnScale = false; break; }
            if (std::abs (off) > 0.01f) sawNonZero = true;
        }
        std::printf ("       Major Triad, pitchJitter=1.0: %d grains spawned, all landed on a triad degree: %s\n",
                     (int) offsets.size(), allOnScale ? "yes" : "no");
        check (allOnScale, "PitchJitterMode::Scale: every grain's semitone offset is an exact Major Triad degree (root/major-3rd/5th, any octave within reach)");
        check (sawNonZero, "PitchJitterMode::Scale: pitchJitter=1.0 actually produces some non-root degrees, not just the root every time");
    }

    // pitchJitter == 0: must always be exactly the root (offset 0),
    // regardless of scale -- matches Random mode's own "no jitter" floor.
    {
        const auto offsets = collectSemitoneOffsets (PitchQuantizeScale::Acoustic, 0.0f, 602);
        bool allZero = true;
        for (float off : offsets)
            if (std::abs (off) > 0.001f) { allZero = false; break; }
        check (allZero, "PitchJitterMode::Scale: pitchJitter=0 always yields the root (offset 0), no scale degrees selected");
    }

    // Octaves {0}: with pitchJitter=1.0 (full one-octave reach), the only
    // possible offsets are -12, 0, or 12 -- confirms the "bare interval,
    // not a full scale" case works the same way as a multi-note scale.
    {
        const auto offsets = collectSemitoneOffsets (PitchQuantizeScale::Octaves, 1.0f, 603);
        bool allOctaves = true;
        for (float off : offsets)
            if (std::abs (off) > 0.01f && std::abs (std::abs (off) - 12.0f) > 0.01f) { allOctaves = false; break; }
        check (allOctaves, "PitchJitterMode::Scale: Octaves scale only ever produces offsets of -12, 0, or +12 semitones");
    }
}

int main()
{
    testRenderGrainBlock();
    testSpawnRateAndCaps();
    testSpawnRateIndependentOfDuration();
    testVoiceStealingKeepsSpawningContinuous();
    testVoiceStealingShortensNotCorrupts();
    testGrainRateJitterVariesInterval();
    testGrainDurationJitterVariesLifetime();
    testGlobalBudgetSharedAcrossClouds();
    testLifetimeExpiry();
    testBounceStaysWithinBoundary();
    testRadialExplosionMovesOutward();
    testOrbitTracksMovingParent();
    testOrbitSphereSpread();
    testAttractRepelSiblings();
    testGrainDoppler();
    testReadDepthRangeDisabledByDefault();
    testReadDepthRangeIsRespected();
    testReadDepthDistributionBias();
    testPitchJitterScaleQuantizes();

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
