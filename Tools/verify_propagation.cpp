#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>
#include <juce_core/juce_core.h>
#include "../Source/PropagationProcessor.h"

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlockSize = 512;

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

    // Rough frequency estimate via zero-crossing rate over the given range.
    float estimateFrequencyHz (const std::vector<float>& signal, int startIdx, int endIdx, double sampleRate)
    {
        int crossings = 0;
        for (int i = startIdx + 1; i < endIdx; ++i)
            if ((signal[(size_t) (i - 1)] < 0.0f) != (signal[(size_t) i] < 0.0f))
                ++crossings;

        const double windowSeconds = (double) (endIdx - startIdx) / sampleRate;
        return (float) ((crossings / 2.0) / windowSeconds);
    }

    // Runs `numSamples` of a sine at f0 through the processor with a
    // caller-supplied position-over-time function, returns the full output.
    std::vector<float> runSine (PropagationProcessor& proc, const SoundObject& obj, const SceneSettings& scene,
                                 float f0, int numSamples, const std::function<Vec3 (double timeSeconds)>& positionFn)
    {
        std::vector<float> input ((size_t) numSamples), output ((size_t) numSamples);
        for (int i = 0; i < numSamples; ++i)
            input[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * f0 * (double) i / kSampleRate);

        proc.reset();
        for (int start = 0; start < numSamples; start += kBlockSize)
        {
            const int n = std::min (kBlockSize, numSamples - start);
            const double blockTimeSeconds = (double) start / kSampleRate;
            float directivityGain = 0.0f;
            Vec3 pos = positionFn (blockTimeSeconds);
            proc.process (input.data() + start, output.data() + start, n, pos, obj, scene, directivityGain);
        }
        return output;
    }
}

