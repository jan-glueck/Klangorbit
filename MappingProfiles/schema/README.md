# Mapping profile format

Mapping profiles store controller-input-to-parameter bindings (which
canonical input source drives which `ParameterRegistry` parameter, and
in which paging bank) as JSON. Own `schemaVersion` field, a SEPARATE
counter from both the code version (`CHANGELOG.md`) and the scene-preset
schema (`Presets/schema/README.md`) -- a mapping profile is independent
of which scene happens to be loaded (the same gamepad-to-movement mapping
should work whether you're editing `orbit_pair_demo` or a brand-new
empty scene), and changes to the mapping format have nothing to do with
changes to the scene format, so the two version numbers must be free to
move independently. Same reasoning as `Presets/schema/README.md`'s own
"why separate from code versioning" section, applied to this second,
unrelated axis of versioning.

## Why mapping profiles are not part of the preset schema

Deliberately not folded into `Presets/schema/`'s own JSON, even though
both are "a JSON file describing configuration": a scene preset describes
what's being composed (objects, physics, grains); a mapping profile
describes how you're PHYSICALLY controlling the session (which stick
drives which parameter). Loading a different scene while keeping your
own gamepad mapping should just work, and loading a mapping profile
someone else made shouldn't silently overwrite or require re-authoring
your whole scene. Keeping them as two independent files (and two
independent schema-version counters) is what makes both of those true.

## Schema history

- **1** -- initial format (bindings list + modifier source id).

## Field reference (schemaVersion 1)

```jsonc
{
  "schemaVersion": 1,
  "name": "default",

  // Canonical input source id (see Source/CanonicalInput.h) that, when
  // currently held (value >= 0.5), activates bank 1 instead of bank 0 --
  // see MappingEngine::getCurrentBank(). Exactly two banks; this is the
  // one input source reserved for paging and can't itself be bound to a
  // parameter.
  "modifierSourceId": "Gamepad0.RightShoulder",

  "bindings": [
    {
      // Canonical input source id (Source/CanonicalInput.h's
      // CanonicalInputEvent::sourceId) -- e.g. "Gamepad0.LeftStick.X",
      // later "Midi0.CC1.Ch1", "OSC./orbit/x" once those drivers exist.
      "sourceId": "Gamepad0.RightStick.X",

      // ParameterRegistry::Descriptor::id (see Source/ParameterRegistry.h
      // and Source/PluginProcessor.cpp's buildParameterRegistry() for the
      // full list of what's registered) -- e.g. "selectedObject.mass",
      // "object.3.orbitRadius", "scene.roomSize".
      "parameterId": "selectedObject.orbitAngularSpeed",

      // 0 = active whenever modifierSourceId above is NOT held, 1 =
      // active only while it IS held (see MappingEngine::getCurrentBank()).
      "bank": 0
    }
  ]
}
```

Loading REPLACES the entire binding set (and the modifier source id) --
a profile describes a complete controller-mapping configuration, not a
diff against whatever was loaded before, same policy
`PresetManager::loadFromVar()` already established for scenes.

## Storage

- `MappingProfiles/factory/` -- checked-in, curated default mapping(s).
  Part of the repo, every change goes through normal commits.
- `MappingProfiles/user/` -- own, unpolished experiments. If a user
  mapping turns out to be a keeper, move it to `factory/` deliberately,
  same convention `Presets/user/`/`Presets/factory/` already established.

## Relation to `Presets/schema/`

Independent schemas, independent files, independent version counters --
see "Why mapping profiles are not part of the preset schema" above. A
mapping profile never appears inside a scene preset's own JSON, and vice
versa.
