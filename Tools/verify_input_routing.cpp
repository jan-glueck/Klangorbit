#include <cmath>
#include <cstdio>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/InputRouting.h"

/**
    Verifies InputRouting.h (feature/input-routing-matrix) in isolation:
    resolveChannels()'s inactive/default/explicit-list cases, and
    mixRoutedInput()'s summing correctness, out-of-range-channel
    handling, and "doesn't clear dst itself" contract.
*/
namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    bool nearlyEqual (float a, float b, float tolerance = 1.0e-5f)
    {
        return std::abs (a - b) <= tolerance;
    }
}

int main()
{
    // --- resolveChannels(): inactive object -> empty -----------------------
    {
        SoundObject obj;
        obj.inputChannel = -1;
        obj.routedInputChannels = { 0, 1 }; // even if set, an inactive object is never routed
        const auto resolved = InputRouting::resolveChannels (obj);
        check (resolved.empty(), "resolveChannels() returns empty for an inactive object, regardless of routedInputChannels");
    }

    // --- resolveChannels(): empty routedInputChannels -> the 1:1 default ---
    {
        SoundObject obj;
        obj.inputChannel = 3;
        obj.routedInputChannels.clear();
        const auto resolved = InputRouting::resolveChannels (obj);
        check (resolved.size() == 1 && resolved[0] == 3,
               "resolveChannels() with empty routedInputChannels falls back to {inputChannel} (the 1:1 default)");
    }

    // --- resolveChannels(): non-empty routedInputChannels -- fully ---------
    // replaces the default, including moving off the object's own identity
    // channel entirely
    {
        SoundObject obj;
        obj.inputChannel = 2;
        obj.routedInputChannels = { 0, 5 };
        const auto resolved = InputRouting::resolveChannels (obj);
        check (resolved.size() == 2 && resolved[0] == 0 && resolved[1] == 5,
               "resolveChannels() with a non-empty routedInputChannels returns it verbatim, not including inputChannel itself");
    }

    // --- resolveChannels(): a single explicit entry, even if it matches ----
    // the object's own identity channel, still goes through the
    // "non-empty, authoritative" path (not a special case)
    {
        SoundObject obj;
        obj.inputChannel = 4;
        obj.routedInputChannels = { 4 };
        const auto resolved = InputRouting::resolveChannels (obj);
        check (resolved.size() == 1 && resolved[0] == 4,
               "resolveChannels() with routedInputChannels == {inputChannel} still returns that one entry");
    }

    // --- mixRoutedInput(): sums multiple channels --------------------------
    {
        juce::AudioBuffer<float> input (4, 4);
        input.clear();
        for (int i = 0; i < 4; ++i)
        {
            input.getWritePointer (0)[i] = 1.0f;
            input.getWritePointer (1)[i] = 2.0f;
            input.getWritePointer (2)[i] = 4.0f;
            input.getWritePointer (3)[i] = 8.0f;
        }

        float dst[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        InputRouting::mixRoutedInput (dst, input, { 0, 2 }, 4, 4);
        bool allCorrect = true;
        for (int i = 0; i < 4; ++i)
            if (! nearlyEqual (dst[i], 5.0f)) // 1.0 + 4.0
                allCorrect = false;
        check (allCorrect, "mixRoutedInput() sums channels 0 and 2 correctly (1.0 + 4.0 = 5.0 per sample)");
    }

    // --- mixRoutedInput(): single channel is a pure copy --------------------
    {
        juce::AudioBuffer<float> input (2, 3);
        input.getWritePointer (1)[0] = 1.0f;
        input.getWritePointer (1)[1] = 2.0f;
        input.getWritePointer (1)[2] = 3.0f;

        float dst[3] = { 0.0f, 0.0f, 0.0f };
        InputRouting::mixRoutedInput (dst, input, { 1 }, 2, 3);
        check (nearlyEqual (dst[0], 1.0f) && nearlyEqual (dst[1], 2.0f) && nearlyEqual (dst[2], 3.0f),
               "mixRoutedInput() with a single channel is a plain copy");
    }

    // --- mixRoutedInput(): out-of-range channels are silently dropped, -----
    // not read out of bounds
    {
        juce::AudioBuffer<float> input (2, 2);
        input.getWritePointer (0)[0] = 10.0f;
        input.getWritePointer (0)[1] = 20.0f;
        input.getWritePointer (1)[0] = 1.0f;
        input.getWritePointer (1)[1] = 1.0f;

        float dst[2] = { 0.0f, 0.0f };
        // 5 and -1 are both out of range for numInputChannels == 2 -- must
        // not crash, and must not contribute anything.
        InputRouting::mixRoutedInput (dst, input, { 0, 5, -1 }, 2, 2);
        check (nearlyEqual (dst[0], 10.0f) && nearlyEqual (dst[1], 20.0f),
               "mixRoutedInput() drops out-of-range channel indices (5, -1) without crashing or contributing");
    }

    // --- mixRoutedInput(): empty channel list contributes nothing ----------
    {
        juce::AudioBuffer<float> input (1, 2);
        input.getWritePointer (0)[0] = 99.0f;
        input.getWritePointer (0)[1] = 99.0f;

        float dst[2] = { 3.0f, 4.0f };
        InputRouting::mixRoutedInput (dst, input, {}, 1, 2);
        check (nearlyEqual (dst[0], 3.0f) && nearlyEqual (dst[1], 4.0f),
               "mixRoutedInput() with an empty channel list leaves dst untouched (an inactive object's silent slot)");
    }

    // --- mixRoutedInput(): does NOT clear dst itself (adds on top) --------
    {
        juce::AudioBuffer<float> input (1, 2);
        input.getWritePointer (0)[0] = 1.0f;
        input.getWritePointer (0)[1] = 1.0f;

        float dst[2] = { 100.0f, 100.0f }; // pre-existing content, caller's responsibility to clear if needed
        InputRouting::mixRoutedInput (dst, input, { 0 }, 1, 2);
        check (nearlyEqual (dst[0], 101.0f) && nearlyEqual (dst[1], 101.0f),
               "mixRoutedInput() adds onto dst's existing content rather than clearing it first");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
