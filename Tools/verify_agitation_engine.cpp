#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/AgitationEngine.h"

/**
    Verifies AgitationEngine (feature/agitation-and-snapshots, Branch 5,
    Stage 2) against a small, hand-built ParameterRegistry (same minimal
    approach as verify_parameter_registry.cpp -- no KlangorbitProcessor
    needed, since AgitationEngine only ever touches a registry through
    its own public Descriptor::getValue()/setValue() interface).
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

    using Polarity = ParameterRegistry::Polarity;
    using Scope = ParameterRegistry::Scope;
}

int main()
{
    // --- settingsFor()/findSettings(): lazy creation, no false positives --
    {
        AgitationEngine engine;
        check (engine.findSettings ("never.touched") == nullptr,
               "findSettings() returns nullptr for a parameter id never accessed");

        auto& s = engine.settingsFor ("some.param");
        check (! s.enabled, "settingsFor() lazily creates a default (disabled) entry");
        s.enabled = true;
        s.amount = 5.0f;

        const auto* found = engine.findSettings ("some.param");
        check (found != nullptr && found->enabled && nearlyEqual (found->amount, 5.0f),
               "findSettings() sees the same entry settingsFor() just mutated");
    }

    // --- remove()/clear() --------------------------------------------------
    {
        AgitationEngine engine;
        engine.settingsFor ("a").enabled = true;
        engine.settingsFor ("b").enabled = true;
        engine.remove ("a");
        check (engine.findSettings ("a") == nullptr, "remove() erases that one entry");
        check (engine.findSettings ("b") != nullptr, "remove() leaves other entries untouched");
        engine.clear();
        check (engine.findSettings ("b") == nullptr, "clear() erases every entry");
    }

    // --- advanceAll(): disabled entries are untouched ----------------------
    {
        ParameterRegistry registry;
        float backing = 42.0f;
        registry.registerParameter ({
            "test.disabled", "Disabled", "Test", 0.0f, 100.0f, Polarity::Unipolar, Scope::Global, -1,
            [&backing] { return backing; },
            [&backing] (float v) { backing = v; }
        });

        AgitationEngine engine;
        engine.settingsFor ("test.disabled"); // touched, but left disabled (the default)
        SeededRandom rng (1);
        for (int i = 0; i < 100; ++i)
            engine.advanceAll (registry, 0.01f, 120.0, rng);

        check (nearlyEqual (backing, 42.0f), "advanceAll() leaves a disabled entry's backing value untouched");
    }

    // --- advanceAll(): an unknown id (no matching registry entry) is a ----
    // harmless no-op, not a crash.
    {
        ParameterRegistry registry; // empty -- nothing registered at all
        AgitationEngine engine;
        auto& s = engine.settingsFor ("ghost.param");
        s.enabled = true;
        s.amount = 10.0f;
        SeededRandom rng (2);
        for (int i = 0; i < 10; ++i)
            engine.advanceAll (registry, 0.01f, 120.0, rng); // must not crash
        check (true, "advanceAll() tolerates an entry whose id isn't found in the registry");
    }

    // --- advanceAll(): enabling actually modulates the backing value, -----
    // captures a baseline on the enable transition, and keeps wandering
    // around it rather than drifting away permanently.
    {
        ParameterRegistry registry;
        float backing = 1000.0f;
        registry.registerParameter ({
            "test.agitated", "Agitated", "Test", -1.0e6f, 1.0e6f, Polarity::Unipolar, Scope::Global, -1,
            [&backing] { return backing; },
            [&backing] (float v) { backing = v; }
        });

        AgitationEngine engine;
        auto& s = engine.settingsFor ("test.agitated");
        s.enabled = true;
        s.amount = 50.0f;
        s.reversionRate = 2.0f;
        s.smoothing = 0.05f;
        SeededRandom rng (3);

        bool everMoved = false;
        float maxDeviation = 0.0f;
        constexpr int numTicks = 20000;
        for (int i = 0; i < numTicks; ++i)
        {
            engine.advanceAll (registry, 0.01f, 120.0, rng);
            if (! nearlyEqual (backing, 1000.0f))
                everMoved = true;
            maxDeviation = juce::jmax (maxDeviation, std::abs (backing - 1000.0f));
        }

        check (everMoved, "advanceAll() with amount > 0 actually modulates the registry's backing value");
        check (maxDeviation < s.amount * 10.0f, "advanceAll()'s modulation stays within a sane multiple of its own amount");
    }

    // --- advanceAll(): the baseline is captured from the registry's own ---
    // CURRENT value at the moment of the enable transition, not from
    // whatever AgitationEngine happened to be default-constructed with.
    {
        ParameterRegistry registry;
        float backing = 777.0f;
        registry.registerParameter ({
            "test.baseline", "Baseline", "Test", -1.0e6f, 1.0e6f, Polarity::Unipolar, Scope::Global, -1,
            [&backing] { return backing; },
            [&backing] (float v) { backing = v; }
        });

        AgitationEngine engine;
        auto& s = engine.settingsFor ("test.baseline");
        s.amount = 1.0f; // enabled stays false for now -- shouldn't matter yet
        SeededRandom rng (4);
        engine.advanceAll (registry, 0.01f, 120.0, rng); // a no-op tick while disabled

        backing = 123.0f; // changed by something else entirely, e.g. a UI slider
        s.enabled = true;  // NOW it transitions false -> true
        engine.advanceAll (registry, 0.0f, 120.0, rng); // dt == 0 -> no noise/reversion movement yet, isolates start()'s own capture

        check (nearlyEqual (backing, 123.0f, 1.0f),
               "the enable transition captures its baseline from the CURRENT backing value (123), not a stale one (777)");
    }

    // --- advanceAll(): a disable -> re-enable cycle re-captures a fresh ---
    // baseline again (not stuck on the first one forever).
    {
        ParameterRegistry registry;
        float backing = 10.0f;
        registry.registerParameter ({
            "test.recapture", "Recapture", "Test", -1.0e6f, 1.0e6f, Polarity::Unipolar, Scope::Global, -1,
            [&backing] { return backing; },
            [&backing] (float v) { backing = v; }
        });

        AgitationEngine engine;
        auto& s = engine.settingsFor ("test.recapture");
        s.amount = 1.0f;
        SeededRandom rng (5);

        s.enabled = true;
        engine.advanceAll (registry, 0.0f, 120.0, rng); // captures baseline == 10

        s.enabled = false;
        engine.advanceAll (registry, 0.01f, 120.0, rng); // disabled -- no movement, no re-capture yet

        backing = 999.0f; // moved while disabled
        s.enabled = true;  // re-enable -- a fresh false -> true transition
        engine.advanceAll (registry, 0.0f, 120.0, rng); // dt == 0 isolates the capture again

        check (nearlyEqual (backing, 999.0f, 1.0f),
               "re-enabling after a disable re-captures a fresh baseline (999), not the original one (10)");
    }

    // --- advanceAll(): multiple independent entries don't interfere -------
    {
        ParameterRegistry registry;
        float backingA = 0.0f, backingB = 0.0f;
        registry.registerParameter ({
            "test.a", "A", "Test", -1.0e6f, 1.0e6f, Polarity::Unipolar, Scope::Global, -1,
            [&backingA] { return backingA; }, [&backingA] (float v) { backingA = v; }
        });
        registry.registerParameter ({
            "test.b", "B", "Test", -1.0e6f, 1.0e6f, Polarity::Unipolar, Scope::Global, -1,
            [&backingB] { return backingB; }, [&backingB] (float v) { backingB = v; }
        });

        AgitationEngine engine;
        engine.settingsFor ("test.a").enabled = true;
        engine.settingsFor ("test.a").amount = 10.0f;
        // "test.b" deliberately left untouched -- no entry at all.

        SeededRandom rng (6);
        for (int i = 0; i < 200; ++i)
            engine.advanceAll (registry, 0.01f, 120.0, rng);

        check (nearlyEqual (backingB, 0.0f), "an entirely unconfigured parameter is never touched by advanceAll()");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
