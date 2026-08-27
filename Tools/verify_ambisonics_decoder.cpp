#include <cmath>
#include <cstdio>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/AmbisonicsDecoder.h"
#include "../Source/AmbisonicsEncoder.h"
#include "../Source/SpeakerLayouts.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    constexpr int blockSize = 1024;

    // Encodes a steady 440Hz test tone from one direction into a fresh
    // ambisonics buffer. Runs encodeBlock() twice into the same
    // previousChannelGains state so the second pass is ramp-free (start
    // gain == end gain, see AmbisonicsEncoder::encodeBlock()) -- the first
    // pass's ramped output is discarded.
    juce::AudioBuffer<float> encodeTestSource (int ambiOrder, float azimuthRad, float elevationRad)
    {
        AmbisonicsEncoder encoder;
        encoder.setOrder (ambiOrder);
        encoder.prepare (48000.0, blockSize);

        std::vector<float> source ((size_t) blockSize);
        for (int i = 0; i < blockSize; ++i)
            source[(size_t) i] = std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / 48000.0f);

        juce::AudioBuffer<float> ambi (encoder.getNumChannels(), blockSize);
        std::vector<float> previousGains;

        ambi.clear();
        encoder.encodeBlock (source.data(), blockSize, azimuthRad, elevationRad, 1.0f, 1.0f, ambi, previousGains);
        ambi.clear();
        encoder.encodeBlock (source.data(), blockSize, azimuthRad, elevationRad, 1.0f, 1.0f, ambi, previousGains);
        return ambi;
    }

    float rms (const juce::AudioBuffer<float>& buffer, int channel)
    {
        return buffer.getRMSLevel (channel, 0, buffer.getNumSamples());
    }
}

