# Snapshot format

Snapshots store a whole captured parameter set (every `ParameterRegistry`
parameter's current value, optionally restricted to a chosen set of
registry categories) as JSON. Own `schemaVersion` field, a SEPARATE
counter from the code version (`CHANGELOG.md`), the scene-preset schema
(`Presets/schema/README.md`), AND the mapping-profile schema
(`MappingProfiles/schema/README.md`) -- a snapshot is independent of
which scene happens to be loaded and independent of how you're
physically controlling the session, and changes to the snapshot format
have nothing to do with changes to either of those, so all three
version numbers must be free to move independently. Same reasoning as
`MappingProfiles/schema/README.md`'s own "why mapping profiles are not
part of the preset schema" section, applied to this third, unrelated
axis of versioning.

## Why snapshots are not part of the preset schema (or the mapping-profile schema)

Three genuinely different concerns, each already given its own file:
a scene preset describes what's being composed (objects, physics,
grains); a mapping profile describes how you're PHYSICALLY controlling
the session (which stick drives which parameter); a snapshot describes
what every parameter's VALUE currently is, at one specific moment, as
a thing you can capture and recall independently of either of those --
recalling one shouldn't touch which objects exist, and shouldn't touch
your gamepad/MIDI/OSC bindings either. Keeping all three as independent
files (and independent schema-version counters) is what makes that
true.

## Schema history

- **1** -- initial format (name + categories + entries list).

## Field reference (schemaVersion 1)

```jsonc
{
  "schemaVersion": 1,
  "name": "my snapshot",

  // Which ParameterRegistry::Descriptor::category strings this
  // snapshot was captured with (see Source/ParameterRegistry.h and
  // Source/PluginProcessor.cpp's buildParameterRegistry() for the
  // full list of categories, e.g. "Grain Cloud", "Delay", "Orbit") --
  // an empty array means no restriction was applied at capture time
  // (every eligible parameter is included). Purely informational: a
  // future snapshot-browser UI can show "this one covers: Grain
  // Cloud, Delay" without reverse-engineering it from "entries" below
  // -- recalling a snapshot never consults this field, only "entries".
  "categories": ["Grain Cloud", "Delay"],

  // One entry per captured parameter -- ONLY ParameterRegistry::Scope::
  // Global and Scope::SelectedObject parameters are ever captured
  // (never the per-object Scope::SpecificObject variants -- same
  // "whichever object is currently selected" restriction MappingPanel's
  // own target-parameter picker and AgitationEditor's row list both
  // already apply, see Source/SnapshotManager.h's own comment).
  "entries": [
    {
      // ParameterRegistry::Descriptor::id, e.g. "selectedObject.mass",
      // "scene.roomSize", "selectedObject.grain.grainRate".
      "id": "selectedObject.grain.grainRate",
      // Always a plain float, in that parameter's own REAL (not
      // normalized) range -- same convention every other float field
      // in this project's own preset/profile JSON already uses.
      "value": 12.5
    }
  ]
}
```

Recalling a snapshot writes every entry's value back through
`ParameterRegistry::Descriptor::setValue()`, nothing more -- it does
NOT reset parameters that aren't in the `entries` list to anything (a
snapshot captured with a narrow category restriction only ever touches
that narrow set on recall, leaving everything else exactly as it was).
An entry whose id is no longer found in the current registry (shouldn't
normally happen) is silently skipped rather than failing the whole
recall.

## Storage

- `Snapshots/factory/` -- checked-in, curated default snapshot(s), if
  any ever ship. Part of the repo, every change goes through normal
  commits.
- `Snapshots/user/` -- own, unpolished experiments. If a user snapshot
  turns out to be a keeper, move it to `factory/` deliberately, same
  convention `Presets/user/`/`Presets/factory/` and `MappingProfiles/
  user/`/`MappingProfiles/factory/` already established.

## Relation to `Presets/schema/` and `MappingProfiles/schema/`

Three independent schemas, three independent files, three independent
version counters -- see "Why snapshots are not part of the preset
schema (or the mapping-profile schema)" above. A snapshot never appears
inside a scene preset's own JSON or a mapping profile's own JSON, and
vice versa in both directions.
