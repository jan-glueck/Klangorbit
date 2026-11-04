#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/SnapshotMorphEngine.h"

/**
    Verifies SnapshotMorphEngine (feature/agitation-and-snapshots,
    Branch 5, Stage 4) against a small, hand-built ParameterRegistry
    (same minimal approach as verify_agitation_engine.cpp): endpoint
    intersection (only ids common to both snapshots are morphed),
    smoothing toward the `morph` target, the morph=0/1 pure-endpoint
    cases, and clear()/hasEndpoints().
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

    bool nearlyEqual (float a, float b, float tolerance = 1.0e-3f)
    {
        return std::abs (a - b) <= tolerance;
    }

    using Polarity = ParameterRegistry::Polarity;
    using Scope = ParameterRegistry::Scope;
}

int main()
{
    // --- setEndpoints(): only ids common to BOTH snapshots are morphed ----
    {
        SnapshotManager::Snapshot a;
        a.entries = { { "shared.id", 0.0f }, { "only.in.a", 1.0f } };
        SnapshotManager::Snapshot b;
        b.entries = { { "shared.id", 100.0f }, { "only.in.b", 2.0f } };

        SnapshotMorphEngine engine;
        check (! engine.hasEndpoints(), "a freshly constructed engine has no endpoints");
        engine.setEndpoints (a, b);
        check (engine.hasEndpoints(), "setEndpoints() with overlapping entries leaves hasEndpoints() true");
        check (engine.getNumMorphedParameters() == 1, "setEndpoints() keeps only the one id present in BOTH snapshots");
    }

    // --- clear(): back to no-op ---------------------------------------------
    {
        SnapshotManager::Snapshot a;
        a.entries = { { "shared.id", 0.0f } };
        SnapshotManager::Snapshot b;
        b.entries = { { "shared.id", 100.0f } };

        SnapshotMorphEngine engine;
        engine.setEndpoints (a, b);
        engine.clear();
        check (! engine.hasEndpoints(), "clear() removes every endpoint");

        ParameterRegistry registry;
        float backing = 55.0f;
        registry.registerParameter ({
            "shared.id", "Shared", "Test", 0.0f, 100.0f, Polarity::Unipolar, Scope::Global, -1,
            [&backing] { return backing; }, [&backing] (float v) { backing = v; }
        });
        for (int i = 0; i < 10; ++i)
            engine.advance (registry, 0.01f);
        check (nearlyEqual (backing, 55.0f), "advance() after clear() is a complete no-op");
    }

    // --- advance(): morph == 0 settles on pure A, morph == 1 on pure B ----
    {
        ParameterRegistry registry;
        float backing = 0.0f;
        registry.registerParameter ({
            "shared.id", "Shared", "Test", 0.0f, 100.0f, Polarity::Unipolar, Scope::Global, -1,
            [&backing] { return backing; }, [&backing] (float v) { backing = v; }
        });

        SnapshotManager::Snapshot a;
        a.entries = { { "shared.id", 10.0f } };
        SnapshotManager::Snapshot b;
        b.entries = { { "shared.id", 90.0f } };

        SnapshotMorphEngine engine;
        engine.setEndpoints (a, b);
        engine.smoothing = 0.05f;
        engine.morph = 0.0f;
        for (int i = 0; i < 500; ++i)
            engine.advance (registry, 0.01f); // enough ticks to fully settle
        check (nearlyEqual (backing, 10.0f, 0.5f), "advance() settles on pure A's value when morph == 0");

        engine.morph = 1.0f;
        for (int i = 0; i < 500; ++i)
            engine.advance (registry, 0.01f);
        check (nearlyEqual (backing, 90.0f, 0.5f), "advance() settles on pure B's value when morph == 1");
    }

    // --- advance(): morph == 0.5 settles halfway between A and B ----------
    {
        ParameterRegistry registry;
        float backing = 0.0f;
        registry.registerParameter ({
            "shared.id", "Shared", "Test", 0.0f, 100.0f, Polarity::Unipolar, Scope::Global, -1,
            [&backing] { return backing; }, [&backing] (float v) { backing = v; }
        });

        SnapshotManager::Snapshot a;
        a.entries = { { "shared.id", 0.0f } };
        SnapshotManager::Snapshot b;
        b.entries = { { "shared.id", 100.0f } };

        SnapshotMorphEngine engine;
        engine.setEndpoints (a, b);
        engine.smoothing = 0.05f;
        engine.morph = 0.5f;
        for (int i = 0; i < 500; ++i)
            engine.advance (registry, 0.01f);
        check (nearlyEqual (backing, 50.0f, 0.5f), "advance() settles halfway between A and B at morph == 0.5");
    }

    // --- advance(): smoothing actually smooths (doesn't jump instantly) ---
    {
        ParameterRegistry registry;
        float backing = 0.0f;
        registry.registerParameter ({
            "shared.id", "Shared", "Test", 0.0f, 100.0f, Polarity::Unipolar, Scope::Global, -1,
            [&backing] { return backing; }, [&backing] (float v) { backing = v; }
        });

        SnapshotManager::Snapshot a;
        a.entries = { { "shared.id", 0.0f } };
        SnapshotManager::Snapshot b;
        b.entries = { { "shared.id", 100.0f } };

        SnapshotMorphEngine engine;
        engine.setEndpoints (a, b);
        engine.smoothing = 2.0f; // slow
        engine.morph = 1.0f; // jump straight to pure B as the target
        engine.advance (registry, 0.01f); // just one tick
        check (backing < 50.0f, "a single short tick with slow smoothing hasn't reached the far endpoint yet");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
