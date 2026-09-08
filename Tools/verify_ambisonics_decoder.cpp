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
    // pass's ramped output is discarded. Still used by the modes that keep
    // going through a decodeMatrix (Stereo) and the raw-passthrough no-op
    // check below.
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

    Vec3 directionFrom (float azimuthRad, float elevationRad)
    {
        return SpeakerLayouts::directionFromAngles (azimuthRad, elevationRad);
    }
}

int main()
{
    using Mode = AmbisonicsDecoder::Mode;

    // --- Metadata: every mode's helper functions agree with each other and
    //     with the SpeakerLayouts tables they're built from. ---
    {
        struct Expected { Mode mode; int channels; int order; bool raw; bool directPan; int lfeIndex; };
        const Expected table[] = {
            { Mode::AmbisonicsRawOrder1, 4,  1, true,  false, -1 },
            { Mode::AmbisonicsRawOrder2, 9,  2, true,  false, -1 },
            { Mode::AmbisonicsRawOrder3, 16, 3, true,  false, -1 },
            { Mode::AmbisonicsRawOrder4, 25, 4, true,  false, -1 },
            { Mode::AmbisonicsRawOrder5, 36, 5, true,  false, -1 },
            { Mode::Stereo,              2,  3, false, false, -1 },
            { Mode::Binaural,            2,  3, false, false, -1 },
            { Mode::Quad,                4,  3, false, true,  -1 },
            { Mode::Octophonic,          8,  3, false, true,  -1 },
            { Mode::Surround5_1,         6,  3, false, true,  3 },
            { Mode::Surround7_1,         8,  3, false, true,  3 },
            { Mode::Atmos5_1_2,          8,  3, false, true,  3 },
            { Mode::Atmos5_1_4,          10, 3, false, true,  3 },
            { Mode::Atmos7_1_2,          10, 3, false, true,  3 },
            { Mode::Atmos7_1_4,          12, 3, false, true,  3 },
        };

        for (auto& e : table)
        {
            check (AmbisonicsDecoder::numOutputChannels (e.mode) == e.channels, "numOutputChannels matches expected channel count");
            check (AmbisonicsDecoder::ambisonicsOrderFor (e.mode) == e.order, "ambisonicsOrderFor matches expected order");
            check (AmbisonicsDecoder::isRawPassthrough (e.mode) == e.raw, "isRawPassthrough matches expected");
            check (AmbisonicsDecoder::usesDirectPan (e.mode) == e.directPan, "usesDirectPan matches expected");
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
        check (AmbisonicsDecoder::usesDirectPan (Mode::CircularArray), "CircularArray: uses direct pan");
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
    //     output channel 0 (L). The only mode still going through
    //     decode()/decodeMatrix -- see AmbisonicsDecoder's own class
    //     comment for why every other real-speaker format below now uses
    //     computeDirectPanGains() instead. ---
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

    // --- Direct-pan sharpness: this is the actual regression check the
    //     reported diffuseness (opposite speaker only ~18dB down for a
    //     source panned exactly onto one speaker, measured by the
    //     reporting user in Quad and 7.1) motivates. For every direct-pan
    //     mode, panning exactly onto one real speaker's own direction must
    //     put (almost) ALL the gain on that one speaker and (near) zero
    //     everywhere else -- a real, numeric sharpness assertion, not just
    //     "loudest on the right channel" (which the old AllRAD-based tests
    //     already confirmed, and which alone would NOT have caught this
    //     bug: AllRAD was always directionally correct, just too diffuse). ---
    {
        struct Case { const char* name; Mode mode; int speakerIndex; float azimuthRad; float elevationRad; int expectedChannels; };
        const Case cases[] = {
            { "Quad (L, +45deg)",              Mode::Quad,         0, SpeakerLayouts::deg (45.0f),  0.0f,                 4 },
            { "Octophonic (FL, +22.5deg)",      Mode::Octophonic,   0, SpeakerLayouts::deg (22.5f),  0.0f,                 8 },
            { "Surround5_1 (C, 0deg)",          Mode::Surround5_1,  2, 0.0f,                          0.0f,                 6 },
            { "Surround7_1 (Lss, +90deg)",      Mode::Surround7_1,  4, SpeakerLayouts::deg (90.0f),   0.0f,                 8 },
            { "Atmos5_1_4 (topFrontL, +45/+45)", Mode::Atmos5_1_4,  6, SpeakerLayouts::deg (45.0f),   SpeakerLayouts::deg (45.0f), 10 },
        };

        for (auto& c : cases)
        {
            AmbisonicsDecoder decoder;
            decoder.prepare (48000.0);
            decoder.setMode (c.mode);

            const auto gains = decoder.computeDirectPanGains (directionFrom (c.azimuthRad, c.elevationRad));
            check ((int) gains.size() == c.expectedChannels, "direct-pan sharpness: computeDirectPanGains() returns a full-width, correctly-sized vector");

            float targetGain = 0.0f, worstOtherGain = 0.0f;
            for (int ch = 0; ch < (int) gains.size(); ++ch)
            {
                if (ch == c.speakerIndex) targetGain = gains[(size_t) ch];
                else worstOtherGain = juce::jmax (worstOtherGain, std::abs (gains[(size_t) ch]));
            }
            std::printf ("       %s: target speaker gain=%.4f, worst other speaker gain=%.4f\n", c.name, targetGain, worstOtherGain);
            check (targetGain > 0.9f, "direct-pan sharpness: panning exactly onto a speaker gives that speaker ~full gain");
            check (worstOtherGain < 0.05f, "direct-pan sharpness: panning exactly onto a speaker leaves every OTHER speaker's gain near-zero (the actual reported-diffuseness regression check)");
        }
    }

    // --- LFE never gets directional gain from computeDirectPanGains() -----
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setMode (Mode::Surround5_1);

        const int lfeIdx = AmbisonicsDecoder::lfeChannelIndexFor (Mode::Surround5_1);
        check (lfeIdx == 3, "5.1: LFE channel index is 3 (matches SpeakerLayouts::surround5point1() ordering)");

        // Sweep several directions, including straight at the LFE's own
        // (nonexistent) direction -- it must never receive gain regardless.
        bool lfeAlwaysZero = true;
        for (float az : { 0.0f, SpeakerLayouts::deg (30.0f), SpeakerLayouts::deg (110.0f), SpeakerLayouts::deg (180.0f) })
        {
            const auto gains = decoder.computeDirectPanGains (directionFrom (az, 0.0f));
            if (lfeIdx >= 0 && lfeIdx < (int) gains.size() && gains[(size_t) lfeIdx] != 0.0f)
                lfeAlwaysZero = false;
        }
        check (lfeAlwaysZero, "5.1: computeDirectPanGains() never assigns gain to the LFE channel (LFE has no direction)");
    }

    // --- applyLfeFilterDirect(): silent when bass management is off, picks
    //     up a filtered signal once enabled -- the direct-pan modes' own
    //     equivalent of decode()'s W-channel-derived LFE handling. ---
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setMode (Mode::Surround5_1);

        std::vector<float> drySum ((size_t) blockSize);
        for (int i = 0; i < blockSize; ++i)
            drySum[(size_t) i] = std::sin (2.0f * juce::MathConstants<float>::pi * 100.0f * (float) i / 48000.0f);

        std::vector<float> lfeOff ((size_t) blockSize, 1.0f); // pre-filled with garbage to confirm it actually gets overwritten with silence
        decoder.applyLfeFilterDirect (drySum.data(), lfeOff.data(), blockSize);
        bool allZeroOff = true;
        for (float v : lfeOff) if (v != 0.0f) { allZeroOff = false; break; }
        check (allZeroOff, "applyLfeFilterDirect: silent when bass management is disabled (default)");

        decoder.setBassManagementEnabled (true);
        std::vector<float> lfeOn ((size_t) blockSize, 0.0f);
        decoder.applyLfeFilterDirect (drySum.data(), lfeOn.data(), blockSize);
        double energy = 0.0;
        for (float v : lfeOn) energy += (double) v * (double) v;
        std::printf ("       applyLfeFilterDirect: RMS with bass management on: %.4f\n", (float) std::sqrt (energy / blockSize));
        check (energy > 0.0, "applyLfeFilterDirect: picks up a filtered signal once bass management is enabled");
    }

    // --- CircularArray: direct-pan setup responds to setCircularArraySpeakerCount() ---
    {
        AmbisonicsDecoder decoder;
        decoder.prepare (48000.0);
        decoder.setCircularArraySpeakerCount (6);
        decoder.setMode (Mode::CircularArray);

        auto gains6 = decoder.computeDirectPanGains (directionFrom (0.0f, 0.0f));
        check ((int) gains6.size() == 6, "CircularArray(6): computeDirectPanGains() returns a 6-wide vector");
        check (gains6[0] > 0.9f, "CircularArray(6): a source straight ahead (0deg start) pans almost entirely onto channel 0");

        // Sweep every supported speaker count -- setCircularArraySpeakerCount()
        // rebuilds the direct-pan setup live since CircularArray is already
        // the active mode; each rebuild must still produce a sane
        // (correctly-sized, non-degenerate) result.
        bool allSweepsReasonable = true;
        for (int n = AmbisonicsDecoder::minCircularSpeakers; n <= AmbisonicsDecoder::maxCircularSpeakers; ++n)
        {
            decoder.setCircularArraySpeakerCount (n);
            const auto gains = decoder.computeDirectPanGains (directionFrom (0.0f, 0.0f));
            if ((int) gains.size() != n || gains[0] < 0.9f)
                allSweepsReasonable = false;
        }
        check (allSweepsReasonable, "CircularArray: every supported speaker count (4..24) still pans a straight-ahead source almost entirely onto channel 0");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
