#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/PresetManager.h"

/**
    Round-trips GrainCloudSettings::filter (VoiceFilterSettings, feature/
    grain-filter-integration) through the REAL preset save/load path
    (PresetManager::sceneToVar()/loadFromVar(), the same functions the
    plugin GUI uses) -- not a reimplementation of the JSON mapping, the
    actual code. Confirms every field (including the nested RandomizedParam
    cutoffHz/resonance and their array fields) survives exactly, and that a
    preset saved before this field existed still loads with the filter
    off (backward compatibility), see grainCloudSettingsFromVar()'s own
    comment in PresetManager.cpp.
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
    // --- Round-trip a filter configuration that exercises every field ------
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);

        auto& filter = engine.getGrainCloud (0).getSettings().filter;
        filter.enabled = true;
        filter.type = VoiceFilterType::BandPass;
        filter.cutoffHz.base = 1500.0f;
        filter.cutoffHz.spread = 300.0f;
        filter.cutoffHz.distribution = RandomDistribution::Normal;
        filter.cutoffHz.quantizeToSet = true;
        filter.cutoffHz.quantizeValues = { 500.0f, 1000.0f, 2000.0f };
        filter.resonance.base = 3.5f;
        filter.resonance.spread = 0.5f;
        filter.resonance.distribution = RandomDistribution::LogUniform;
        filter.driftReversionRate = 0.75f;
        filter.driftAmplitude = 200.0f;
        filter.driftSmoothing = 0.3f;

        auto saved = PresetManager::sceneToVar (engine, "filter round-trip test");

        TrajectoryEngine reloaded (SAPOC_MAX_LIVE_INPUTS);
        auto result = PresetManager::loadFromVar (saved, reloaded);
        check (result.wasOk(), "loadFromVar() accepts a scene containing a configured filter without error");

        auto& f = reloaded.getGrainCloud (0).getSettings().filter;
        check (f.enabled == true, "filter.enabled round-trips");
        check (f.type == VoiceFilterType::BandPass, "filter.type round-trips");
        check (nearlyEqual (f.cutoffHz.base, 1500.0f), "filter.cutoffHz.base round-trips");
        check (nearlyEqual (f.cutoffHz.spread, 300.0f), "filter.cutoffHz.spread round-trips");
        check (f.cutoffHz.distribution == RandomDistribution::Normal, "filter.cutoffHz.distribution round-trips");
        check (f.cutoffHz.quantizeToSet == true, "filter.cutoffHz.quantizeToSet round-trips");
        check (f.cutoffHz.quantizeValues.size() == 3
                   && nearlyEqual (f.cutoffHz.quantizeValues[0], 500.0f)
                   && nearlyEqual (f.cutoffHz.quantizeValues[1], 1000.0f)
                   && nearlyEqual (f.cutoffHz.quantizeValues[2], 2000.0f),
               "filter.cutoffHz.quantizeValues round-trips (array field)");
        check (nearlyEqual (f.resonance.base, 3.5f), "filter.resonance.base round-trips");
        check (nearlyEqual (f.resonance.spread, 0.5f), "filter.resonance.spread round-trips");
        check (f.resonance.distribution == RandomDistribution::LogUniform, "filter.resonance.distribution round-trips");
        check (nearlyEqual (f.driftReversionRate, 0.75f), "filter.driftReversionRate round-trips");
        check (nearlyEqual (f.driftAmplitude, 200.0f), "filter.driftAmplitude round-trips");
        check (nearlyEqual (f.driftSmoothing, 0.3f), "filter.driftSmoothing round-trips");
    }

    // --- Backward compatibility: a preset saved without "filter" at all ----
    // (every preset that existed before feature/grain-filter-integration)
    // must load with the filter off, not fail and not crash.
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);
        auto saved = PresetManager::sceneToVar (engine, "no filter field");

        // Simulate a pre-existing preset by stripping the "filter" key
        // GrainCloudSettingsToVar() would otherwise have written, exactly
        // like a real file saved before this field existed.
        if (auto* root = saved.getDynamicObject())
        {
            if (auto* objectsVar = root->getProperty ("objects").getArray())
            {
                for (auto& objVar : *objectsVar)
                    if (auto* gc = objVar.getProperty ("grainCloud", juce::var()).getDynamicObject())
                        gc->removeProperty ("filter");
            }
        }

        TrajectoryEngine reloaded (SAPOC_MAX_LIVE_INPUTS);
        auto result = PresetManager::loadFromVar (saved, reloaded);
        check (result.wasOk(), "loadFromVar() accepts a scene with no 'filter' key at all (pre-existing preset)");
        check (reloaded.getGrainCloud (0).getSettings().filter.enabled == false,
               "a preset with no 'filter' key loads with the filter off (backward compatible default)");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
