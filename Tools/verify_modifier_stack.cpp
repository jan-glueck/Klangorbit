#include <cmath>
#include <cstdio>
#include <vector>
#include <juce_core/juce_core.h>
#include "../Source/Modifier.h"

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

    Modifier makeGain (float base, float spread = 0.0f)
    {
        Modifier m;
        m.type = ModifierType::Gain;
        m.gain = { base, spread };
        return m;
    }

    Modifier makeTimeOffset (float base, float spread = 0.0f)
    {
        Modifier m;
        m.type = ModifierType::TimeOffset;
        m.timeOffset = { base, spread };
        return m;
    }

    Modifier makePitch (float base, float spread = 0.0f)
    {
        Modifier m;
        m.type = ModifierType::Pitch;
        m.pitchSemitones = { base, spread };
        return m;
    }

    Modifier makeFilter (float cutoff, float resonance = 0.707f)
    {
        Modifier m;
        m.type = ModifierType::Filter;
        m.filter.cutoffHz = { cutoff, 0.0f };
        m.filter.resonance = { resonance, 0.0f };
        return m;
    }
}

int main()
{
    // --- Empty/disabled stacks are a no-op -----------------------------
    {
        SeededRandom rng (1);
        auto out = ModifierStack::sample ({}, rng);
        check (out.gainMultiplier == 1.0f && out.timeOffsetSeconds == 0.0f && out.pitchSemitones == 0.0f
                   && out.filters.numSlots == 0,
               "sample(): an empty stack produces the fully-neutral default Outcome");
    }
    {
        std::vector<Modifier> stack { makeGain (2.0f), makeTimeOffset (0.5f), makePitch (7.0f), makeFilter (1000.0f) };
        for (auto& m : stack) m.enabled = false;
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (out.gainMultiplier == 1.0f && out.timeOffsetSeconds == 0.0f && out.pitchSemitones == 0.0f
                   && out.filters.numSlots == 0,
               "sample(): every slot disabled behaves exactly like an empty stack");
    }

    // --- Gain/TimeOffset/Pitch aggregate correctly ----------------------
    {
        std::vector<Modifier> stack { makeGain (2.0f), makeGain (0.5f), makeGain (3.0f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (std::abs (out.gainMultiplier - 3.0f) < 1.0e-4f, "sample(): multiple Gain slots multiply together (2.0 * 0.5 * 3.0 == 3.0)");
    }
    {
        std::vector<Modifier> stack { makeTimeOffset (0.1f), makeTimeOffset (-0.03f), makeTimeOffset (0.2f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (std::abs (out.timeOffsetSeconds - 0.27f) < 1.0e-4f, "sample(): multiple TimeOffset slots sum (0.1 - 0.03 + 0.2 == 0.27)");
    }
    {
        std::vector<Modifier> stack { makePitch (12.0f), makePitch (-5.0f), makePitch (2.0f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (std::abs (out.pitchSemitones - 9.0f) < 1.0e-4f, "sample(): multiple Pitch slots sum (12 - 5 + 2 == 9)");
    }
    {
        // A disabled slot in the middle of several enabled ones of the
        // same type must simply be skipped, not break the aggregation.
        std::vector<Modifier> stack { makeGain (2.0f), makeGain (100.0f), makeGain (3.0f) };
        stack[1].enabled = false;
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (std::abs (out.gainMultiplier - 6.0f) < 1.0e-4f, "sample(): a disabled slot in the middle of the stack is skipped, not zeroed-into the aggregate");
    }

    // --- Filter slots: order, sampling, overflow ------------------------
    {
        std::vector<Modifier> stack { makeFilter (500.0f), makeFilter (2000.0f), makeFilter (8000.0f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (out.filters.numSlots == 3, "sample(): 3 Filter slots produce exactly 3 entries in FilterBankState");
        check (std::abs (out.filters.cutoffHz[0] - 500.0f) < 1.0e-3f
                   && std::abs (out.filters.cutoffHz[1] - 2000.0f) < 1.0e-3f
                   && std::abs (out.filters.cutoffHz[2] - 8000.0f) < 1.0e-3f,
               "sample(): Filter slots are captured in STACK ORDER (spread=0 reproduces each one's own base cutoff exactly)");
    }
    {
        // Gain/TimeOffset/Pitch slots interleaved between Filter slots
        // must not shift the Filter slots' own indexing.
        std::vector<Modifier> stack { makeFilter (1000.0f), makeGain (2.0f), makeFilter (3000.0f), makePitch (5.0f), makeFilter (6000.0f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (out.filters.numSlots == 3
                   && std::abs (out.filters.cutoffHz[0] - 1000.0f) < 1.0f
                   && std::abs (out.filters.cutoffHz[1] - 3000.0f) < 1.0f
                   && std::abs (out.filters.cutoffHz[2] - 6000.0f) < 1.0f,
               "sample(): non-Filter slots interleaved in the stack don't disturb Filter slots' own 0..2 indexing");
    }
    {
        std::vector<Modifier> stack;
        for (int i = 0; i < ModifierLimits::maxModifiersPerStack + 3; ++i)
            stack.push_back (makeFilter (1000.0f + 100.0f * (float) i));
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (out.filters.numSlots == ModifierLimits::maxModifiersPerStack,
               "sample(): more Filter slots than ModifierLimits::maxModifiersPerStack silently caps at the limit, never overflows the fixed-size array");
    }

    // --- Reproducibility -------------------------------------------------
    {
        std::vector<Modifier> stack { makeGain (1.0f, 0.5f), makeTimeOffset (0.0f, 0.1f), makePitch (0.0f, 12.0f), makeFilter (1000.0f) };
        stack[3].filter.cutoffHz.spread = 500.0f;

        SeededRandom rngA (42);
        auto outA = ModifierStack::sample (stack, rngA);
        SeededRandom rngB (42);
        auto outB = ModifierStack::sample (stack, rngB);

        check (outA.gainMultiplier == outB.gainMultiplier
                   && outA.timeOffsetSeconds == outB.timeOffsetSeconds
                   && outA.pitchSemitones == outB.pitchSemitones
                   && outA.filters.cutoffHz[0] == outB.filters.cutoffHz[0],
               "sample(): the same seed produces an identical Outcome for a stack with randomized spreads");
    }

    // --- Drift ------------------------------------------------------------
    {
        std::vector<Modifier> stack { makeFilter (1000.0f) }; // driftAmplitude defaults to 0 (disabled)
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        const float before = out.filters.cutoffHz[0];
        for (int i = 0; i < 50; ++i)
            ModifierStack::advanceFilterDrift (stack, 0.01f, rng, out.filters, (float) kSampleRate);
        check (out.filters.cutoffHz[0] == before, "advanceFilterDrift(): a slot with driftAmplitude == 0 (the default) never moves");
    }
    {
        std::vector<Modifier> stack { makeFilter (1000.0f) };
        stack[0].filter.driftAmplitude = 400.0f;
        stack[0].filter.driftReversionRate = 0.5f;
        stack[0].filter.driftSmoothing = 0.1f;

        SeededRandom rng (9);
        auto out = ModifierStack::sample (stack, rng);
        const float before = out.filters.cutoffHz[0];

        bool everMoved = false;
        bool everOutOfRange = false;
        for (int i = 0; i < 2000; ++i)
        {
            ModifierStack::advanceFilterDrift (stack, 0.01f, rng, out.filters, (float) kSampleRate);
            const float now = out.filters.cutoffHz[0];
            if (now != before) everMoved = true;
            if (now < VoiceFilterLimits::minCutoffHz || now > (float) kSampleRate * VoiceFilterLimits::maxCutoffRatioOfSampleRate)
                everOutOfRange = true;
        }
        check (everMoved, "advanceFilterDrift(): a slot with driftAmplitude > 0 actually moves its cutoff over time");
        check (! everOutOfRange, "advanceFilterDrift(): a drifting slot's cutoff never leaves VoiceFilterLimits' safe range");
    }
    {
        // Multiple Filter slots interleaved with non-Filter slots: drift
        // must still land on the correct slot index, same guard as the
        // sample()-side indexing test above.
        std::vector<Modifier> stack { makeFilter (1000.0f), makeGain (1.0f), makeFilter (5000.0f) };
        stack[0].filter.driftAmplitude = 0.0f;   // slot 0: static
        stack[2].filter.driftAmplitude = 400.0f; // slot 1 (of filters): drifting

        SeededRandom rng (3);
        auto out = ModifierStack::sample (stack, rng);
        const float before0 = out.filters.cutoffHz[0];
        const float before1 = out.filters.cutoffHz[1];

        for (int i = 0; i < 500; ++i)
            ModifierStack::advanceFilterDrift (stack, 0.01f, rng, out.filters, (float) kSampleRate);

        check (out.filters.cutoffHz[0] == before0, "advanceFilterDrift(): slot 0 (driftAmplitude == 0) stays static even while slot 1 drifts");
        check (out.filters.cutoffHz[1] != before1, "advanceFilterDrift(): slot 1 (driftAmplitude > 0) drifts independently of slot 0, matched by correct index despite the interleaved Gain slot");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
