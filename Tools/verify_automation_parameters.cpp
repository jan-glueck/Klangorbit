#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include "../Source/ParameterRegistry.h"
#include "../Source/AutomationParameterBridge.h"
#include "../Source/PresetManager.h"
#include "../Source/TrajectoryEngine.h"

// Exercises buildAutomationParameterGroups()/RegistryAutomationParameter
// (AutomationParameterBridge.h/.cpp) against synthetic ParameterRegistry
// descriptors -- no full juce::AudioProcessor/host context needed, see
// CMakeLists.txt's own comment on this target for why that's deliberate.

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    bool approxEqual (float a, float b, float tolerance = 1.0e-4f)
    {
        return std::abs (a - b) <= tolerance;
    }

    // Finds a parameter anywhere in `groups` (recursive) by its exact
    // juce::AudioProcessorParameter::getParameterID() string.
    // getParameterID() lives on juce::HostedAudioProcessorParameter (not the
    // plain AudioProcessorParameter base getParameters() itself returns) --
    // every parameter this file builds is actually a RegistryAutomationParameter
    // (RangedAudioParameter -> AudioProcessorParameterWithID ->
    // HostedAudioProcessorParameter), so this cast is always safe here.
    juce::HostedAudioProcessorParameter* findParameterById (
        const std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>>& groups, const juce::String& id)
    {
        for (auto& g : groups)
            for (auto* p : g->getParameters (true))
                if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (p))
                    if (hosted->getParameterID() == id)
                        return hosted;
        return nullptr;
    }

    int totalParameterCount (const std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>>& groups)
    {
        int count = 0;
        for (auto& g : groups)
            count += g->getParameters (true).size();
        return count;
    }

    // Finds a direct child subgroup of `group` by its exact name (not recursive).
    const juce::AudioProcessorParameterGroup* findSubgroupByName (const juce::AudioProcessorParameterGroup& group,
                                                                     const juce::String& name)
    {
        for (auto* sub : group.getSubgroups (false))
            if (sub->getName() == name)
                return sub;
        return nullptr;
    }

    const juce::AudioProcessorParameterGroup* findTopLevelGroupByName (
        const std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>>& groups, const juce::String& name)
    {
        for (auto& g : groups)
            if (g->getName() == name)
                return g.get();
        return nullptr;
    }
}

