#pragma once
#include <juce_core/juce_core.h>
#include "TrajectoryEngine.h"

/**
    Reads/writes scenes in the preset JSON format, see
    Presets/schema/README.md for the field reference.

    schemaVersion is checked: an unsupported schemaVersion is rejected with
    a clear error message instead of being silently misinterpreted
    (deliberate policy from Presets/schema/README.md). Once a schemaVersion
    2 exists, a migrateSchemaV1toV2()-style function goes here.

    Loading REPLACES the entire scene: objects that don't appear in the
    preset are reset to an inactive starting state -- a preset describes a
    complete configuration, not a diff against whatever was loaded before.

    Only call from the message thread (like TrajectoryEngine::getObject()).
*/
namespace PresetManager
{
    constexpr int currentSchemaVersion = 1;

    // Builds a var object from the current scene state (schemaVersion 1).
    // Inactive objects (inputChannel < 0) are not saved.
    juce::var sceneToVar (TrajectoryEngine& engine, const juce::String& name);

    // Applies a parsed preset var to the engine (replaces the scene).
    juce::Result loadFromVar (const juce::var& root, TrajectoryEngine& engine);

    // outName is set to the preset's "name" field on success (fallback: file name).
    juce::Result loadFile (const juce::File& file, TrajectoryEngine& engine, juce::String* outName = nullptr);
    juce::Result saveFile (const juce::File& file, TrajectoryEngine& engine, const juce::String& name);
}
