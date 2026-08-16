#pragma once
#include <juce_core/juce_core.h>
#include "TrajectoryEngine.h"

/**
    Reads/writes scenes in the preset JSON format, see
    Presets/schema/README.md for the field reference.

    schemaVersion is checked: an unsupported schemaVersion is rejected with
    a clear error message instead of being silently misinterpreted
    (deliberate policy from Presets/schema/README.md). schemaVersion 1
    presets are accepted and migrated to 2 (see migrateSchemaV1toV2()) --
    the v2 additions (per-object GrainCloud settings) are all optional
    with sensible defaults, so migration is a straightforward accept, but
    the version is still bumped to flag GrainCloud support explicitly in
    the schema history rather than silently widening v1.

    Loading REPLACES the entire scene: objects that don't appear in the
    preset are reset to an inactive starting state -- a preset describes a
    complete configuration, not a diff against whatever was loaded before.

    Only call from the message thread (like TrajectoryEngine::getObject()).
*/
namespace PresetManager
{
    constexpr int currentSchemaVersion = 2;
    constexpr int oldestSupportedSchemaVersion = 1;

    // Builds a var object from the current scene state (current schemaVersion).
    // Inactive objects (inputChannel < 0) are not saved.
    juce::var sceneToVar (TrajectoryEngine& engine, const juce::String& name);

    // Returns a schemaVersion-2 var equivalent to the given schemaVersion-1
    // one. All v2 additions are optional/additive, so this only needs to
    // bump the version number -- exposed as a named function (rather than
    // just accepting v1 content in place) so there's an obvious place for
    // an actual field rename/transformation if a future version needs one.
    juce::var migrateSchemaV1toV2 (const juce::var& v1Root);

    // Applies a parsed preset var to the engine (replaces the scene).
    // Accepts schemaVersion oldestSupportedSchemaVersion..currentSchemaVersion,
    // migrating older ones internally first.
    juce::Result loadFromVar (const juce::var& root, TrajectoryEngine& engine);

    // outName is set to the preset's "name" field on success (fallback: file name).
    juce::Result loadFile (const juce::File& file, TrajectoryEngine& engine, juce::String* outName = nullptr);
    juce::Result saveFile (const juce::File& file, TrajectoryEngine& engine, const juce::String& name);
}
