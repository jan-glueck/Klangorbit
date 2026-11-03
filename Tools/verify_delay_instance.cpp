#include <cmath>
#include <cstdio>
#include <vector>
#include <juce_core/juce_core.h>
#include "../Source/DelayInstance.h"
#include "../Source/DelayRenderer.h"

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
}

// --- DelayRenderer.h: the real-time-safe write/read functions --------------

// Uses a SUSTAINED DC input rather than an impulse specifically so the
// prediction is exact regardless of the damping filter's shape: at DC
// (0 Hz) ANY LowPass filter has unity gain by definition (see
// Tools/verify_voice_filter.cpp's own DC-response test), so
// damped(y_inf) == y_inf once the feedback loop settles, and the
// steady-state gain is exactly the geometric-series sum 1/(1-feedbackGain)
// -- independent of cutoff/Q. An impulse, by contrast, gets measurably
// reshaped by the filter every time it recirculates (TPT filters smear
// transient energy even near Nyquist), so its repeat amplitudes don't
// cleanly follow feedbackGain^n -- exactly the wrong signal to test the
// feedback MATH with.
static void testFeedbackSettlesToGeometricSeriesGain()
{
    constexpr int bufferSize = 48000;
    std::vector<float> buf (bufferSize, 0.0f);
    int writePos = 0;

    VoiceFilter dampingFilter;
    dampingFilter.prepare (kSampleRate);
    dampingFilter.setCutoffAndResonance (20000.0f, 0.707f);

    const float feedbackGain = 0.5f;
    constexpr int totalSamples = 20000; // plenty of time to settle, even with a 1000-sample feedbackDelaySamples
    std::vector<float> input (totalSamples, 1.0f);

    writeDelayLineBlock (buf.data(), bufferSize, writePos, input.data(), totalSamples,
                          1000.0f, feedbackGain, dampingFilter);

    const float settled = buf[(size_t) ((writePos - 1 + bufferSize) % bufferSize)];
    const float expected = 1.0f / (1.0f - feedbackGain);

    std::printf ("       sustained DC=1 through feedback=0.5 settles to %.4f (expected %.4f = 1/(1-feedback))\n", settled, expected);
    check (std::abs (settled - expected) < 0.02f,
           "writeDelayLineBlock: a sustained input settles to the exact geometric-series gain 1/(1-feedbackGain)");
}

static void testZeroFeedbackMeansNoRepeats()
{
    constexpr int bufferSize = 48000;
    std::vector<float> buf (bufferSize, 0.0f);
    int writePos = 0;

    VoiceFilter noDamping;
    noDamping.prepare (kSampleRate);
    noDamping.setCutoffAndResonance (20000.0f, 0.707f);

    constexpr int totalSamples = 3000;
    std::vector<float> input (totalSamples, 0.0f);
    input[0] = 1.0f;

    writeDelayLineBlock (buf.data(), bufferSize, writePos, input.data(), totalSamples,
                          1000.0f, 0.0f, noDamping);

    bool anyNonzeroAfterImpulse = false;
    for (int i = 1; i < totalSamples; ++i)
        if (std::abs (buf[(size_t) i]) > 1.0e-6f)
            anyNonzeroAfterImpulse = true;

    check (! anyNonzeroAfterImpulse, "writeDelayLineBlock: feedbackGain == 0 produces no repeats at all (just the dry signal written through)");
}

static void testReadDelayTapBlockReadsExactDelay()
{
    constexpr int bufferSize = 48000;
    std::vector<float> buf (bufferSize, 0.0f);

    // Directly write a known pattern (bypassing writeDelayLineBlock, to
    // test readDelayTapBlock() in isolation): a ramp, so each sample has
    // a distinguishable value.
    for (int i = 0; i < bufferSize; ++i)
        buf[(size_t) i] = (float) i;

    constexpr int writePosAfterBlock = 10000;
    constexpr float tapDelaySamples = 500.0f;
    constexpr int numSamples = 8;
    std::vector<float> dest (numSamples, 0.0f);

    readDelayTapBlock (buf.data(), bufferSize, writePosAfterBlock, tapDelaySamples, dest.data(), numSamples);

    bool allCorrect = true;
    for (int i = 0; i < numSamples; ++i)
    {
        const float expected = (float) (writePosAfterBlock - numSamples + i) - tapDelaySamples;
        if (std::abs (dest[(size_t) i] - expected) > 0.01f)
            allCorrect = false;
    }
    check (allCorrect, "readDelayTapBlock: reads exactly tapDelaySamples behind each sample's own write time");
}

