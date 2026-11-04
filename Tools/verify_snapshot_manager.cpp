#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/SnapshotManager.h"

/**
    Verifies SnapshotManager (feature/agitation-and-snapshots, Branch 5,
    Stage 3) against a small, hand-built ParameterRegistry (same minimal
    approach as verify_parameter_registry.cpp/verify_agitation_engine.cpp
    -- no KlangorbitProcessor needed): capture()/recall() scope filtering,
    the real JSON round-trip (snapshotToVar()/snapshotFromVar()), and the
    real file save/load path (saveFile()/loadFile()) against actual temp
    files on disk.
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

    // A small registry exercising every scope/category combination
    // capture() needs to tell apart: two categories, each with a
    // Global, a SelectedObject, AND a SpecificObject entry.
    ParameterRegistry buildTestRegistry (float& grainBacking, float& grainObjectBacking,
                                           float& delayBacking, float& sceneBacking)
    {
        ParameterRegistry registry;

        registry.registerParameter ({
            "selectedObject.grain.rate", "Grain Rate", "Grain Cloud", 0.0f, 100.0f, Polarity::Unipolar,
            Scope::SelectedObject, -1,
            [&grainBacking] { return grainBacking; }, [&grainBacking] (float v) { grainBacking = v; }
        });
        registry.registerParameter ({
            "object.0.grain.rate", "Grain Rate", "Grain Cloud", 0.0f, 100.0f, Polarity::Unipolar,
            Scope::SpecificObject, 0,
            [&grainObjectBacking] { return grainObjectBacking; }, [&grainObjectBacking] (float v) { grainObjectBacking = v; }
        });
        registry.registerParameter ({
            "selectedObject.delay.feedback", "Feedback", "Delay", 0.0f, 1.0f, Polarity::Unipolar,
            Scope::SelectedObject, -1,
            [&delayBacking] { return delayBacking; }, [&delayBacking] (float v) { delayBacking = v; }
        });
        registry.registerParameter ({
            "scene.roomSize", "Boundary Size", "Global", 0.0f, 50.0f, Polarity::Unipolar,
            Scope::Global, -1,
            [&sceneBacking] { return sceneBacking; }, [&sceneBacking] (float v) { sceneBacking = v; }
        });

        return registry;
    }
}

int main()
{
    // --- capture(): no category restriction captures every Global/ --------
    // SelectedObject entry, excludes SpecificObject
    {
        float grainBacking = 10.0f, grainObjectBacking = 999.0f, delayBacking = 0.3f, sceneBacking = 5.0f;
        auto registry = buildTestRegistry (grainBacking, grainObjectBacking, delayBacking, sceneBacking);

        auto snapshot = SnapshotManager::capture (registry, "test", {});
        check (snapshot.entries.size() == 3, "capture() with no category restriction captures exactly the 3 eligible entries");

        bool foundObjectSpecific = false;
        for (auto& e : snapshot.entries)
            if (e.id == "object.0.grain.rate")
                foundObjectSpecific = true;
        check (! foundObjectSpecific, "capture() excludes Scope::SpecificObject entries entirely");
    }

    // --- capture(): category restriction narrows the set ------------------
    {
        float grainBacking = 10.0f, grainObjectBacking = 999.0f, delayBacking = 0.3f, sceneBacking = 5.0f;
        auto registry = buildTestRegistry (grainBacking, grainObjectBacking, delayBacking, sceneBacking);

        auto snapshot = SnapshotManager::capture (registry, "grain only", { "Grain Cloud" });
        check (snapshot.entries.size() == 1, "capture() restricted to one category captures only that category's entries");
        check (! snapshot.entries.empty() && snapshot.entries[0].id == "selectedObject.grain.rate",
               "capture() restricted to 'Grain Cloud' captures the right entry");

        auto multiSnapshot = SnapshotManager::capture (registry, "two categories", { "Grain Cloud", "Delay" });
        check (multiSnapshot.entries.size() == 2, "capture() with two categories captures both");
    }

    // --- capture(): reads the CURRENT value, not a stale one ---------------
    {
        float grainBacking = 10.0f, grainObjectBacking = 0.0f, delayBacking = 0.0f, sceneBacking = 0.0f;
        auto registry = buildTestRegistry (grainBacking, grainObjectBacking, delayBacking, sceneBacking);
        grainBacking = 77.0f; // changed after the registry was built, before capture()

        auto snapshot = SnapshotManager::capture (registry, "test", { "Grain Cloud" });
        check (! snapshot.entries.empty() && nearlyEqual (snapshot.entries[0].value, 77.0f),
               "capture() reads the parameter's CURRENT value at capture time");
    }

    // --- recall(): writes every entry back through the registry -----------
    {
        float grainBacking = 10.0f, grainObjectBacking = 0.0f, delayBacking = 0.3f, sceneBacking = 5.0f;
        auto registry = buildTestRegistry (grainBacking, grainObjectBacking, delayBacking, sceneBacking);

        SnapshotManager::Snapshot snapshot;
        snapshot.entries = { { "selectedObject.grain.rate", 42.0f }, { "scene.roomSize", 12.0f } };

        SnapshotManager::recall (snapshot, registry);
        check (nearlyEqual (grainBacking, 42.0f), "recall() writes grain rate back through the registry");
        check (nearlyEqual (sceneBacking, 12.0f), "recall() writes roomSize back through the registry");
        check (nearlyEqual (delayBacking, 0.3f), "recall() leaves an entry NOT in the snapshot untouched");
    }

    // --- recall(): an id no longer in the registry is silently skipped ----
    {
        ParameterRegistry registry; // empty
        SnapshotManager::Snapshot snapshot;
        snapshot.entries = { { "ghost.param", 5.0f } };
        SnapshotManager::recall (snapshot, registry); // must not crash
        check (true, "recall() tolerates an entry whose id isn't found in the registry");
    }

    // --- snapshotToVar()/snapshotFromVar() round-trip ----------------------
    {
        SnapshotManager::Snapshot snapshot;
        snapshot.name = "my snapshot";
        snapshot.categories = { "Grain Cloud", "Delay" };
        snapshot.entries = { { "selectedObject.grain.rate", 33.5f }, { "scene.roomSize", 7.0f } };

        auto saved = SnapshotManager::snapshotToVar (snapshot);

        SnapshotManager::Snapshot reloaded;
        auto result = SnapshotManager::snapshotFromVar (saved, reloaded);
        check (result.wasOk(), "snapshotFromVar() accepts a well-formed snapshot var");
        check (reloaded.name == "my snapshot", "name round-trips");
        check (reloaded.categories.size() == 2 && reloaded.categories[0] == "Grain Cloud" && reloaded.categories[1] == "Delay",
               "categories round-trip");
        check (reloaded.entries.size() == 2, "entries count round-trips");
        check (reloaded.entries[0].id == "selectedObject.grain.rate" && nearlyEqual (reloaded.entries[0].value, 33.5f),
               "first entry's id/value round-trip exactly");
        check (reloaded.entries[1].id == "scene.roomSize" && nearlyEqual (reloaded.entries[1].value, 7.0f),
               "second entry's id/value round-trip exactly");
    }

    // --- snapshotFromVar(): schemaVersion / malformed-content rejection ----
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("schemaVersion", 999);
        root->setProperty ("name", "future");
        root->setProperty ("entries", juce::Array<juce::var>());
        SnapshotManager::Snapshot out;
        auto result = SnapshotManager::snapshotFromVar (juce::var (root), out);
        check (result.failed(), "snapshotFromVar() rejects an unsupported schemaVersion");
    }
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("schemaVersion", SnapshotManager::currentSchemaVersion);
        root->setProperty ("name", "no entries key");
        SnapshotManager::Snapshot out;
        auto result = SnapshotManager::snapshotFromVar (juce::var (root), out);
        check (result.failed(), "snapshotFromVar() rejects a snapshot missing its 'entries' array entirely");
    }
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("schemaVersion", SnapshotManager::currentSchemaVersion);
        root->setProperty ("name", "bad entry");
        juce::Array<juce::var> entries;
        auto* badEntry = new juce::DynamicObject();
        badEntry->setProperty ("value", 1.0);
        // no "id" set
        entries.add (juce::var (badEntry));
        root->setProperty ("entries", entries);
        SnapshotManager::Snapshot out;
        auto result = SnapshotManager::snapshotFromVar (juce::var (root), out);
        check (result.failed(), "snapshotFromVar() rejects an entry with a missing id");
    }

    // --- Real file save/load round-trip -------------------------------------
    {
        auto tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("klangorbit_verify_snapshot_manager_test.json");
        tempFile.deleteFile();

        SnapshotManager::Snapshot snapshot;
        snapshot.name = "file round-trip";
        snapshot.entries = { { "selectedObject.grain.rate", 99.0f } };

        auto saveResult = SnapshotManager::saveFile (tempFile, snapshot);
        check (saveResult.wasOk(), "saveFile() writes a snapshot to disk without error");
        check (tempFile.existsAsFile(), "saveFile() actually creates the file");

        SnapshotManager::Snapshot loaded;
        juce::String loadedName;
        auto loadResult = SnapshotManager::loadFile (tempFile, loaded, &loadedName);
        check (loadResult.wasOk(), "loadFile() reads the just-saved snapshot without error");
        check (loadedName == "file round-trip", "loadFile() reports the snapshot's own name");
        check (loaded.entries.size() == 1 && nearlyEqual (loaded.entries[0].value, 99.0f),
               "loadFile() round-trips the entry's value exactly");

        tempFile.deleteFile();

        auto missingResult = SnapshotManager::loadFile (tempFile, loaded);
        check (missingResult.failed(), "loadFile() fails cleanly for a file that doesn't exist");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
