#include <cmath>
#include <cstdio>
#include <vector>
#include <juce_core/juce_core.h>
#include "../Source/VoiceFilter.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    // Runs numSamples of a constant-amplitude sine at freqHz through a
    // freshly-started filter and returns the amplitude (peak abs value)
    // of its last quarter, i.e. after the filter has settled.
    float settledSineAmplitude (VoiceFilterType type, float cutoffHz, float resonance,
                                 double sampleRate, float freqHz, int numSamples)
    {
        VoiceFilterSettings settings;
        settings.type = type;
        settings.cutoffHz = { cutoffHz, 0.0f };
        settings.resonance = { resonance, 0.0f };

        SeededRandom rng (1);
        VoiceFilter filter;
        filter.prepare (sampleRate);
        filter.start (settings, rng);

        float peak = 0.0f;
        const int settleFrom = numSamples * 3 / 4;
        for (int i = 0; i < numSamples; ++i)
        {
            const float input = std::sin (juce::MathConstants<float>::twoPi * freqHz * (float) i / (float) sampleRate);
            const float output = filter.processSample (input);
            if (i >= settleFrom)
                peak = juce::jmax (peak, std::abs (output));
        }
        return peak;
    }

    // Feeds a sustained constant (DC) input and returns the output once
    // it has settled -- lets LowPass/HighPass/BandPass/Notch's known DC
    // behaviour be checked directly.
    float settledDcResponse (VoiceFilterType type, float cutoffHz, double sampleRate, int numSamples)
    {
        VoiceFilterSettings settings;
        settings.type = type;
        settings.cutoffHz = { cutoffHz, 0.0f };
        settings.resonance = { 0.707f, 0.0f };

        SeededRandom rng (1);
        VoiceFilter filter;
        filter.prepare (sampleRate);
        filter.start (settings, rng);

        float output = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            output = filter.processSample (1.0f);
        return output;
    }
}

