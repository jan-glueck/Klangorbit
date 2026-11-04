#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/PresetManager.h"

/**
    Round-trips GrainCloudSettings::generationMode/groupSize/groupStartDelay
    (feature/modifier-stack, Stage 4) through the REAL preset save/load
    path (PresetManager::sceneToVar()/loadFromVar()), same approach as
    verify_grain_filter_preset.cpp. Confirms Group mode's fields (including
    the nested RandomizedParam groupStartDelay) survive exactly, and that a
    preset saved before this field existed still loads as Emitter mode
    (backward compatibility), see grainCloudSettingsFromVar()'s own comment
    in PresetManager.cpp.
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
    // --- Round-trip a Group-mode configuration that exercises every field --
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);

        auto& gc = engine.getGrainCloud (0).getSettings();
        gc.generationMode = GrainGenerationMode::Group;
        gc.groupSize = 25;
        gc.groupStartDelay.base = 0.1f;
        gc.groupStartDelay.spread = 0.05f;
        gc.groupStartDelay.distribution = RandomDistribution::Normal;

        auto saved = PresetManager::sceneToVar (engine, "group mode round-trip test");

        TrajectoryEngine reloaded (SAPOC_MAX_LIVE_INPUTS);
        auto result = PresetManager::loadFromVar (saved, reloaded);
        check (result.wasOk(), "loadFromVar() accepts a scene containing Group mode without error");

        auto& r = reloaded.getGrainCloud (0).getSettings();
        check (r.generationMode == GrainGenerationMode::Group, "generationMode round-trips");
        check (r.groupSize == 25, "groupSize round-trips");
        check (nearlyEqual (r.groupStartDelay.base, 0.1f), "groupStartDelay.base round-trips");
        check (nearlyEqual (r.groupStartDelay.spread, 0.05f), "groupStartDelay.spread round-trips");
        check (r.groupStartDelay.distribution == RandomDistribution::Normal, "groupStartDelay.distribution round-trips");
    }

    // --- Backward compatibility: a preset saved without these keys at all --
    // (every preset that existed before this Stage 4 field) must load as
    // Emitter mode, not fail and not crash.
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);
        auto saved = PresetManager::sceneToVar (engine, "no generationMode field");

        // Simulate a pre-existing preset by stripping the keys
        // grainCloudSettingsToVar() would otherwise have written, exactly
        // like a real file saved before this field existed.
        if (auto* root = saved.getDynamicObject())
        {
            if (auto* objectsVar = root->getProperty ("objects").getArray())
            {
                for (auto& objVar : *objectsVar)
                    if (auto* gc = objVar.getProperty ("grainCloud", juce::var()).getDynamicObject())
                    {
                        gc->removeProperty ("generationMode");
                        gc->removeProperty ("groupSize");
                        gc->removeProperty ("groupStartDelay");
                    }
            }
        }

        TrajectoryEngine reloaded (SAPOC_MAX_LIVE_INPUTS);
        auto result = PresetManager::loadFromVar (saved, reloaded);
        check (result.wasOk(), "loadFromVar() accepts a scene with no generationMode/groupSize/groupStartDelay keys at all");
        check (reloaded.getGrainCloud (0).getSettings().generationMode == GrainGenerationMode::Emitter,
               "a preset with no 'generationMode' key loads as Emitter mode (backward compatible default)");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