int main()
{
    using Mode = AmbisonicsDecoder::Mode;

    // --- Metadata: every mode's helper functions agree with each other and
    //     with the SpeakerLayouts tables they're built from. ---
    {
        struct Expected { Mode mode; int channels; int order; bool raw; int lfeIndex; };
        const Expected table[] = {
            { Mode::AmbisonicsRawOrder1, 4,  1, true,  -1 },
            { Mode::AmbisonicsRawOrder2, 9,  2, true,  -1 },
            { Mode::AmbisonicsRawOrder3, 16, 3, true,  -1 },
            { Mode::Stereo,              2,  3, false, -1 },
            { Mode::Quad,                4,  3, false, -1 },
            { Mode::Surround5_1,         6,  3, false, 3 },
            { Mode::Surround7_1,         8,  3, false, 3 },
            { Mode::Atmos5_1_2,          8,  3, false, 3 },
            { Mode::Atmos5_1_4,          10, 3, false, 3 },
            { Mode::Atmos7_1_2,          10, 3, false, 3 },
            { Mode::Atmos7_1_4,          12, 3, false, 3 },
            { Mode::Octophonic,          8,  3, false, -1 },
        };

        for (auto& e : table)
        {
            check (AmbisonicsDecoder::numOutputChannels (e.mode) == e.channels, "numOutputChannels matches expected channel count");
            check (AmbisonicsDecoder::ambisonicsOrderFor (e.mode) == e.order, "ambisonicsOrderFor matches expected order");
            check (AmbisonicsDecoder::isRawPassthrough (e.mode) == e.raw, "isRawPassthrough matches expected");
            check (AmbisonicsDecoder::lfeChannelIndexFor (e.mode) == e.lfeIndex, "lfeChannelIndexFor matches expected");
            check (AmbisonicsDecoder::outputChannelSetFor (e.mode).size() == e.channels, "outputChannelSetFor's channel count matches numOutputChannels");
        }

        // CircularArray: channel count/bus tracks circularSpeakerCount, not a fixed value.
        for (int n : { AmbisonicsDecoder::minCircularSpeakers, 8, 12, AmbisonicsDecoder::maxCircularSpeakers })
        {
            check (AmbisonicsDecoder::numOutputChannels (Mode::CircularArray, n) == n, "CircularArray: numOutputChannels tracks circularSpeakerCount");
            check (AmbisonicsDecoder::outputChannelSetFor (Mode::CircularArray, n).size() == n, "CircularArray: outputChannelSetFor's channel count tracks circularSpeakerCount");
        }
        check (AmbisonicsDecoder::numOutputChannels (Mode::CircularArray, 2) == AmbisonicsDecoder::minCircularSpeakers, "CircularArray: numOutputChannels clamps below minCircularSpeakers");
        check (AmbisonicsDecoder::numOutputChannels (Mode::CircularArray, 99) == AmbisonicsDecoder::maxCircularSpeakers, "CircularArray: numOutputChannels clamps above maxCircularSpeakers");
        check (AmbisonicsDecoder::ambisonicsOrderFor (Mode::CircularArray) == 3, "CircularArray: ambisonicsOrderFor is the fixed internal maximum");
        check (! AmbisonicsDecoder::isRawPassthrough (Mode::CircularArray), "CircularArray: not raw passthrough");
        check (AmbisonicsDecoder::lfeChannelIndexFor (Mode::CircularArray) == -1, "CircularArray: no LFE channel (horizontal ring, no LFE by design)");
    }

    // --- Raw passthrough: decode() is a safe (defensive-only) no-op -------
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setMode (Mode::AmbisonicsRawOrder3);

        const auto ambi = encodeTestSource (3, 0.0f, 0.0f);
        juce::AudioBuffer<float> out (16, blockSize);
        out.clear();
        decoder.decode (ambi, out, blockSize);
        check (out.getMagnitude (0, blockSize) == 0.0f, "Raw passthrough: decode() is a defensive no-op (caller is expected to bypass this class entirely, see class comment)");
    }

    // --- Stereo decode: a source at the L speaker's own direction (+30deg,
    //     matches SpeakerLayouts::stereoPair()) comes out mostly on
    //     output channel 0 (L). ---
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setMode (Mode::Stereo);

        const auto ambi = encodeTestSource (3, SpeakerLayouts::deg (30.0f), 0.0f);
        juce::AudioBuffer<float> out (2, blockSize);
        decoder.decode (ambi, out, blockSize);

        const float l = rms (out, 0);
        const float r = rms (out, 1);
        std::printf ("       Stereo L/R RMS at +30deg: L=%.4f R=%.4f\n", l, r);
        check (l > r * 2.0f, "Stereo: a source at the L speaker's own direction comes out mostly on the L channel");
    }

    // --- Quad (AllRAD, horizontal-only): a source at the L speaker's own
    //     direction (+45deg) comes out mostly on output channel 0 (L). ---
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setMode (Mode::Quad);

        const auto ambi = encodeTestSource (3, SpeakerLayouts::deg (45.0f), 0.0f);
        juce::AudioBuffer<float> out (4, blockSize);
        decoder.decode (ambi, out, blockSize);

        const float rmsAt[4] = { rms (out, 0), rms (out, 1), rms (out, 2), rms (out, 3) };
        std::printf ("       Quad RMS at +45deg (L R Ls Rs): %.4f %.4f %.4f %.4f\n", rmsAt[0], rmsAt[1], rmsAt[2], rmsAt[3]);
        check (rmsAt[0] > rmsAt[1] && rmsAt[0] > rmsAt[2] && rmsAt[0] > rmsAt[3],
               "Quad: a source at the L speaker's own direction comes out strongest on the L channel");
    }

    // --- Octophonic (mode-matching, regular 8-speaker ring): a source at
    //     the front-left speaker's own direction (+22.5deg) comes out
    //     strongest on output channel 0. ---
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setMode (Mode::Octophonic);

        const auto ambi = encodeTestSource (3, SpeakerLayouts::deg (22.5f), 0.0f);
        juce::AudioBuffer<float> out (8, blockSize);
        decoder.decode (ambi, out, blockSize);

        float rmsAt[8];
        for (int ch = 0; ch < 8; ++ch) rmsAt[ch] = rms (out, ch);
        std::printf ("       Octophonic RMS at +22.5deg (FL FR sL sR rL rR RL RR): %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                     rmsAt[0], rmsAt[1], rmsAt[2], rmsAt[3], rmsAt[4], rmsAt[5], rmsAt[6], rmsAt[7]);
        bool frontLeftIsLoudest = true;
        for (int ch = 1; ch < 8; ++ch)
            if (rmsAt[ch] >= rmsAt[0]) frontLeftIsLoudest = false;
        check (frontLeftIsLoudest, "Octophonic: a source at the front-left speaker's own direction comes out strongest on that single channel");
    }

    // --- Circular Array (mode-matching, generic N): with numSpeakers=6, a
    //     source straight ahead (channel 0's own direction, 0deg start)
    //     comes out strongest on output channel 0; sweeping numSpeakers
    //     across its full range doesn't crash or misbehave. ---
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setCircularArraySpeakerCount (6);
        decoder.setMode (Mode::CircularArray);

        const auto ambi = encodeTestSource (3, 0.0f, 0.0f);
        juce::AudioBuffer<float> out (6, blockSize);
        decoder.decode (ambi, out, blockSize);

        float rmsAt[6];
        for (int ch = 0; ch < 6; ++ch) rmsAt[ch] = rms (out, ch);
        std::printf ("       CircularArray(6) RMS at 0deg: %.4f %.4f %.4f %.4f %.4f %.4f\n",
                     rmsAt[0], rmsAt[1], rmsAt[2], rmsAt[3], rmsAt[4], rmsAt[5]);
        bool channel0IsLoudest = true;
        for (int ch = 1; ch < 6; ++ch)
            if (rmsAt[ch] >= rmsAt[0]) channel0IsLoudest = false;
        check (channel0IsLoudest, "CircularArray(6): a source straight ahead comes out strongest on channel 0 (0deg start)");

        // Sweep every supported speaker count -- setCircularArraySpeakerCount()
        // rebuilds live since CircularArray is already the active mode; each
        // rebuild must still produce a sane (non-empty, correctly sized,
        // reasonably calibrated) decode.
        bool allSweepsReasonable = true;
        for (int n = AmbisonicsDecoder::minCircularSpeakers; n <= AmbisonicsDecoder::maxCircularSpeakers; ++n)
        {
            decoder.setCircularArraySpeakerCount (n);
            juce::AudioBuffer<float> sweepOut (n, blockSize);
            decoder.decode (ambi, sweepOut, blockSize);

            float totalEnergy = 0.0f;
            for (int ch = 0; ch < n; ++ch) { const float r = rms (sweepOut, ch); totalEnergy += r * r; }
            const float totalRms = std::sqrt (totalEnergy);
            if (totalRms < 0.05f || totalRms > 3.0f)
                allSweepsReasonable = false;
        }
        check (allSweepsReasonable, "CircularArray: every supported speaker count (4..24) decodes to a reasonable overall output level");
    }

    // --- 5.1 (AllRAD, including a narrowly-flanked speaker): LFE channel
    //     stays silent by default and picks up a filtered signal once bass
    //     management is enabled; a source straight ahead comes out
    //     strongest on C, even though C's own VBAP catchment area is
    //     narrower than L/R's (see buildAllRadMatrix()'s per-speaker
    //     density-compensation comment -- this is the case that regressed
    //     without it). ---
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setMode (Mode::Surround5_1);

        const auto ambi = encodeTestSource (3, 0.0f, 0.0f); // straight ahead, at the C speaker
        const int lfeIdx = AmbisonicsDecoder::lfeChannelIndexFor (Mode::Surround5_1);
        check (lfeIdx == 3, "5.1: LFE channel index is 3 (matches SpeakerLayouts::surround5point1() ordering)");

        juce::AudioBuffer<float> outOff (6, blockSize);
        decoder.decode (ambi, outOff, blockSize);
        check (rms (outOff, lfeIdx) == 0.0f, "5.1: LFE channel is silent by default (bass management off)");

        decoder.setBassManagementEnabled (true);
        juce::AudioBuffer<float> outOn (6, blockSize);
        decoder.decode (ambi, outOn, blockSize);
        std::printf ("       5.1 LFE RMS with bass management on: %.4f\n", rms (outOn, lfeIdx));
        check (rms (outOn, lfeIdx) > 0.01f, "5.1: LFE channel picks up a filtered signal once bass management is enabled");

        std::printf ("       5.1 RMS straight ahead (L R C LFE Ls Rs): %.4f %.4f %.4f %.4f %.4f %.4f\n",
                     rms (outOn, 0), rms (outOn, 1), rms (outOn, 2), rms (outOn, 3), rms (outOn, 4), rms (outOn, 5));
        check (rms (outOn, 2) > rms (outOn, 0) && rms (outOn, 2) > rms (outOn, 1),
               "5.1: a source straight ahead comes out strongest on the C channel");
    }

    // --- 5.1.4 (AllRAD with height speakers): a source at the top-front-left
    //     speaker's own direction comes out strongest on that single
    //     channel, ahead of every other individual channel (some spread
    //     onto neighboring channels is expected/normal for a two-stage
    //     AllRAD decode, so this checks "clearly the loudest channel", not
    //     "louder than everything else combined"). ---
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setMode (Mode::Atmos5_1_4);

        // SpeakerLayouts::atmos5point1point4() order: L R C LFE Ls Rs
        // topFrontL(6) topFrontR(7) topRearL(8) topRearR(9).
        const auto ambi = encodeTestSource (3, SpeakerLayouts::deg (45.0f), SpeakerLayouts::deg (45.0f));
        juce::AudioBuffer<float> out (10, blockSize);
        decoder.decode (ambi, out, blockSize);

        float rmsAt[10];
        for (int ch = 0; ch < 10; ++ch) rmsAt[ch] = rms (out, ch);
        std::printf ("       5.1.4 all 10 RMS (L R C LFE Ls Rs tFL tFR tRL tRR): %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                     rmsAt[0], rmsAt[1], rmsAt[2], rmsAt[3], rmsAt[4], rmsAt[5], rmsAt[6], rmsAt[7], rmsAt[8], rmsAt[9]);

        bool topFrontLeftIsLoudest = true;
        for (int ch = 0; ch < 10; ++ch)
            if (ch != 6 && rmsAt[ch] >= rmsAt[6])
                topFrontLeftIsLoudest = false;
        check (topFrontLeftIsLoudest, "5.1.4: a source at the top-front-left speaker's own direction comes out strongest on that single channel");
    }

    // --- Calibration sanity: overall output level for a decoded mode stays
    //     in a reasonable range relative to the (RMS ~0.707) input tone,
    //     across several directions -- not a strict theoretical check, just
    //     a guard against the practical calibration (see
    //     AmbisonicsDecoder::calibrateDecodeMatrix()) going wildly wrong. ---
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setMode (Mode::Atmos7_1_4);

        bool allReasonable = true;
        for (int i = 0; i < 8; ++i)
        {
            const float az = SpeakerLayouts::deg ((float) i * 47.0f);
            const float el = SpeakerLayouts::deg ((float) (i % 3) * 20.0f);
            const auto ambi = encodeTestSource (3, az, el);
            juce::AudioBuffer<float> out (12, blockSize);
            decoder.decode (ambi, out, blockSize);

            float totalEnergy = 0.0f;
            for (int ch = 0; ch < 12; ++ch) { const float r = rms (out, ch); totalEnergy += r * r; }
            const float totalRms = std::sqrt (totalEnergy);
            if (totalRms < 0.05f || totalRms > 3.0f)
                allReasonable = false;
        }
        check (allReasonable, "7.1.4: calibrated overall output level stays within a reasonable range (0.05..3.0 RMS) across several source directions");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