int main()
{
    SceneSettings scene; // defaults: speedOfSound=343, no wind, 50% humidity, 20C
    SoundObject obj;      // defaults: dopplerFactor=1, dopplerSmoothing=0.05, Omni

    // --- Test 1: propagation delay (latency) for a static source -----------
    {
        PropagationProcessor proc;
        proc.prepare (kSampleRate, kBlockSize);
        proc.reset();

        const float distance = 10.0f;
        const int numSamples = 8192;
        std::vector<float> input ((size_t) numSamples, 0.0f);
        input[0] = 1.0f; // single impulse
        std::vector<float> output ((size_t) numSamples, 0.0f);

        for (int start = 0; start < numSamples; start += kBlockSize)
        {
            const int n = std::min (kBlockSize, numSamples - start);
            float directivityGain = 0.0f;
            proc.process (input.data() + start, output.data() + start, n, { distance, 0.0f, 0.0f }, obj, scene, directivityGain);
        }

        check (allFinite (output), "latency test: output has no NaN/Inf");

        const int peakIdx = (int) (std::max_element (output.begin(), output.end(),
                                    [] (float a, float b) { return std::abs (a) < std::abs (b); }) - output.begin());
        const float expectedDelaySamples = distance / scene.speedOfSound * (float) kSampleRate;
        const float toleranceSamples = 8.0f; // interpolation + one-pole filter peak rounding

        std::printf ("       expected delay ~%.1f samples (%.1f ms), measured peak at %d\n",
                      expectedDelaySamples, expectedDelaySamples / (float) kSampleRate * 1000.0f, peakIdx);
        check (std::abs ((float) peakIdx - expectedDelaySamples) <= toleranceSamples,
               "latency test: measured peak matches distance/speedOfSound within tolerance");
    }

    // --- Test 2: Doppler direction (approaching = higher pitch, receding = lower) ---
    {
        PropagationProcessor procApproach, procRecede;
        procApproach.prepare (kSampleRate, kBlockSize);
        procRecede.prepare (kSampleRate, kBlockSize);

        const float f0 = 1000.0f;
        const float velocity = 30.0f; // m/s, well audible relative to c=343
        const int numSamples = (int) (2.0 * kSampleRate);

        auto approachPos = [&] (double t) -> Vec3 { return { 60.0f - (float) (velocity * t), 0.0f, 0.0f }; };
        auto recedePos    = [&] (double t) -> Vec3 { return { 5.0f  + (float) (velocity * t), 0.0f, 0.0f }; };

        auto outApproach = runSine (procApproach, obj, scene, f0, numSamples, approachPos);
        auto outRecede    = runSine (procRecede,   obj, scene, f0, numSamples, recedePos);

        check (allFinite (outApproach) && allFinite (outRecede), "doppler test: output has no NaN/Inf");

        // Measure in a stable middle window, well clear of the initial silence
        // while the delay line fills and the drift correction settles.
        const int winStart = numSamples / 2;
        const int winEnd = numSamples / 2 + (int) kSampleRate / 2;

        const float freqApproach = estimateFrequencyHz (outApproach, winStart, winEnd, kSampleRate);
        const float freqRecede    = estimateFrequencyHz (outRecede,   winStart, winEnd, kSampleRate);

        std::printf ("       f0=%.0f Hz, approaching measured ~%.1f Hz, receding measured ~%.1f Hz\n",
                      f0, freqApproach, freqRecede);

        const float expectedApproach = f0 * scene.speedOfSound / (scene.speedOfSound - velocity);
        const float expectedRecede    = f0 * scene.speedOfSound / (scene.speedOfSound + velocity);
        std::printf ("       (classic Doppler formula predicts ~%.1f Hz / ~%.1f Hz)\n", expectedApproach, expectedRecede);

        check (freqApproach > f0 * 1.02f, "doppler test: approaching source measures higher pitch");
        check (freqRecede < f0 * 0.98f, "doppler test: receding source measures lower pitch");
        check (std::abs (freqApproach - expectedApproach) < f0 * 0.1f, "doppler test: approaching pitch within 10% of formula prediction");
        check (std::abs (freqRecede - expectedRecede) < f0 * 0.1f, "doppler test: receding pitch within 10% of formula prediction");
    }

    // --- Test 3: dopplerFactor = 0 suppresses the pitch shift ---------------
    {
        PropagationProcessor proc;
        proc.prepare (kSampleRate, kBlockSize);

        SoundObject noDoppler = obj;
        noDoppler.dopplerFactor = 0.0f;

        const float f0 = 1000.0f;
        const float velocity = 30.0f;
        const int numSamples = (int) (2.0 * kSampleRate);
        auto approachPos = [&] (double t) -> Vec3 { return { 60.0f - (float) (velocity * t), 0.0f, 0.0f }; };

        auto out = runSine (proc, noDoppler, scene, f0, numSamples, approachPos);
        check (allFinite (out), "dopplerFactor=0 test: output has no NaN/Inf");

        const int winStart = numSamples / 2;
        const int winEnd = numSamples / 2 + (int) kSampleRate / 2;
        const float freq = estimateFrequencyHz (out, winStart, winEnd, kSampleRate);
        std::printf ("       dopplerFactor=0, approaching, measured ~%.1f Hz (should stay close to f0=%.0f Hz)\n", freq, f0);
        check (std::abs (freq - f0) < f0 * 0.02f, "dopplerFactor=0 test: pitch stays within 2% of source frequency");
    }

    // --- Test 3b: dopplerEnabled=false suppresses the pitch shift too,
    // independent of dopplerFactor (still at its default 1.0 here) -------
    {
        PropagationProcessor proc;
        proc.prepare (kSampleRate, kBlockSize);

        SoundObject dopplerOff = obj;
        dopplerOff.dopplerEnabled = false; // dopplerFactor stays at the default 1.0 -- gate, not the dial
        check (dopplerOff.dopplerFactor == 1.0f, "dopplerEnabled=false test: dopplerFactor itself is untouched by the toggle");

        const float f0 = 1000.0f;
        const float velocity = 30.0f;
        const int numSamples = (int) (2.0 * kSampleRate);
        auto approachPos = [&] (double t) -> Vec3 { return { 60.0f - (float) (velocity * t), 0.0f, 0.0f }; };

        auto out = runSine (proc, dopplerOff, scene, f0, numSamples, approachPos);
        check (allFinite (out), "dopplerEnabled=false test: output has no NaN/Inf");

        const int winStart = numSamples / 2;
        const int winEnd = numSamples / 2 + (int) kSampleRate / 2;
        const float freq = estimateFrequencyHz (out, winStart, winEnd, kSampleRate);
        std::printf ("       dopplerEnabled=false, approaching, measured ~%.1f Hz (should stay close to f0=%.0f Hz)\n", freq, f0);
        check (std::abs (freq - f0) < f0 * 0.02f, "dopplerEnabled=false test: pitch stays within 2% of source frequency despite dopplerFactor=1");
    }

    // --- Test 4: air absorption attenuates high frequencies more at distance ---
    {
        PropagationProcessor procNear, procFar;
        procNear.prepare (kSampleRate, kBlockSize);
        procFar.prepare (kSampleRate, kBlockSize);

        const float f0 = 8000.0f; // high frequency, should be affected by the lowpass
        const int numSamples = (int) (0.5 * kSampleRate);

        auto nearPos = [] (double) -> Vec3 { return { 1.0f, 0.0f, 0.0f }; };
        auto farPos  = [] (double) -> Vec3 { return { 40.0f, 0.0f, 0.0f }; };

        auto outNear = runSine (procNear, obj, scene, f0, numSamples, nearPos);
        auto outFar  = runSine (procFar,  obj, scene, f0, numSamples, farPos);

        check (allFinite (outNear) && allFinite (outFar), "air absorption test: output has no NaN/Inf");

        double rmsNear = 0.0, rmsFar = 0.0;
        const int rmsStart = numSamples / 2; // skip filter settling time
        for (int i = rmsStart; i < numSamples; ++i)
        {
            rmsNear += (double) outNear[(size_t) i] * outNear[(size_t) i];
            rmsFar  += (double) outFar[(size_t) i]  * outFar[(size_t) i];
        }
        rmsNear = std::sqrt (rmsNear / (numSamples - rmsStart));
        rmsFar  = std::sqrt (rmsFar  / (numSamples - rmsStart));

        std::printf ("       %.0f Hz RMS at 1m: %.4f, at 40m: %.4f\n", f0, rmsNear, rmsFar);
        check (rmsFar < rmsNear * 0.9, "air absorption test: high frequency is quieter (post-distance-gain-normalized trend) at 40m than at 1m");
    }

    // --- Test 5: directivity gain ------------------------------------------
    {
        SoundObject cardioid = obj;
        cardioid.directivityPattern = SoundObject::DirectivityPattern::Cardioid;
        cardioid.sourceOrientation = { -1.0f, 0.0f, 0.0f }; // facing toward the listener at (5,0,0)

        PropagationProcessor proc;
        proc.prepare (kSampleRate, kBlockSize);
        proc.reset();

        std::vector<float> in ((size_t) kBlockSize, 1.0f), out ((size_t) kBlockSize, 0.0f);
        float gainFacingListener = 0.0f;
        proc.process (in.data(), out.data(), kBlockSize, { 5.0f, 0.0f, 0.0f }, cardioid, scene, gainFacingListener);

        cardioid.sourceOrientation = { 1.0f, 0.0f, 0.0f }; // facing away from the listener
        proc.reset();
        float gainFacingAway = 0.0f;
        proc.process (in.data(), out.data(), kBlockSize, { 5.0f, 0.0f, 0.0f }, cardioid, scene, gainFacingAway);

        std::printf ("       cardioid facing listener: gain=%.3f, facing away: gain=%.3f\n", gainFacingListener, gainFacingAway);
        check (gainFacingListener > 0.9f, "directivity test: cardioid facing the listener has high gain");
        check (gainFacingAway < 0.1f, "directivity test: cardioid facing away has low gain");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