int main()
{
    constexpr double sampleRate = 48000.0;

    // --- DC behaviour, one per mode -----------------------------------------
    {
        const float dc = settledDcResponse (VoiceFilterType::LowPass, 500.0f, sampleRate, 20000);
        check (dc > 0.95f, "LowPass: a sustained DC input settles close to its full input value (unity DC gain)");
    }
    {
        const float dc = settledDcResponse (VoiceFilterType::HighPass, 500.0f, sampleRate, 20000);
        check (std::abs (dc) < 0.05f, "HighPass: a sustained DC input settles close to zero (DC is blocked)");
    }
    {
        const float dc = settledDcResponse (VoiceFilterType::BandPass, 500.0f, sampleRate, 20000);
        check (std::abs (dc) < 0.05f, "BandPass: a sustained DC input settles close to zero (DC is outside the pass band)");
    }
    {
        const float dc = settledDcResponse (VoiceFilterType::Notch, 500.0f, sampleRate, 20000);
        check (dc > 0.95f, "Notch: a sustained DC input settles close to its full input value (DC is outside the rejected band)");
    }

    // --- LowPass attenuates well above its cutoff ---------------------------
    {
        const float lowFreqAmp = settledSineAmplitude (VoiceFilterType::LowPass, 1000.0f, 0.707f, sampleRate, 100.0f, 4000);
        const float highFreqAmp = settledSineAmplitude (VoiceFilterType::LowPass, 1000.0f, 0.707f, sampleRate, 10000.0f, 4000);
        check (lowFreqAmp > 0.8f, "LowPass: a sine well below cutoff passes through close to full amplitude");
        check (highFreqAmp < 0.2f, "LowPass: a sine well above cutoff is strongly attenuated");
        check (highFreqAmp < lowFreqAmp, "LowPass: attenuation increases with frequency above cutoff");
    }

    // --- HighPass attenuates well below its cutoff --------------------------
    {
        const float lowFreqAmp = settledSineAmplitude (VoiceFilterType::HighPass, 1000.0f, 0.707f, sampleRate, 100.0f, 4000);
        const float highFreqAmp = settledSineAmplitude (VoiceFilterType::HighPass, 1000.0f, 0.707f, sampleRate, 10000.0f, 4000);
        check (lowFreqAmp < 0.2f, "HighPass: a sine well below cutoff is strongly attenuated");
        check (highFreqAmp > 0.8f, "HighPass: a sine well above cutoff passes through close to full amplitude");
    }

    // --- Resonance raises the peak near cutoff -------------------------------
    {
        const float lowQAmp = settledSineAmplitude (VoiceFilterType::LowPass, 1000.0f, 0.707f, sampleRate, 1000.0f, 8000);
        const float highQAmp = settledSineAmplitude (VoiceFilterType::LowPass, 1000.0f, 10.0f, sampleRate, 1000.0f, 8000);
        check (highQAmp > lowQAmp, "LowPass: higher resonance (Q) produces a taller peak right at cutoff than lower resonance");
    }

    // --- RandomizedParam wiring: spread=0 always samples the base value ----
    {
        VoiceFilterSettings settings;
        settings.cutoffHz = { 1234.0f, 0.0f };
        settings.resonance = { 3.0f, 0.0f };
        SeededRandom rng (7);
        VoiceFilter filter;
        filter.prepare (sampleRate);
        filter.start (settings, rng);
        check (std::abs (filter.getCurrentCutoffHz() - 1234.0f) < 0.01f,
               "VoiceFilter::start() samples cutoffHz from settings via RandomizedParam (spread=0 reproduces the base value exactly)");
    }

    // --- Per-voice reproducibility: same seed -> same sampled cutoff -------
    {
        VoiceFilterSettings settings;
        settings.cutoffHz = { 1000.0f, 500.0f };
        settings.cutoffHz.distribution = RandomDistribution::Uniform;

        SeededRandom rngA (42);
        VoiceFilter filterA;
        filterA.prepare (sampleRate);
        filterA.start (settings, rngA);

        SeededRandom rngB (42);
        VoiceFilter filterB;
        filterB.prepare (sampleRate);
        filterB.start (settings, rngB);

        check (filterA.getCurrentCutoffHz() == filterB.getCurrentCutoffHz(),
               "Two voices started with the same seed sample an identical per-voice cutoff from the same RandomizedParam");
    }

    // --- Drift: disabled by default means a static cutoff across blocks ----
    {
        VoiceFilterSettings settings; // driftAmplitude defaults to 0.0f (disabled)
        settings.cutoffHz = { 800.0f, 0.0f };
        SeededRandom rng (3);
        VoiceFilter filter;
        filter.prepare (sampleRate);
        filter.start (settings, rng);
        const float before = filter.getCurrentCutoffHz();
        for (int block = 0; block < 50; ++block)
            filter.advanceDrift (settings, 0.01f, rng);
        check (filter.getCurrentCutoffHz() == before,
               "advanceDrift() is a no-op when driftAmplitude == 0 (disabled, the default)");
    }

    // --- Drift: enabled means the cutoff actually moves, but stays bounded -
    {
        VoiceFilterSettings settings;
        settings.cutoffHz = { 800.0f, 0.0f };
        settings.driftAmplitude = 400.0f;
        settings.driftReversionRate = 0.5f;
        settings.driftSmoothing = 0.1f;
        SeededRandom rng (9);
        VoiceFilter filter;
        filter.prepare (sampleRate);
        filter.start (settings, rng);
        const float before = filter.getCurrentCutoffHz();

        bool everMoved = false;
        bool everOutOfBounds = false;
        for (int block = 0; block < 2000; ++block)
        {
            filter.advanceDrift (settings, 0.01f, rng);
            const float now = filter.getCurrentCutoffHz();
            if (now != before)
                everMoved = true;
            if (now < VoiceFilterLimits::minCutoffHz || now > (float) sampleRate * VoiceFilterLimits::maxCutoffRatioOfSampleRate)
                everOutOfBounds = true;
        }
        check (everMoved, "advanceDrift() with driftAmplitude > 0 actually moves the cutoff over time");
        check (! everOutOfBounds, "Drifted cutoff never leaves VoiceFilterLimits' safe range");
    }

    // --- Stability across the whole parameter space -------------------------
    // Sweeps cutoff/resonance (including values a careless caller might pass
    // outside the documented range) and confirms processSample() never
    // produces NaN/Inf and stays bounded for a bounded input, for every mode.
    {
        bool anyBad = false;
        SeededRandom sweepRng (123);
        const VoiceFilterType types[] = { VoiceFilterType::LowPass, VoiceFilterType::HighPass,
                                           VoiceFilterType::BandPass, VoiceFilterType::Notch };
        for (VoiceFilterType type : types)
        {
            for (int trial = 0; trial < 200; ++trial)
            {
                const float cutoff = sweepRng.nextFloat01() * 30000.0f + 1.0f;   // intentionally includes out-of-range values
                const float q = sweepRng.nextFloat01() * 30.0f + 0.01f;          // intentionally includes out-of-range values

                VoiceFilterSettings settings;
                settings.type = type;
                settings.cutoffHz = { cutoff, 0.0f };
                settings.resonance = { q, 0.0f };

                SeededRandom rng (trial);
                VoiceFilter filter;
                filter.prepare (sampleRate);
                filter.start (settings, rng);

                for (int i = 0; i < 500; ++i)
                {
                    const float input = sweepRng.nextBipolar();
                    const float output = filter.processSample (input);
                    if (! std::isfinite (output) || std::abs (output) > 100.0f)
                        anyBad = true;
                }
            }
        }
        check (! anyBad, "processSample() stays finite and bounded across a wide sweep of cutoff/resonance/input, every filter mode");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