static void testFractionalDelayIsInterpolated()
{
    constexpr int bufferSize = 100;
    std::vector<float> buf (bufferSize, 0.0f);
    buf[10] = 0.0f;
    buf[11] = 10.0f; // a single sample step, so a fractional read between 10 and 11 should land strictly between 0 and 10

    std::vector<float> dest (1, 0.0f);
    // writtenAt = writePosAfterBlock - numSamples + i = writePosAfterBlock - 1 (numSamples=1, i=0);
    // readPos = writtenAt - tapDelaySamples. Want readPos == 10.5 (between
    // buf[10] and buf[11]) with tapDelaySamples=0.5 => writtenAt == 11.0
    // => writePosAfterBlock == 12.
    readDelayTapBlock (buf.data(), bufferSize, 12, 0.5f, dest.data(), 1);

    std::printf ("       fractional read between samples 10 (0.0) and 11 (10.0) at frac=0.5: %.3f\n", dest[0]);
    check (dest[0] > 0.1f && dest[0] < 9.9f, "readDelayTapBlock: a fractional delay linearly interpolates between neighboring samples");
}

// --- DelayInstance: control-rate tap configuration --------------------------

static void testDisabledByDefaultPublishesNoTapsActive()
{
    DelayInstance delay;
    delay.update (0.01, {}, kSampleRate);

    DelayInstance::Snapshot snap;
    delay.getSnapshot (snap);
    check (! snap.enabled, "DelayInstance: disabled by default (matches DelayInstanceSettings::enabled's own default)");
}

static void testTapCountMatchesSettingsAndIsClamped()
{
    DelayInstance delay;
    delay.getSettings().enabled = true;
    delay.getSettings().numTaps = 4;
    delay.update (0.01, {}, kSampleRate);

    DelayInstance::Snapshot snap;
    delay.getSnapshot (snap);
    check (snap.taps.size() == 4, "DelayInstance: Snapshot carries exactly numTaps taps");

    delay.getSettings().numTaps = 999; // way beyond DelayLimits::maxTaps
    delay.update (0.01, {}, kSampleRate);
    delay.getSnapshot (snap);
    check ((int) snap.taps.size() == DelayLimits::maxTaps, "DelayInstance: numTaps is clamped to DelayLimits::maxTaps, never allocates beyond it");
}

static void testTapsAreEvenlySpacedAroundTheParent()
{
    DelayInstance delay;
    delay.getSettings().enabled = true;
    delay.getSettings().numTaps = 4;
    delay.getSettings().tapSpreadRadius = 2.0f;
    delay.getSettings().tapOrbitAngularSpeed = 0.0f; // static
    delay.update (0.01, { 0.0f, 0.0f, 0.0f }, kSampleRate);

    DelayInstance::Snapshot snap;
    delay.getSnapshot (snap);

    bool allAtRadius = true;
    for (auto& tap : snap.taps)
    {
        const float dist = tap.position.length();
        if (std::abs (dist - 2.0f) > 0.01f)
            allAtRadius = false;
    }
    check (allAtRadius, "DelayInstance: every tap sits exactly tapSpreadRadius away from the parent");

    // 4 taps evenly spaced: opposite pairs (0&2, 1&3) should be ~diametrically opposite.
    const Vec3 sum02 = snap.taps[0].position + snap.taps[2].position;
    const Vec3 sum13 = snap.taps[1].position + snap.taps[3].position;
    check (sum02.length() < 0.05f && sum13.length() < 0.05f,
           "DelayInstance: 4 taps are evenly spaced around the circle (opposite pairs sum to ~zero offset)");
}

static void testOrbitingTapsActuallyMove()
{
    DelayInstance delay;
    delay.getSettings().enabled = true;
    delay.getSettings().numTaps = 2;
    delay.getSettings().tapSpreadRadius = 1.0f;
    delay.getSettings().tapOrbitAngularSpeed = 1.0f; // rad/s
    delay.update (0.01, {}, kSampleRate);

    DelayInstance::Snapshot before;
    delay.getSnapshot (before);
    const Vec3 posBefore = before.taps[0].position;

    for (int i = 0; i < 100; ++i) // 1s of ticks
        delay.update (0.01, {}, kSampleRate);

    DelayInstance::Snapshot after;
    delay.getSnapshot (after);
    const Vec3 posAfter = after.taps[0].position;

    const float moved = (posAfter - posBefore).length();
    check (moved > 0.1f, "DelayInstance: tapOrbitAngularSpeed > 0 actually moves the taps over time");
}

static void testStaticTapsDoNotMove()
{
    DelayInstance delay;
    delay.getSettings().enabled = true;
    delay.getSettings().numTaps = 2;
    delay.getSettings().tapOrbitAngularSpeed = 0.0f; // default, static
    delay.update (0.01, {}, kSampleRate);

    DelayInstance::Snapshot before;
    delay.getSnapshot (before);

    for (int i = 0; i < 100; ++i)
        delay.update (0.01, {}, kSampleRate);

    DelayInstance::Snapshot after;
    delay.getSnapshot (after);

    const float moved = (after.taps[0].position - before.taps[0].position).length();
    check (moved < 1.0e-4f, "DelayInstance: tapOrbitAngularSpeed == 0 (the default) keeps taps perfectly static");
}

