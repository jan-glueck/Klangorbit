#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/PresetManager.h"

/**
    Round-trips AgitationEngine (feature/agitation-and-snapshots, Branch 5,
    Stage 2) through the REAL preset save/load path (PresetManager::
    sceneToVar()/loadFromVar(), both with their optional agitationEngine
    parameter), same convention as verify_delay_instance_preset.cpp for
    DelayInstanceSettings -- confirms every field survives exactly, that
    passing agitationEngine == nullptr leaves it completely untouched
    either way, and that loading REPLACES its entire contents rather than
    merging (including clearing stale entries when the loaded preset has
    no "agitation" block at all).

    Uses arbitrary id strings rather than real ParameterRegistry ids --
    persistence only ever deals with AgitationEngine's own id -> Settings
    map, entirely independent of whether any registry currently has a
    matching entry (see AgitationEngine::advanceAll()'s own "unknown id
    is silently skipped" handling, verified separately in
    verify_agitation_engine.cpp).
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
    // --- Round-trip a configured AgitationEngine ---------------------------
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);

        AgitationEngine agitation;
        auto& a = agitation.settingsFor ("object.0.mass");
        a.enabled = true;
        a.reversionRate = 1.5f;
        a.amount = 2.5f;
        a.smoothing = 0.3f;
        a.tempoSynced = false;

        auto& b = agitation.settingsFor ("scene.roomSize");
        b.enabled = false; // disabled but still configured -- must still round-trip
        b.reversionRate = 0.5f;
        b.amount = 1.0f;
        b.smoothing = 0.1f;
        b.tempoSynced = true;
        b.tempoDivision = Agitation::TempoDivision::EighthTriplet;

        auto saved = PresetManager::sceneToVar (engine, "agitation round-trip test", &agitation);

        TrajectoryEngine reloadedEngine (SAPOC_MAX_LIVE_INPUTS);
        AgitationEngine reloadedAgitation;
        auto result = PresetManager::loadFromVar (saved, reloadedEngine, &reloadedAgitation);
        check (result.wasOk(), "loadFromVar() accepts a scene containing a configured AgitationEngine without error");

        const auto* ra = reloadedAgitation.findSettings ("object.0.mass");
        check (ra != nullptr, "object.0.mass's Agitation entry round-trips (is found at all)");
        check (ra != nullptr && ra->enabled, "object.0.mass's enabled round-trips");
        check (ra != nullptr && nearlyEqual (ra->reversionRate, 1.5f), "object.0.mass's reversionRate round-trips");
        check (ra != nullptr && nearlyEqual (ra->amount, 2.5f), "object.0.mass's amount round-trips");
        check (ra != nullptr && nearlyEqual (ra->smoothing, 0.3f), "object.0.mass's smoothing round-trips");
        check (ra != nullptr && ! ra->tempoSynced, "object.0.mass's tempoSynced (false) round-trips");

        const auto* rb = reloadedAgitation.findSettings ("scene.roomSize");
        check (rb != nullptr, "scene.roomSize's Agitation entry round-trips even though enabled == false");
        check (rb != nullptr && ! rb->enabled, "scene.roomSize's enabled (false) round-trips");
        check (rb != nullptr && rb->tempoSynced, "scene.roomSize's tempoSynced (true) round-trips");
        check (rb != nullptr && rb->tempoDivision == Agitation::TempoDivision::EighthTriplet,
               "scene.roomSize's tempoDivision round-trips");
    }

    // --- agitationEngine == nullptr on save: no "agitation" block written --
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);
        auto saved = PresetManager::sceneToVar (engine, "no agitation engine passed");
        check (! saved.hasProperty ("agitation"), "sceneToVar() with agitationEngine == nullptr writes no 'agitation' key at all");
    }

    // --- agitationEngine == nullptr on load: left completely untouched -----
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);
        AgitationEngine agitation;
        agitation.settingsFor ("object.0.mass").enabled = true;
        auto saved = PresetManager::sceneToVar (engine, "test", &agitation);

        TrajectoryEngine reloadedEngine (SAPOC_MAX_LIVE_INPUTS);
        AgitationEngine reloadedAgitation;
        reloadedAgitation.settingsFor ("pre.existing.entry").enabled = true; // should survive untouched
        auto result = PresetManager::loadFromVar (saved, reloadedEngine, nullptr); // nullptr -- don't touch agitation
        check (result.wasOk(), "loadFromVar() with agitationEngine == nullptr still succeeds");
        check (reloadedAgitation.findSettings ("pre.existing.entry") != nullptr,
               "loadFromVar() with agitationEngine == nullptr leaves an unrelated AgitationEngine completely untouched");
    }

    // --- Loading REPLACES the entire contents, clearing stale entries ------
    // (a preset with NO "agitation" block at all, same as every preset
    // saved before this field existed, or saved with agitationEngine ==
    // nullptr -- must still CLEAR an already-configured AgitationEngine,
    // not leave it stuck with whatever was there before this load).
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);
        auto saved = PresetManager::sceneToVar (engine, "no agitation field at all"); // agitationEngine omitted -> no block

        TrajectoryEngine reloadedEngine (SAPOC_MAX_LIVE_INPUTS);
        AgitationEngine reloadedAgitation;
        reloadedAgitation.settingsFor ("stale.entry.from.before").enabled = true;
        auto result = PresetManager::loadFromVar (saved, reloadedEngine, &reloadedAgitation);
        check (result.wasOk(), "loadFromVar() accepts a scene with no 'agitation' key at all (pre-existing preset)");
        check (reloadedAgitation.findSettings ("stale.entry.from.before") == nullptr,
               "loading a preset with no 'agitation' block clears a previously-configured AgitationEngine");
    }

    // --- A second load replaces, rather than merges with, the first --------
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);

        AgitationEngine agitationA;
        agitationA.settingsFor ("only.in.a").enabled = true;
        auto savedA = PresetManager::sceneToVar (engine, "preset A", &agitationA);

        AgitationEngine agitationB;
        agitationB.settingsFor ("only.in.b").enabled = true;
        auto savedB = PresetManager::sceneToVar (engine, "preset B", &agitationB);

        AgitationEngine live;
        PresetManager::loadFromVar (savedA, engine, &live);
        check (live.findSettings ("only.in.a") != nullptr, "after loading preset A, its own entry is present");

        PresetManager::loadFromVar (savedB, engine, &live);
        check (live.findSettings ("only.in.b") != nullptr, "after loading preset B, its own entry is present");
        check (live.findSettings ("only.in.a") == nullptr,
               "after loading preset B, preset A's entry is gone (replace, not merge)");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