int main()
{
    using Polarity = ParameterRegistry::Polarity;
    using Scope = ParameterRegistry::Scope;

    ParameterRegistry registry;

    float obj0Mass = 1.0f, obj0Gain = 1.0f, obj1Mass = 2.0f, obj0GrainRate = 10.0f, roomSize = 5.0f, selectedMass = 1.0f;

    registry.registerParameter ({ "object.0.mass", "Mass", "Object Physics", 0.01f, 20.0f, Polarity::Unipolar,
                                   Scope::SpecificObject, 0,
                                   [&obj0Mass] { return obj0Mass; }, [&obj0Mass] (float v) { obj0Mass = v; } });
    registry.registerParameter ({ "object.0.gain", "Gain", "Object Physics", 0.0f, 2.0f, Polarity::Unipolar,
                                   Scope::SpecificObject, 0,
                                   [&obj0Gain] { return obj0Gain; }, [&obj0Gain] (float v) { obj0Gain = v; } });
    registry.registerParameter ({ "object.0.grain.grainRate", "Grain Rate", "Grain Cloud", 0.1f, 500.0f, Polarity::Unipolar,
                                   Scope::SpecificObject, 0,
                                   [&obj0GrainRate] { return obj0GrainRate; }, [&obj0GrainRate] (float v) { obj0GrainRate = v; } });
    registry.registerParameter ({ "object.1.mass", "Mass", "Object Physics", 0.01f, 20.0f, Polarity::Unipolar,
                                   Scope::SpecificObject, 1,
                                   [&obj1Mass] { return obj1Mass; }, [&obj1Mass] (float v) { obj1Mass = v; } });
    registry.registerParameter ({ "scene.roomSize", "Boundary Size", "Global", 0.0f, 50.0f, Polarity::Unipolar,
                                   Scope::Global, -1,
                                   [&roomSize] { return roomSize; }, [&roomSize] (float v) { roomSize = v; } });
    // Deliberately included to confirm this scope is EXCLUDED -- see below.
    registry.registerParameter ({ "selectedObject.mass", "Mass", "Object Physics", 0.01f, 20.0f, Polarity::Unipolar,
                                   Scope::SelectedObject, -1,
                                   [&selectedMass] { return selectedMass; }, [&selectedMass] (float v) { selectedMass = v; } });

    auto groups = buildAutomationParameterGroups (registry.all());

    // --- Top-level structure ------------------------------------------------
    check (groups.size() == 3, "buildAutomationParameterGroups: one top-level group per distinct objectId (0, 1) plus one Global group");
    check (findTopLevelGroupByName (groups, "Object 1") != nullptr, "top-level group \"Object 1\" (objectId 0, 1-based) exists");
    check (findTopLevelGroupByName (groups, "Object 2") != nullptr, "top-level group \"Object 2\" (objectId 1, 1-based) exists");
    check (findTopLevelGroupByName (groups, "Global") != nullptr, "top-level group \"Global\" exists");

    // --- Category subgrouping within one object -----------------------------
    if (auto* obj1Group = findTopLevelGroupByName (groups, "Object 1"))
    {
        auto* physics = findSubgroupByName (*obj1Group, "Object Physics");
        auto* grain = findSubgroupByName (*obj1Group, "Grain Cloud");
        check (physics != nullptr, "\"Object 1\" has an \"Object Physics\" subgroup");
        check (grain != nullptr, "\"Object 1\" has a \"Grain Cloud\" subgroup");
        check (physics != nullptr && physics->getParameters (false).size() == 2,
               "\"Object 1\" > \"Object Physics\" has exactly 2 parameters (Mass, Gain)");
        check (grain != nullptr && grain->getParameters (false).size() == 1,
               "\"Object 1\" > \"Grain Cloud\" has exactly 1 parameter (Grain Rate)");
    }
    else
    {
        check (false, "\"Object 1\" group exists (skipped subgroup checks)");
    }

    // --- Scope::SelectedObject is never bridged ------------------------------
    check (totalParameterCount (groups) == 5, "total bridged parameter count is 5 (6 registered minus the 1 SelectedObject entry)");
    check (findParameterById (groups, "selectedObject.mass") == nullptr,
           "a Scope::SelectedObject descriptor (\"selectedObject.mass\") produces NO bridged parameter anywhere in the tree");

    // --- Value bridging: get/set/range/default -------------------------------
    if (auto* massParam = findParameterById (groups, "object.0.mass"))
    {
        check (approxEqual (massParam->getValue(), (obj0Mass - 0.01f) / (20.0f - 0.01f)),
               "object.0.mass: getValue() (normalized) matches the backing value's position in [minValue, maxValue]");

        massParam->setValue (1.0f); // normalized max
        check (approxEqual (obj0Mass, 20.0f), "object.0.mass: setValue(1.0) (normalized max) writes 20.0 (maxValue) through to the backing field");

        massParam->setValue (0.0f); // normalized min
        check (approxEqual (obj0Mass, 0.01f), "object.0.mass: setValue(0.0) (normalized min) writes 0.01 (minValue) through to the backing field");

        // Default was captured at construction time, when obj0Mass was still 1.0.
        const float expectedDefaultNormalized = (1.0f - 0.01f) / (20.0f - 0.01f);
        check (approxEqual (massParam->getDefaultValue(), expectedDefaultNormalized),
               "object.0.mass: getDefaultValue() reflects the backing value AT CONSTRUCTION time (1.0), not any later setValue() call");
    }
    else
    {
        check (false, "object.0.mass parameter exists (skipped value-bridging checks)");
    }

    // --- Independent per-object identity: object.0.mass and object.1.mass
    //     are two DIFFERENT parameters, not accidentally sharing storage ---
    if (auto* obj1MassParam = findParameterById (groups, "object.1.mass"))
    {
        obj1MassParam->setValue (1.0f);
        check (approxEqual (obj1Mass, 20.0f), "object.1.mass: setValue() writes to object 1's own backing field");
        // obj0Mass was left at 0.01 (minValue) by the previous test block's
        // own last setValue() call -- unchanged here confirms independent storage.
        check (approxEqual (obj0Mass, 0.01f),
               "object.1.mass: setting it does NOT also change object 0's mass (independent storage)");
    }
    else
    {
        check (false, "object.1.mass parameter exists");
    }

    // --- getStateInformation()/setStateInformation() JSON-text round-trip --
    // Those two (PluginProcessor.cpp) are thin wrappers: PresetManager::
    // sceneToVar() -> juce::JSON::toString() -> (host stores the bytes) ->
    // juce::JSON::parse() -> PresetManager::loadFromVar(). PresetManager's
    // OWN round-trip correctness has its own dedicated coverage elsewhere;
    // this checks specifically the ONE thing getState/setState adds beyond
    // that -- the JSON-text (de)serialization layer -- by exercising the
    // exact same two calls, without needing a full KlangorbitProcessor.
    {
        TrajectoryEngine engine (2);
        engine.activateObject (0);
        engine.getObject (0).mass = 7.5f;
        engine.getObject (0).gain = 1.75f;

        const auto json = juce::JSON::toString (PresetManager::sceneToVar (engine, juce::String()));
        check (json.isNotEmpty(), "getStateInformation(): PresetManager::sceneToVar() -> JSON::toString() produces non-empty text");

        TrajectoryEngine restored (2);
        const auto result = PresetManager::loadFromVar (juce::JSON::parse (json), restored);
        check (result.wasOk(), "setStateInformation(): JSON::parse() -> PresetManager::loadFromVar() succeeds on state just saved");
        check (approxEqual (restored.getObject (0).mass, 7.5f), "state round-trip: Object 0's mass survives the JSON-text round-trip");
        check (approxEqual (restored.getObject (0).gain, 1.75f), "state round-trip: Object 0's gain survives the JSON-text round-trip");
        check (restored.getObject (0).inputChannel >= 0, "state round-trip: Object 0 is still active after restoring");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
