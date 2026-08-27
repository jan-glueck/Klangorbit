#pragma once
#include <juce_core/juce_core.h>
#include "MappingEngine.h"

/**
    Reads/writes controller-mapping bindings (MappingEngine's own
    MappingBinding list + its modifier source id) as their own JSON
    format, see MappingProfiles/schema/README.md for the field reference.

    Deliberately a SEPARATE schema from Presets/schema/, its own
    schemaVersion, own currentSchemaVersion counter -- a mapping profile
    (which controller input drives which parameter) is independent of
    which scene happens to be loaded, and versions on its own timeline;
    see MappingProfiles/schema/README.md for the full reasoning (mirrors
    Presets/schema/README.md's own "why separate from code versioning"
    section, applied to this second, unrelated axis of versioning).

    Loading REPLACES the entire binding set, same "a profile describes a
    complete configuration, not a diff" policy PresetManager's own
    loadFromVar() already established for scenes.

    Only call from the message thread (same as PresetManager/
    TrajectoryEngine's own access rules) -- MappingEngine::
    canonicalInputReceived() is also only ever invoked from that thread
    (see CanonicalInputHub's own comment: whichever thread a driver polls
    from, which for GamepadDriver is the processor's message-thread
    timer), so there's no cross-thread concern here either.
*/
namespace MappingProfileManager
{
    constexpr int currentSchemaVersion = 1;

    // Builds a var object from the engine's current bindings + modifier.
    juce::var profileToVar (const MappingEngine& engine, const juce::String& name);

    // Applies a parsed profile var to the engine (replaces all bindings).
    // Accepts schemaVersion 1 only for now -- no migration history yet.
    juce::Result loadFromVar (const juce::var& root, MappingEngine& engine);

    // outName is set to the profile's "name" field on success (fallback: file name).
    juce::Result loadFile (const juce::File& file, MappingEngine& engine, juce::String* outName = nullptr);
    juce::Result saveFile (const juce::File& file, const MappingEngine& engine, const juce::String& name);
}
