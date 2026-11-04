#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/PresetManager.h"

/**
    Round-trips the modifier stack (feature/modifier-stack, Source/
    Modifier.h) through the REAL preset save/load path
    (PresetManager::sceneToVar()/loadFromVar()), for both
    GrainCloudSettings::modifiers and DelayInstanceSettings::modifiers --
    same convention as Tools/verify_grain_filter_preset.cpp /
    verify_delay_instance_preset.cpp. Confirms every field of a
    multi-slot, multi-type stack survives exactly (including stack
    ORDER), and that a preset saved before this field existed still
    loads with an empty stack (backward compatibility).
*/
namespace
{
    int g_failures = 0;

    void check (bool condition, const juce::String& description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description.toRawUTF8());
        if (! condition)
            ++g_failures;
    }

    bool nearlyEqual (float a, float b, float tolerance = 1.0e-4f)
    {
        return std::abs (a - b) <= tolerance;
    }

    std::vector<Modifier> makeTestStack()
    {
        Modifier gain;
        gain.enabled = true;
        gain.type = ModifierType::Gain;
        gain.gain = { 0.8f, 0.1f };

        Modifier timeOffset;
        timeOffset.enabled = false; // deliberately disabled -- confirms `enabled` itself round-trips
        timeOffset.type = ModifierType::TimeOffset;
        timeOffset.timeOffset = { 0.25f, 0.05f };
        timeOffset.timeOffset.distribution = RandomDistribution::Normal;

        Modifier filter;
        filter.enabled = true;
        filter.type = ModifierType::Filter;
        filter.filter.enabled = true;
        filter.filter.type = VoiceFilterType::HighPass;
        filter.filter.cutoffHz = { 800.0f, 200.0f };
        filter.filter.resonance = { 2.0f, 0.0f };
        filter.filter.driftAmplitude = 150.0f;

        Modifier pitch;
        pitch.enabled = true;
        pitch.type = ModifierType::Pitch;
        pitch.pitchSemitones = { -3.0f, 7.0f };
        pitch.pitchSemitones.quantizeToSet = true;
        pitch.pitchSemitones.quantizeValues = { -12.0f, 0.0f, 12.0f };

        Modifier movement;
        movement.enabled = true;
        movement.type = ModifierType::Movement;
        movement.movementShape = MovementModifierShape::Rotating;
        movement.movementSpeed = { 0.5f, 0.2f };
        movement.movementAcceleration = { 0.1f, 0.0f };

        Modifier spatialOffset;
        spatialOffset.enabled = true;
        spatialOffset.type = ModifierType::SpatialOffset;
        spatialOffset.spatialShape = SpatialDistributionShape::Cuboid;
        spatialOffset.spatialRadius = { 0.0f, 2.5f };

        return { gain, timeOffset, filter, pitch, movement, spatialOffset }; // order matters -- checked below
    }

    void checkStackRoundTrips (const std::vector<Modifier>& original, const std::vector<Modifier>& reloaded, const char* label)
    {
        check (reloaded.size() == original.size(), juce::String (label) + ": modifier count round-trips");
        if (reloaded.size() != original.size())
            return;

        bool allMatch = true;
        for (size_t i = 0; i < original.size(); ++i)
            if (reloaded[i] != original[i])
                allMatch = false;
        check (allMatch, juce::String (label) + ": every slot's every field round-trips exactly, IN STACK ORDER (Modifier::operator==)");
    }
}

int main()
{
    // --- Round-trip a configured stack on BOTH GrainCloud and DelayInstance ---
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);

        const auto stack = makeTestStack();
        engine.getGrainCloud (0).getSettings().modifiers = stack;
        engine.getDelayInstance (0).getSettings().modifiers = stack;

        auto saved = PresetManager::sceneToVar (engine, "modifier stack round-trip test");

        TrajectoryEngine reloaded (SAPOC_MAX_LIVE_INPUTS);
        auto result = PresetManager::loadFromVar (saved, reloaded);
        check (result.wasOk(), "loadFromVar() accepts a scene containing a multi-slot, multi-type modifier stack without error");

        checkStackRoundTrips (stack, reloaded.getGrainCloud (0).getSettings().modifiers, "GrainCloudSettings::modifiers");
        checkStackRoundTrips (stack, reloaded.getDelayInstance (0).getSettings().modifiers, "DelayInstanceSettings::modifiers");
    }

    // --- Backward compatibility: a preset saved without "modifiers" --------
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);
        auto saved = PresetManager::sceneToVar (engine, "no modifiers field");

        if (auto* root = saved.getDynamicObject())
        {
            if (auto* objectsVar = root->getProperty ("objects").getArray())
            {
                for (auto& objVar : *objectsVar)
                {
                    if (auto* gc = objVar.getProperty ("grainCloud", juce::var()).getDynamicObject())
                        gc->removeProperty ("modifiers");
                    if (auto* di = objVar.getProperty ("delayInstance", juce::var()).getDynamicObject())
                        di->removeProperty ("modifiers");
                }
            }
        }

        TrajectoryEngine reloaded (SAPOC_MAX_LIVE_INPUTS);
        auto result = PresetManager::loadFromVar (saved, reloaded);
        check (result.wasOk(), "loadFromVar() accepts a scene with no 'modifiers' key at all on either grainCloud or delayInstance (pre-existing preset)");
        check (reloaded.getGrainCloud (0).getSettings().modifiers.empty(),
               "a grainCloud with no 'modifiers' key loads with an empty stack (backward compatible default)");
        check (reloaded.getDelayInstance (0).getSettings().modifiers.empty(),
               "a delayInstance with no 'modifiers' key loads with an empty stack (backward compatible default)");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
