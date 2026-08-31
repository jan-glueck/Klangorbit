#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/HrtfDataset.h"
#include "../Source/BinauralDecoder.h"
#include "../Source/AmbisonicsEncoder.h"

// Exercises HrtfDataset (the libmysofa wrapper) and BinauralDecoder (the
// virtual-array + convolution decode) against the two bundled real SOFA
// files directly -- see each section's own comment below for why real
// data is used rather than a hand-built synthetic HRTF: KEMAR in
// particular is small (710 directions, ~11ms filters) and fast to
// prepare(), so there's no need to avoid real data purely for test speed,
// and testing against it exercises the actual end-to-end pipeline rather
// than just BinauralDecoder's own plumbing in isolation.

namespace
{
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

    bool anyNonZero (const std::vector<float>& v)
    {
        for (float x : v)
            if (x != 0.0f)
                return true;
        return false;
    }
}

int main()
{
    // ==================================================================
    // HrtfDataset: load the two real bundled SOFA files directly from
    // disk (SAPOC_ASSETS_HRTF_DIR -- a local-dev-only absolute path
    // define, same convention as SAPOC_PRESETS_USER_DIR etc., see
    // CMakeLists.txt) and sanity-check a handful of known directions --
    // confirms libmysofa itself can actually open both real-world files
    // (this is the one thing genuinely worth re-verifying here: SADIE's
    // own SOFA files have tripped a validation bug in some libmysofa
    // versions before, see HrtfDataset.h's own comment) and that
    // HrtfDataset's wrapper plumbing (resize, pass-through) works
    // correctly against real data.
    // ==================================================================
   #if ! defined (SAPOC_ASSETS_HRTF_DIR)
    #error "SAPOC_ASSETS_HRTF_DIR must be defined (see CMakeLists.txt's verify_binaural_decoder target)"
   #endif
    const juce::File hrtfDir (SAPOC_ASSETS_HRTF_DIR);

    const struct { const char* name; juce::File file; } datasets[] = {
        { "KEMAR", hrtfDir.getChildFile ("kemar_44100.sofa") },
        { "SADIE II D1", hrtfDir.getChildFile ("sadie_d1_44100.sofa") },
    };

    for (auto& ds : datasets)
    {
        HrtfDataset hrtf;
        juce::String error;
        const bool loaded = hrtf.load (ds.file, 44100.0, error);

        check (loaded, (juce::String (ds.name) + ": loads successfully via libmysofa").toRawUTF8());
        if (! loaded)
        {
            std::printf ("       (%s)\n", error.toRawUTF8());
            continue;
        }

        check (hrtf.getFilterLength() > 0, (juce::String (ds.name) + ": reports a positive filter length").toRawUTF8());

        const Vec3 testDirections[] = {
            { 1.0f, 0.0f, 0.0f },  // front
            { -1.0f, 0.0f, 0.0f }, // back
            { 0.0f, 1.0f, 0.0f },  // left
            { 0.0f, -1.0f, 0.0f }, // right
            { 0.0f, 0.0f, 1.0f },  // up
        };
        const char* directionNames[] = { "front", "back", "left", "right", "up" };

        bool allDirectionsOk = true;
        for (size_t i = 0; i < 5; ++i)
        {
            std::vector<float> left, right;
            float delayL = 0.0f, delayR = 0.0f;
            const bool ok = hrtf.getFilter (testDirections[i], left, right, delayL, delayR);

            if (! ok || (int) left.size() != hrtf.getFilterLength() || (int) right.size() != hrtf.getFilterLength()
                || ! allFinite (left) || ! allFinite (right) || ! anyNonZero (left) || ! anyNonZero (right))
            {
                allDirectionsOk = false;
                std::printf ("       (%s direction \"%s\" failed the sanity check)\n", ds.name, directionNames[i]);
            }
        }
        check (allDirectionsOk, (juce::String (ds.name) + ": getFilter() returns correctly-sized, finite, non-silent taps for front/back/left/right/up").toRawUTF8());
    }

    // ==================================================================
    // BinauralDecoder: prepare() against the real bundled KEMAR dataset,
    // then decode() a known-direction test source and check basic,
    // verifiable physical plausibility (ILD -- a source panned hard left
    // should come out louder in the left output channel than the right,
    // and vice versa) -- the same "verify direction/physical
    // plausibility, not exact sample matches" style already used by
    // verify_propagation/verify_ambisonics_decoder, not a golden-value
    // comparison against one specific HRTF's own exact numbers.
    // ==================================================================
    {
        HrtfDataset kemar;
        juce::String error;
        const bool loaded = kemar.load (hrtfDir.getChildFile ("kemar_44100.sofa"), 44100.0, error);
        check (loaded, "BinauralDecoder test: KEMAR loads for prepare()");

        constexpr int blockSize = 512;
        BinauralDecoder decoder;
        decoder.prepare (kemar, 44100.0, blockSize);

        AmbisonicsEncoder encoder;
        encoder.setOrder (3);
        const int numAmbiCh = encoder.getNumChannels();

        // Encodes a 300Hz test tone from (azimuthRad, elevationRad) and
        // returns the decoded stereo output's per-channel RMS.
        auto encodeAndDecodeRms = [&] (float azimuthRad, float elevationRad, float& outLeftRms, float& outRightRms)
        {
            std::vector<float> mono ((size_t) blockSize);
            for (int i = 0; i < blockSize; ++i)
                mono[(size_t) i] = std::sin (2.0f * juce::MathConstants<float>::pi * 300.0f * (float) i / 44100.0f);

            juce::AudioBuffer<float> ambi (numAmbiCh, blockSize);
            ambi.clear();
            std::vector<float> prevGains ((size_t) numAmbiCh, 0.0f);
            encoder.encodeBlock (mono.data(), blockSize, azimuthRad, elevationRad, 1.0f, 1.0f, ambi, prevGains);

            juce::AudioBuffer<float> stereo (2, blockSize);
            decoder.decode (ambi, stereo, blockSize);

            auto rms = [&] (int channel)
            {
                const float* d = stereo.getReadPointer (channel);
                double sum = 0.0;
                for (int i = 0; i < blockSize; ++i)
                    sum += (double) d[i] * (double) d[i];
                return (float) std::sqrt (sum / (double) blockSize);
            };
            outLeftRms = rms (0);
            outRightRms = rms (1);
        };

        // This project's own convention (SoundObject.h): y = left-positive,
        // azimuth = atan2(y, x) -- a source at y=+1 (straight left) is
        // azimuth = +halfPi.
        float leftRmsFromLeftSource = 0.0f, rightRmsFromLeftSource = 0.0f;
        encodeAndDecodeRms (juce::MathConstants<float>::halfPi, 0.0f, leftRmsFromLeftSource, rightRmsFromLeftSource);
        check (std::isfinite (leftRmsFromLeftSource) && std::isfinite (rightRmsFromLeftSource),
               "BinauralDecoder: output is finite for a hard-left source");
        check (leftRmsFromLeftSource > rightRmsFromLeftSource,
               "BinauralDecoder: a hard-left source (azimuth=+90deg) produces more energy in the left output channel than the right");

        float leftRmsFromRightSource = 0.0f, rightRmsFromRightSource = 0.0f;
        encodeAndDecodeRms (-juce::MathConstants<float>::halfPi, 0.0f, leftRmsFromRightSource, rightRmsFromRightSource);
        check (rightRmsFromRightSource > leftRmsFromRightSource,
               "BinauralDecoder: a hard-right source (azimuth=-90deg) produces more energy in the right output channel than the left");

        // Silence in -> silence out (basic linearity/no-hidden-DC-offset
        // check). Uses a SEPARATE, freshly-prepared decoder instance,
        // not the one just fed non-silent blocks above: juce::dsp::
        // Convolution is a stateful overlap-add engine, so a decoder that
        // just processed real signal still has a non-zero tail in flight
        // and would legitimately keep producing (decaying) output for a
        // few blocks after its input goes silent -- that's correct
        // convolution behaviour, not a bug, so it's not what this check
        // is testing for.
        {
            BinauralDecoder freshDecoder;
            freshDecoder.prepare (kemar, 44100.0, blockSize);

            juce::AudioBuffer<float> silentAmbi (numAmbiCh, blockSize);
            silentAmbi.clear();
            juce::AudioBuffer<float> stereo (2, blockSize);
            freshDecoder.decode (silentAmbi, stereo, blockSize);

            bool allZero = true;
            for (int ch = 0; ch < 2 && allZero; ++ch)
            {
                const float* d = stereo.getReadPointer (ch);
                for (int i = 0; i < blockSize; ++i)
                    if (d[i] != 0.0f) { allZero = false; break; }
            }
            check (allZero, "BinauralDecoder: silent ambisonics input produces exactly silent output");
        }
    }

    // decode() called on a BinauralDecoder that was never successfully
    // prepare()d (or was prepared with an unloaded dataset) must be a
    // safe no-op -- clears whatever garbage was already in the output
    // buffer, never crashes. See the class comment.
    {
        BinauralDecoder decoder; // never prepared
        juce::AudioBuffer<float> ambi (16, 64);
        ambi.clear();
        juce::AudioBuffer<float> stereo (2, 64);
        stereo.getWritePointer (0)[0] = 1.0f; // pre-fill with garbage to confirm decode() actually clears it
        stereo.getWritePointer (1)[0] = 1.0f;

        decoder.decode (ambi, stereo, 64);

        bool allZero = true;
        for (int ch = 0; ch < 2 && allZero; ++ch)
        {
            const float* d = stereo.getReadPointer (ch);
            for (int i = 0; i < 64; ++i)
                if (d[i] != 0.0f) { allZero = false; break; }
        }
        check (allZero, "BinauralDecoder: decode() without a successful prepare() is a safe no-op (clears output, no crash)");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
