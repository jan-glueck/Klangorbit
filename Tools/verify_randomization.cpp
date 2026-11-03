#include <cmath>
#include <cstdio>
#include <vector>
#include <juce_core/juce_core.h>
#include "../Source/RandomEngine.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }
}

int main()
{
    // --- SeededRandom: reproducibility is the whole point -----------------
    {
        SeededRandom a (1234);
        SeededRandom b (1234);
        bool identical = true;
        for (int i = 0; i < 100; ++i)
            if (a.nextFloat01() != b.nextFloat01())
                identical = false;
        check (identical, "SeededRandom: the same seed produces an identical sequence, call for call");
    }
    {
        SeededRandom a (1);
        SeededRandom b (2);
        bool anyDifferent = false;
        for (int i = 0; i < 20; ++i)
            if (a.nextFloat01() != b.nextFloat01())
                anyDifferent = true;
        check (anyDifferent, "SeededRandom: different seeds produce different sequences");
    }
    {
        SeededRandom rng (5);
        rng.nextFloat01(); rng.nextFloat01(); rng.nextFloat01(); // advance the stream
        rng.setSeed (5);
        SeededRandom fresh (5);
        check (rng.nextFloat01() == fresh.nextFloat01(),
               "SeededRandom::setSeed() resets the stream exactly like a fresh instance with the same seed");
    }
    {
        // SeededRandom's default constructor deliberately matches what
        // juce::Random's OWN default constructor actually does (see
        // RandomEngine.h's own comment on this -- its member initializer
        // list sets seed(1), but the constructor BODY then calls
        // setSeedRandomly(), which is the real, unpredictable behavior):
        // two freshly default-constructed instances should differ, not
        // share one implicit default stream.
        SeededRandom a;
        SeededRandom b;
        check (a.getSeed() != b.getSeed(),
               "two default-constructed SeededRandoms get different seeds (matches juce::Random's own default ctor, not a hidden fixed default)");
    }
    {
        SeededRandom rng (7);
        const auto before = rng.getSeed();
        rng.reseedRandomly();
        // Can't assert the new seed differs from `before` with certainty
        // (astronomically unlikely collision aside) -- what matters is
        // that getSeed() always reports whatever setSeed()/reseedRandomly()
        // actually applied, so a caller can persist it.
        SeededRandom reapplied (rng.getSeed());
        check (rng.nextFloat01() == reapplied.nextFloat01(),
               "SeededRandom::reseedRandomly()'s new seed, read back via getSeed(), reproduces the same stream");
        juce::ignoreUnused (before);
    }
    {
        SeededRandom rng (42);
        bool everOutOfRange = false;
        for (int i = 0; i < 1000; ++i)
        {
            const float u = rng.nextFloat01();
            if (u < 0.0f || u >= 1.0f) everOutOfRange = true;
        }
        check (! everOutOfRange, "SeededRandom::nextFloat01() always stays within [0, 1)");
    }
    {
        SeededRandom rng (43);
        bool everOutOfRange = false;
        for (int i = 0; i < 1000; ++i)
        {
            const float b = rng.nextBipolar();
            if (b < -1.0f || b >= 1.0f) everOutOfRange = true;
        }
        check (! everOutOfRange, "SeededRandom::nextBipolar() always stays within [-1, 1)");
    }
    {
        SeededRandom rng (44);
        bool everOutOfRange = false;
        for (int i = 0; i < 1000; ++i)
        {
            const int n = rng.nextInt (5);
            if (n < 0 || n >= 5) everOutOfRange = true;
        }
        check (! everOutOfRange, "SeededRandom::nextInt(5) always stays within [0, 5)");
    }

    // --- RandomizedParam: each distribution shape ---------------------------
    {
        RandomizedParam p;
        p.base = 10.0f;
        p.spread = 2.0f;
        p.distribution = RandomDistribution::Uniform;

        SeededRandom rng (100);
        bool everOutOfRange = false;
        for (int i = 0; i < 2000; ++i)
        {
            const float v = p.sample (rng);
            if (v < 8.0f - 1.0e-4f || v > 12.0f + 1.0e-4f) everOutOfRange = true;
        }
        check (! everOutOfRange, "RandomizedParam::sample(): Uniform always stays within [base-spread, base+spread]");
    }
    {
        // Normal has unbounded tails by design (see RandomDistribution's
        // own comment) -- check its mean/stddev land close to what was
        // asked for instead of a hard range.
        RandomizedParam p;
        p.base = 5.0f;
        p.spread = 1.0f; // standard deviation
        p.distribution = RandomDistribution::Normal;

        SeededRandom rng (101);
        constexpr int n = 20000;
        double sum = 0.0, sumSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float v = p.sample (rng);
            sum += v;
            sumSq += (double) v * v;
        }
        const double mean = sum / n;
        const double variance = sumSq / n - mean * mean;
        check (std::abs (mean - 5.0) < 0.05, "RandomizedParam::sample(): Normal's empirical mean matches base within tolerance");
        check (std::abs (std::sqrt (variance) - 1.0) < 0.05, "RandomizedParam::sample(): Normal's empirical std-dev matches spread within tolerance");
    }
    {
        RandomizedParam p;
        p.base = 0.0f;
        p.spread = 1.0f;

        SeededRandom rngUp (102);
        p.distribution = RandomDistribution::Ramp;
        p.rampTowardMax = true;
        double sumUp = 0.0;
        constexpr int n = 5000;
        for (int i = 0; i < n; ++i) sumUp += p.sample (rngUp);

        SeededRandom rngDown (102);
        p.rampTowardMax = false;
        double sumDown = 0.0;
        for (int i = 0; i < n; ++i) sumDown += p.sample (rngDown);

        check (sumUp / n > 0.1, "RandomizedParam::sample(): Ramp(rampTowardMax=true) biases the mean toward base+spread");
        check (sumDown / n < -0.1, "RandomizedParam::sample(): Ramp(rampTowardMax=false) biases the mean toward base-spread");
    }
    {
        // LogUniform: base is a frequency-like positive center, spread is a
        // RATIO (see RandomizedParam's own comment) -- result must stay
        // within [base/spread, base*spread], and the geometric mean over
        // many samples should land close to base (log-uniform is symmetric
        // in log space).
        RandomizedParam p;
        p.base = 1000.0f;
        p.spread = 2.0f; // one octave either way
        p.distribution = RandomDistribution::LogUniform;

        SeededRandom rng (103);
        constexpr int n = 5000;
        bool everOutOfRange = false;
        double sumLog = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float v = p.sample (rng);
            if (v < 500.0f - 0.5f || v > 2000.0f + 0.5f) everOutOfRange = true;
            sumLog += std::log ((double) v);
        }
        const double geometricMean = std::exp (sumLog / n);
        check (! everOutOfRange, "RandomizedParam::sample(): LogUniform always stays within [base/spread, base*spread]");
        check (std::abs (geometricMean - 1000.0) < 50.0, "RandomizedParam::sample(): LogUniform's geometric mean lands close to base");
    }
    {
        RandomizedParam p;
        p.base = 0.0f;
        p.spread = 1.0f;
        p.distribution = RandomDistribution::PowerLaw;

        SeededRandom rngHigh (104);
        p.powerExponent = 3.0f; // biases toward base-spread (the low end)
        double sumHigh = 0.0;
        constexpr int n = 5000;
        for (int i = 0; i < n; ++i) sumHigh += p.sample (rngHigh);

        SeededRandom rngLow (104);
        p.powerExponent = 0.33f; // biases toward base+spread (the high end)
        double sumLow = 0.0;
        for (int i = 0; i < n; ++i) sumLow += p.sample (rngLow);

        check (sumHigh / n < sumLow / n,
               "RandomizedParam::sample(): PowerLaw's exponent controls which end the distribution leans toward");
    }
    {
        RandomizedParam p;
        p.distribution = RandomDistribution::Discrete;
        p.discreteValues = { 1.0f, 2.0f, 5.0f };

        SeededRandom rng (105);
        bool allValid = true;
        bool sawMoreThanOne = false;
        float firstSeen = p.sample (rng);
        for (int i = 0; i < 200; ++i)
        {
            const float v = p.sample (rng);
            if (v != 1.0f && v != 2.0f && v != 5.0f) allValid = false;
            if (v != firstSeen) sawMoreThanOne = true;
        }
        check (allValid, "RandomizedParam::sample(): Discrete always returns one of discreteValues, nothing else");
        check (sawMoreThanOne, "RandomizedParam::sample(): Discrete actually varies across calls, not stuck on one value");
    }
    {
        // quantizeToSet is ORTHOGONAL to distribution (see RandomizedParam's
        // own comment) -- a continuous distribution's raw sample should
        // always snap onto one of quantizeValues exactly.
        RandomizedParam p;
        p.base = 0.0f;
        p.spread = 10.0f;
        p.distribution = RandomDistribution::Uniform;
        p.quantizeToSet = true;
        p.quantizeValues = { -5.0f, 0.0f, 3.0f, 7.5f };

        SeededRandom rng (106);
        bool allExact = true;
        for (int i = 0; i < 500; ++i)
        {
            const float v = p.sample (rng);
            bool matched = false;
            for (float q : p.quantizeValues)
                if (v == q) matched = true;
            if (! matched) allExact = false;
        }
        check (allExact, "RandomizedParam::sample(): quantizeToSet snaps every sample exactly onto a quantizeValues entry");
    }
    {
        // An empty quantizeValues with quantizeToSet=true is a defensive
        // no-op (see RandomizedParam's own comment), not a crash/always-0.
        RandomizedParam p;
        p.base = 3.0f;
        p.spread = 0.0f;
        p.distribution = RandomDistribution::Uniform;
        p.quantizeToSet = true; // quantizeValues left empty

        SeededRandom rng (107);
        check (p.sample (rng) == 3.0f, "RandomizedParam::sample(): quantizeToSet with an empty quantizeValues behaves as a no-op");
    }

    // --- MeanReverting::step() ----------------------------------------------
    {
        // Pure reversion (zero noise): value should move monotonically
        // toward baseline, same contract TrajectoryEngine's Orbit case
        // already relies on (see Tools/verify_orbit.cpp) -- this test
        // checks the free function directly, independent of TrajectoryEngine.
        float current = 10.0f;
        float smoothed = 0.0f;
        const float baseline = 2.0f;
        bool monotonic = true;
        float previousDistance = std::abs (current - baseline);
        for (int i = 0; i < 500; ++i)
        {
            current = MeanReverting::step (current, baseline, /*reversionRate*/ 1.0f, /*noiseAmplitude*/ 0.0f,
                                            /*noiseSmoothing*/ 0.0f, /*dt*/ 0.01f, /*gaussianSample*/ 0.0f, smoothed);
            const float distance = std::abs (current - baseline);
            if (distance > previousDistance + 1.0e-6f) monotonic = false;
            previousDistance = distance;
        }
        check (monotonic, "MeanReverting::step(): pure reversion (no noise) moves monotonically toward the baseline");
        check (previousDistance < 0.1f, "MeanReverting::step(): pure reversion actually reaches the baseline's neighborhood");
    }
    {
        // noiseSmoothing<=0 => smoothed should equal the raw gaussian sample
        // exactly every call (see MeanReverting::step()'s own comment on
        // reproducing the pre-smoothing-field behavior exactly).
        float current = 0.0f;
        float smoothed = 0.0f;
        current = MeanReverting::step (current, 0.0f, 0.0f, 1.0f, 0.0f, 0.1f, 0.7f, smoothed);
        check (smoothed == 0.7f, "MeanReverting::step(): noiseSmoothing<=0 leaves the smoothed sample identical to the raw input");
    }
    {
        // With substantial smoothing, a single call's smoothed output
        // should move only partway from its previous state toward a new
        // raw sample, not jump all the way there -- i.e. it's a real
        // low-pass, not a pass-through.
        float current = 0.0f;
        float smoothed = 1.0f; // pretend state was already settled at 1.0
        current = MeanReverting::step (current, 0.0f, 0.0f, 1.0f, /*noiseSmoothing*/ 5.0f, /*dt*/ 0.01f, /*gaussianSample*/ -1.0f, smoothed);
        check (smoothed < 1.0f && smoothed > -1.0f, "MeanReverting::step(): with noiseSmoothing>0, one step moves only partway toward a new raw sample");
    }

    std::printf ("\n%s (%d failures)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
