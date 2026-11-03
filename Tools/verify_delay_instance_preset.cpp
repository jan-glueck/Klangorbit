#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/PresetManager.h"

/**
    Round-trips DelayInstanceSettings (feature/delay-instance) through the
    REAL preset save/load path (PresetManager::sceneToVar()/loadFromVar()),
    same convention as Tools/verify_grain_filter_preset.cpp for
    GrainCloudSettings::filter -- confirms every field survives exactly,
    and that a preset saved before this field existed still loads with
    the delay off (backward compatibility).
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
    // --- Round-trip a configured DelayInstance ------------------------------
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);

        auto& delay = engine.getDelayInstance (0).getSettings();
        delay.enabled = true;
        delay.numTaps = 5;
        delay.delayTime = 0.42f;
        delay.delayTimeJitter = 0.15f;
        delay.feedback = 0.6f;
        delay.feedbackDamping = 0.35f;
        delay.tapSpreadRadius = 2.5f;
        delay.tapOrbitAngularSpeed = 0.75f;
        delay.seed = 123456789;

        auto saved = PresetManager::sceneToVar (engine, "delay round-trip test");

        TrajectoryEngine reloaded (SAPOC_MAX_LIVE_INPUTS);
        auto result = PresetManager::loadFromVar (saved, reloaded);
        check (result.wasOk(), "loadFromVar() accepts a scene containing a configured DelayInstance without error");

        auto& d = reloaded.getDelayInstance (0).getSettings();
        check (d.enabled == true, "delayInstance.enabled round-trips");
        check (d.numTaps == 5, "delayInstance.numTaps round-trips");
        check (nearlyEqual (d.delayTime, 0.42f), "delayInstance.delayTime round-trips");
        check (nearlyEqual (d.delayTimeJitter, 0.15f), "delayInstance.delayTimeJitter round-trips");
        check (nearlyEqual (d.feedback, 0.6f), "delayInstance.feedback round-trips");
        check (nearlyEqual (d.feedbackDamping, 0.35f), "delayInstance.feedbackDamping round-trips");
        check (nearlyEqual (d.tapSpreadRadius, 2.5f), "delayInstance.tapSpreadRadius round-trips");
        check (nearlyEqual (d.tapOrbitAngularSpeed, 0.75f), "delayInstance.tapOrbitAngularSpeed round-trips");
        check (d.seed == 123456789, "delayInstance.seed round-trips exactly (string-encoded 64-bit value)");
    }

    // --- Backward compatibility: a preset saved without "delayInstance" ----
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);
        auto saved = PresetManager::sceneToVar (engine, "no delayInstance field");

        if (auto* root = saved.getDynamicObject())
        {
            if (auto* objectsVar = root->getProperty ("objects").getArray())
            {
                for (auto& objVar : *objectsVar)
                    if (auto* obj = objVar.getDynamicObject())
                        obj->removeProperty ("delayInstance");
            }
        }

        TrajectoryEngine reloaded (SAPOC_MAX_LIVE_INPUTS);
        auto result = PresetManager::loadFromVar (saved, reloaded);
        check (result.wasOk(), "loadFromVar() accepts a scene with no 'delayInstance' key at all (pre-existing preset)");
        check (reloaded.getDelayInstance (0).getSettings().enabled == false,
               "a preset with no 'delayInstance' key loads with the delay off (backward compatible default)");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
