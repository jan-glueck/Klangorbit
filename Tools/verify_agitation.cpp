#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/Agitation.h"

/**
    Verifies Agitation.h (feature/agitation-and-snapshots, Branch 5,
    Stage 1 -- the generic per-parameter modulation engine, before it's
    wired into ParameterRegistry) directly, same approach as
    verify_voice_filter.cpp/verify_modifier_stack.cpp for their own
    header-only/pure-math modules: disabled-by-default no-op, enabled
    actually moves the value, mean-reversion keeps it from drifting away
    permanently, start() captures a fresh baseline, tempo-sync maths, and
    operator==/!=.
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

    bool nearlyEqual (float a, float b, float tolerance = 1.0e-4f)
    {
        return std::abs (a - b) <= tolerance;
    }
}

int main()
{
    // --- Disabled by default: a complete no-op --------------------------
    {
        Agitation::Settings settings; // enabled == false, amount == 0.0f
        Agitation::State state;
        Agitation::start (state, 10.0f);
        SeededRandom rng (1);

        float value = 10.0f;
        for (int i = 0; i < 100; ++i)
            value = Agitation::advance (settings, state, value, 0.01f, 120.0, rng);

        check (nearlyEqual (value, 10.0f), "advance() is a no-op while enabled == false (default)");
    }

    // --- Enabled but amount == 0: still a no-op -------------------------
    {
        Agitation::Settings settings;
        settings.enabled = true; // amount still defaults to 0.0f
        Agitation::State state;
        Agitation::start (state, 5.0f);
        SeededRandom rng (2);

        float value = 5.0f;
        for (int i = 0; i < 100; ++i)
            value = Agitation::advance (settings, state, value, 0.01f, 120.0, rng);

        check (nearlyEqual (value, 5.0f), "advance() is a no-op while amount == 0, even with enabled == true");
    }

    // --- Enabled with amount > 0: actually moves the value, but stays ----
    // bounded and keeps wandering around the captured baseline rather than
    // drifting permanently away from it (the whole point of reusing
    // MeanReverting::step() instead of unbounded random-walk noise).
    {
        Agitation::Settings settings;
        settings.enabled = true;
        settings.amount = 50.0f;
        settings.reversionRate = 2.0f;
        settings.smoothing = 0.05f;
        Agitation::State state;
        Agitation::start (state, 1000.0f);
        SeededRandom rng (3);

        float value = 1000.0f;
        bool everMoved = false;
        float maxDeviation = 0.0f;
        float sum = 0.0f;
        constexpr int numTicks = 20000;
        for (int i = 0; i < numTicks; ++i)
        {
            value = Agitation::advance (settings, state, value, 0.01f, 120.0, rng);
            if (! nearlyEqual (value, 1000.0f))
                everMoved = true;
            maxDeviation = juce::jmax (maxDeviation, std::abs (value - 1000.0f));
            sum += value;
        }
        const float average = sum / (float) numTicks;

        check (everMoved, "advance() with amount > 0 actually moves the value over time");
        // Loose statistical sanity bound, same spirit as verify_voice_filter.cpp's
        // own drift-stays-bounded check -- not a hard clamp (this engine has none,
        // the target parameter's own caller is responsible for that), just
        // confirming the process doesn't run away unboundedly.
        check (maxDeviation < settings.amount * 10.0f,
               "Mean-reverting Agitation stays within a sane multiple of its own amount, never runs away");
        check (nearlyEqual (average, 1000.0f, settings.amount * 0.5f),
               "Mean-reverting Agitation's long-run average stays close to the captured baseline, no permanent drift");
    }

    // --- start() re-captures the baseline fresh each call ----------------
    {
        Agitation::Settings settings;
        settings.enabled = true;
        settings.amount = 10.0f;
        Agitation::State state;
        Agitation::start (state, 100.0f);
        check (nearlyEqual (state.baseline, 100.0f), "start() captures currentValue as the new baseline");
        check (nearlyEqual (state.smoothedNoise, 0.0f), "start() resets smoothedNoise to 0");

        state.smoothedNoise = 42.0f; // simulate some prior wandering
        Agitation::start (state, 7.0f);
        check (nearlyEqual (state.baseline, 7.0f), "a second start() call re-captures a fresh baseline");
        check (nearlyEqual (state.smoothedNoise, 0.0f), "a second start() call resets smoothedNoise again");
    }

    // --- Tempo-sync maths -------------------------------------------------
    {
        // Quarter note at 120 BPM: 0.5s note length -> reversionRate 2.0 (1/0.5).
        check (nearlyEqual (Agitation::tempoDivisionToReversionRate (Agitation::TempoDivision::Quarter, 120.0), 2.0f),
               "Quarter note at 120 BPM resolves to reversionRate 2.0 (0.5s note length)");
        // Whole note at 120 BPM: 2s note length -> reversionRate 0.5.
        check (nearlyEqual (Agitation::tempoDivisionToReversionRate (Agitation::TempoDivision::Whole, 120.0), 0.5f),
               "Whole note at 120 BPM resolves to reversionRate 0.5 (2s note length)");
        // Eighth note at 120 BPM: 0.25s note length -> reversionRate 4.0.
        check (nearlyEqual (Agitation::tempoDivisionToReversionRate (Agitation::TempoDivision::Eighth, 120.0), 4.0f),
               "Eighth note at 120 BPM resolves to reversionRate 4.0 (0.25s note length)");
        // Quarter triplet at 120 BPM: (0.5s * 2/3) = 1/3 s note length -> reversionRate 3.0.
        check (nearlyEqual (Agitation::tempoDivisionToReversionRate (Agitation::TempoDivision::QuarterTriplet, 120.0), 3.0f, 1.0e-3f),
               "Quarter triplet at 120 BPM resolves to reversionRate 3.0 (1/3 s note length)");
        // Doubling the tempo halves the note length, doubling the rate.
        const float rate120 = Agitation::tempoDivisionToReversionRate (Agitation::TempoDivision::Quarter, 120.0);
        const float rate240 = Agitation::tempoDivisionToReversionRate (Agitation::TempoDivision::Quarter, 240.0);
        check (nearlyEqual (rate240, rate120 * 2.0f), "Doubling the tempo doubles the resolved reversionRate");
        // An invalid/unset tempo (<= 0) is clamped rather than dividing by zero.
        const float rateInvalid = Agitation::tempoDivisionToReversionRate (Agitation::TempoDivision::Quarter, 0.0);
        check (std::isfinite (rateInvalid) && rateInvalid > 0.0f,
               "An invalid (<= 0) bpm is clamped, never produces NaN/Inf/zero");
    }

    // --- tempoSynced actually overrides the fixed reversionRate ----------
    {
        Agitation::Settings settings;
        settings.enabled = true;
        settings.amount = 1.0f;
        settings.reversionRate = 0.001f; // deliberately tiny, so a non-tempo-synced
                                           // run would barely move at all in a few ticks
        settings.tempoSynced = true;
        settings.tempoDivision = Agitation::TempoDivision::Sixteenth; // fast at any normal tempo
        settings.smoothing = 0.0f; // no smoothing, so the first tick's noise is immediate
        Agitation::State state;
        Agitation::start (state, 0.0f);
        SeededRandom rng (4);

        float value = 0.0f;
        for (int i = 0; i < 50; ++i)
            value = Agitation::advance (settings, state, value, 0.01f, 140.0, rng);

        check (! nearlyEqual (value, 0.0f, 1.0e-6f),
               "tempoSynced == true uses the tempo-derived rate (fast at Sixteenth), not the tiny fixed reversionRate");
    }

    // --- operator==/!= ----------------------------------------------------
    {
        Agitation::Settings a;
        Agitation::Settings b;
        check (a == b, "two default-constructed Settings compare equal");
        b.amount = 1.0f;
        check (a != b, "changing one field makes Settings compare unequal");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