static void testTapDelayTimesAreMultiplesOfDelayTime()
{
    DelayInstance delay;
    delay.getSettings().enabled = true;
    delay.getSettings().numTaps = 3;
    delay.getSettings().delayTime = 0.25f; // seconds
    delay.getSettings().delayTimeJitter = 0.0f;
    delay.update (0.01, {}, kSampleRate);

    DelayInstance::Snapshot snap;
    delay.getSnapshot (snap);

    const float expected0 = 1.0f * 0.25f * (float) kSampleRate;
    const float expected1 = 2.0f * 0.25f * (float) kSampleRate;
    const float expected2 = 3.0f * 0.25f * (float) kSampleRate;

    std::printf ("       tap delays (samples) at delayTime=0.25s: %.1f %.1f %.1f (expected %.1f %.1f %.1f)\n",
                 snap.taps[0].delaySamples, snap.taps[1].delaySamples, snap.taps[2].delaySamples,
                 expected0, expected1, expected2);

    check (std::abs (snap.taps[0].delaySamples - expected0) < 1.0f, "DelayInstance: tap 0's delay is exactly 1x delayTime (in samples)");
    check (std::abs (snap.taps[1].delaySamples - expected1) < 1.0f, "DelayInstance: tap 1's delay is exactly 2x delayTime");
    check (std::abs (snap.taps[2].delaySamples - expected2) < 1.0f, "DelayInstance: tap 2's delay is exactly 3x delayTime");
}

static void testDelayTimeJitterVariesTapMultiplesButReproducibly()
{
    DelayInstance delayA;
    delayA.getSettings().enabled = true;
    delayA.getSettings().numTaps = 5;
    delayA.getSettings().delayTime = 0.2f;
    delayA.getSettings().delayTimeJitter = 0.3f;
    delayA.getSettings().seed = 777;
    delayA.update (0.01, {}, kSampleRate);
    DelayInstance::Snapshot snapA;
    delayA.getSnapshot (snapA);

    DelayInstance delayB;
    delayB.getSettings().enabled = true;
    delayB.getSettings().numTaps = 5;
    delayB.getSettings().delayTime = 0.2f;
    delayB.getSettings().delayTimeJitter = 0.3f;
    delayB.getSettings().seed = 777;
    delayB.update (0.01, {}, kSampleRate);
    DelayInstance::Snapshot snapB;
    delayB.getSnapshot (snapB);

    bool allIdentical = true;
    bool anyJittered = false;
    for (size_t i = 0; i < snapA.taps.size(); ++i)
    {
        if (std::abs (snapA.taps[i].delaySamples - snapB.taps[i].delaySamples) > 0.01f)
            allIdentical = false;
        const float nominal = (float) (i + 1) * 0.2f * (float) kSampleRate;
        if (std::abs (snapA.taps[i].delaySamples - nominal) > 1.0f)
            anyJittered = true;
    }
    check (allIdentical, "DelayInstance: same seed + same settings produces identical (reproducible) tap delay times");
    check (anyJittered, "DelayInstance: delayTimeJitter > 0 actually perturbs tap delay times away from the exact multiple");
}

static void testDampingCutoffMapping()
{
    const float atZero = DelayInstance::dampingCutoffHzFor (0.0f);
    const float atOne = DelayInstance::dampingCutoffHzFor (1.0f);
    const float atHalf = DelayInstance::dampingCutoffHzFor (0.5f);

    std::printf ("       dampingCutoffHzFor(0/0.5/1): %.1f / %.1f / %.1f\n", atZero, atOne, atHalf);
    check (atZero > 15000.0f, "DelayInstance::dampingCutoffHzFor(0): no damping maps to a bright (near-Nyquist) cutoff");
    check (atOne < 1000.0f, "DelayInstance::dampingCutoffHzFor(1): full damping maps to a dark cutoff");
    check (atHalf < atZero && atHalf > atOne, "DelayInstance::dampingCutoffHzFor: monotonically decreasing as damping increases");
}

static void testFeedbackIsClampedToSafeMaximum()
{
    DelayInstance delay;
    delay.getSettings().enabled = true;
    delay.getSettings().feedback = 5.0f; // way beyond DelayLimits::maxFeedback
    delay.update (0.01, {}, kSampleRate);

    DelayInstance::Snapshot snap;
    delay.getSnapshot (snap);
    check (snap.feedbackGain <= DelayLimits::maxFeedback, "DelayInstance: published feedbackGain is clamped to DelayLimits::maxFeedback regardless of settings.feedback");
}

int main()
{
    testFeedbackSettlesToGeometricSeriesGain();
    testZeroFeedbackMeansNoRepeats();
    testReadDelayTapBlockReadsExactDelay();
    testFractionalDelayIsInterpolated();
    testDisabledByDefaultPublishesNoTapsActive();
    testTapCountMatchesSettingsAndIsClamped();
    testTapsAreEvenlySpacedAroundTheParent();
    testOrbitingTapsActuallyMove();
    testStaticTapsDoNotMove();
    testTapDelayTimesAreMultiplesOfDelayTime();
    testDelayTimeJitterVariesTapMultiplesButReproducibly();
    testDampingCutoffMapping();
    testFeedbackIsClampedToSafeMaximum();

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
