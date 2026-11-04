#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "ParameterRegistry.h"

/**
    Captures/recalls a named "Snapshot" -- a whole parameter set, saved
    and loaded as its own JSON file (feature/agitation-and-snapshots,
    Branch 5, Stage 3). Deliberately a SEPARATE schema from both
    Presets/schema/ (a scene preset) and MappingProfiles/schema/ (a
    controller-mapping profile) -- own schemaVersion, own file -- same
    "independent axis, independent versioning" reasoning
    MappingProfileManager.h's own class comment already gives for why
    IT is separate from scene presets; see Snapshots/schema/README.md
    for the full version of that argument applied to this third axis.
    A snapshot describes "what every parameter's VALUE currently is",
    orthogonal to both "what's being composed" (a scene preset) and
    "how you're physically controlling it" (a mapping profile) --
    loading one shouldn't touch the other two.

    Only ever captures/recalls Scope::Global and Scope::SelectedObject
    parameters (see ParameterRegistry.h) -- NOT the per-object
    Scope::SpecificObject variants, same restriction (and the same
    reasoning: this whole plugin's own UI/workflow is built around
    "whichever object is currently selected", not per-object-by-index
    authoring) MappingPanel's target-parameter picker and
    AgitationEditor's row list both already apply.

    capture()'s `categories` parameter is the "Geltungsbereich ueber die
    Kategorien des Parameter-Registers" the original spec asked for
    (e.g. "nur Grain-Cloud, nur Delay") -- an empty list means no
    restriction at all (every eligible parameter). The chosen
    categories are stored on the resulting Snapshot purely for display
    (e.g. a future snapshot-browser UI showing "this one covers: Grain
    Cloud, Delay") -- recall() itself never consults them, since the
    snapshot's own entries list already defines exactly which
    parameters it covers.
*/
namespace SnapshotManager
{
    constexpr int currentSchemaVersion = 1;

    struct Entry
    {
        juce::String id;    // ParameterRegistry::Descriptor::id
        float value = 0.0f;
    };

    struct Snapshot
    {
        juce::String name;
        std::vector<juce::String> categories; // see this namespace's own comment -- display-only
        std::vector<Entry> entries;
    };

    // Reads every CURRENT eligible parameter's value straight from
    // `registry` (via each Descriptor's own getValue()) -- a live
    // capture of "right now", not of some other point in time.
    Snapshot capture (const ParameterRegistry& registry, const juce::String& name,
                       const std::vector<juce::String>& categories);

    // Writes every captured entry back through `registry`'s own
    // setValue() -- an entry whose id is no longer found there
    // (shouldn't normally happen; registration is a one-shot pass
    // covering every possible slot, see ParameterRegistry.h) is
    // silently skipped, same tolerant handling AgitationEngine::
    // advanceAll() already applies to the identical situation.
    void recall (const Snapshot& snapshot, const ParameterRegistry& registry);

    juce::var snapshotToVar (const Snapshot& snapshot);
    juce::Result snapshotFromVar (const juce::var& root, Snapshot& out);

    // outName is set to the snapshot's own "name" field on success
    // (fallback: file name) -- same convention as PresetManager::
    // loadFile()/MappingProfileManager::loadFile().
    juce::Result loadFile (const juce::File& file, Snapshot& out, juce::String* outName = nullptr);
    juce::Result saveFile (const juce::File& file, const Snapshot& snapshot);
}
