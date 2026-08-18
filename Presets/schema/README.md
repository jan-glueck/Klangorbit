# Preset/scene format

Presets store a complete object configuration (positions, motion modes,
physics parameters, input assignment) as JSON. Own `schemaVersion` field,
INDEPENDENT of the app version (CHANGELOG.md) -- because the data format
changes less often than the code, and old presets should keep loading even
after code refactors.

## Why separate from code versioning

If `schemaVersion` stays unchanged, that guarantees: every preset with that
number loads with every app version that supports this schema. If the JSON
format changes (new required field, renamed key, etc.), `schemaVersion` is
bumped AND a migration function is added (`migrateSchemaV1toV2()` etc.) --
old presets should never be silently misinterpreted, but either migrated
correctly or rejected with a clear error message.

## Schema history

- **1** -- initial format (movement physics + acoustic propagation fields).
- **2** -- adds an optional per-object `grainCloud` block (see below). Every
  v2 addition has a default that reproduces v1 behavior exactly (an object
  with no `grainCloud` block has its cloud disabled, same as before this
  field existed), so `migrateSchemaV1toV2()` (`Source/PresetManager.cpp`) is
  a pure relabeling -- it does not need to invent or transform any values.
  The version was still bumped (rather than treating this as an ordinary
  additive v1 field) to flag GrainCloud support explicitly in the schema
  history. `PresetManager::loadFromVar()` accepts schemaVersion 1..2 and
  migrates 1 -> 2 internally before parsing; anything outside that range is
  rejected with a clear error, never silently misinterpreted.

## Field reference (schemaVersion 2)

All fields except the ones marked "required" are optional -- if missing,
the code default applies (see `Source/SoundObject.h`/`Source/SceneSettings.h`/
`Source/Grain.h`). This is deliberate: new optional fields with a sensible
default are NOT a reason for a schemaVersion bump (see "Why separate from
code versioning" above) -- old presets like `orbit_pair_demo.json` keep
loading unchanged (migrated from schemaVersion 1), they simply use the
defaults for everything new.

```jsonc
{
  "schemaVersion": 2,
  "name": "orbit_pair_demo",

  // Optional, scene-wide parameters. If the block is missing entirely, the
  // SceneSettings defaults apply.
  "scene": {
    "roomSize": 5.0,                  // meters, radius of the spherical boundary; <= 0 = no boundary
    "boundaryBehavior": "reflect",    // reflect | wrap | absorb
    "showRoomBoundary": true,         // purely visual -- the boundary still applies physically even when hidden
    "globalField": [0.0, 0.0, 0.0],   // constant force/mass (like wind/gravity), only affects impulse/attracted
    "timeScale": 1.0,                 // fast-forward (>1) / slow-motion (<1) for the whole simulation

    // Acoustic propagation (medium properties, see PropagationProcessor)
    "speedOfSound": 343.0,            // m/s. Deliberately independent of temperature -- see below
    "temperature": 20.0,              // Celsius, only feeds the air-absorption model, not speedOfSound
    "relativeHumidity": 50.0,         // percent, 0..100
    "atmosphericPressure": 101.325,   // kPa, minor effect in the simplified absorption model
    "windVector": [0.0, 0.0, 0.0]     // m/s, shifts the effective speed of sound directionally
  },

  "objects": [
    {
      "id": 0,                        // required, 0-based
      "inputChannel": 0,
      "position": [1.0, 0.0, 0.0],    // required, x=front, y=left, z=up, meters
      "mode": "orbit",                // required, static | manual | orbit | impulse | attracted

      "orbitCenter": [0.0, 0.0, 0.0],
      "orbitRadius": 1.5,
      "orbitAngularSpeed": 0.8,       // rad/s
      "attractionStrength": 0.0,      // negative = repulsive
      "mass": 1.0,
      "damping": 0.02,
      "gain": 1.0,
      "muted": false,                 // own mute always wins over solo; applies to this object's GrainCloud too
      "soloed": false,                // if ANY object is soloed, every non-soloed object goes silent (non-exclusive, several can be soloed at once)

      // Inertia/motion limits
      "maxVelocity": 6.0,             // <= 0 = unlimited
      "dragCoefficient": 0.0,         // force-based, velocity-proportional brake, in addition to damping
      "restitution": 0.6,             // elasticity when bouncing off scene.roomSize (reflect mode), 0..1
      "velocitySnapThreshold": 0.01,  // velocity below this is hard-snapped to 0

      // n-body refinement (applies when THIS object acts as a source on others)
      "forceExponent": 2.0,           // 2 = classic inverse-square law
      "minDistance": 0.05,            // softening against hard force spikes at small distance
      "maxRange": 0.0,                // <= 0 = unlimited range, otherwise cutoff radius
      "attractionPulseRate": 0.0,     // Hz, 0 = no modulation of attractionStrength
      "attractionPulseDepth": 0.0,    // 0..1

      // Orbit extensions
      "orbitPlaneNormal": [0.0, 0.0, 1.0], // default = previous x/y plane
      "orbitEccentricity": 0.0,       // 0 = circle, <1 = ellipse (simplified approximation, not a real Kepler orbit)
      "orbitOrientation": 0.0,        // radians, rotation of the ellipse's major axis within the orbit plane; irrelevant when orbitEccentricity is 0
      "orbitDecay": 0.0,              // m/s, one-directional radius drift over time
      "orbitRadiusBaseline": 1.0,             // target value the mean-reverting radius wanders around (see below)
      "orbitRadiusReversionRate": 0.0,        // 0 = disabled; how strongly the radius is pulled back toward the baseline
      "orbitRadiusNoiseAmplitude": 0.0,       // 0 = disabled; random perturbation strength (Ornstein-Uhlenbeck process, m/sqrt(s))
      "orbitReferenceObjectId": -1,   // -1 = orbitCenter (fixed point), otherwise the id of another object

      // Acoustic propagation (Doppler, directivity -- see PropagationProcessor)
      "dopplerFactor": 1.0,           // 0 = no Doppler pitch shift, 1 = physically correct, >1 = exaggerated
      "dopplerSmoothing": 0.05,       // seconds, smooths the pitch effect against abrupt direction changes
      "directivityPattern": "omni",   // omni | cardioid | figure8
      "sourceOrientation": [1.0, 0.0, 0.0], // world-space direction the object "faces"; only used by cardioid/figure8

      // GrainCloud (schemaVersion 2+, see Source/Grain.h/GrainCloud.h). Optional --
      // a missing block means the cloud is disabled, same as a v1 preset.
      // Each active grain is a short-lived copy of this object's live input,
      // spawned from a ring buffer and given its own movement (see
      // "movementMode") and audio envelope; grains do NOT go through
      // PropagationProcessor (no per-grain delay/air absorption/directivity),
      // only AmbisonicsEncoder's spatial encoding + distance gain, plus an
      // optional simplified per-grain Doppler pitch shift (dopplerEnabled,
      // see Source/GrainDoppler.h -- much cheaper than PropagationProcessor's
      // delay-line-based Doppler, no per-sample cost, a single per-block
      // pitch ratio instead).
      "grainCloud": {
        "enabled": false,
        "dopplerEnabled": false,       // per-grain Doppler pitch shift, off by default (see above); uses this object's own dopplerFactor to scale strength
        "grainRate": 10.0,             // grains/sec, spawn rate while enabled
        "grainDuration": 0.15,         // seconds, both the audio envelope length AND the movement lifetime (single-shot grain model)
        "pitchJitter": 0.0,            // 0..1, random per-grain playback-rate variation
        "positionJitterInBuffer": 0.05,// 0..1 fraction of the ring buffer, randomizes the read start position
        "maxConcurrentGrains": 8,      // per-cloud cap; the effective cap is also limited by a global budget shared across all clouds (see Source/PluginProcessor.h::maxConcurrentGrainsGlobal)
        "windowShape": "hann",         // grain envelope shape; hann is currently the only option
        "movementMode": "randomWalk",  // randomWalk | bounce | radialExplosion | orbitAroundParent | attractRepelSiblings

        "randomWalkSpeed": 1.0,        // m/s, used by randomWalk
        "boundaryRadius": 1.0,         // meters, used by bounce (elastic reflection around the spawn position)
        "restitution": 0.6,            // 0..1, elasticity for bounce
        "initialSpeed": 2.0,           // m/s, used by radialExplosion
        "acceleration": 0.0,           // m/s^2, used by radialExplosion (outward)
        "orbitRadius": 0.5,            // meters, used by orbitAroundParent
        "orbitAngularSpeed": 2.0,      // rad/s, used by orbitAroundParent
        "attractionStrength": 1.0,     // used by attractRepelSiblings; negative = repulsive, n-body only within this cloud (see TrajectoryEngine::computeAttractionForce)

        "jitterTarget": "none",        // none | initialSpeed | lifetime | boundaryRadius | orbitRadius -- which field randomRange applies to
        "jitterRange": 0.0,            // +/- range applied to jitterTarget, in that field's own unit

        // How far into the ring buffer's past a grain's start point may be
        // drawn from, in seconds -- independent of and additive with
        // positionJitterInBuffer above (that one is a small de-clicking
        // offset near the write head; this is a deliberate, potentially
        // much larger reach into history). Both 0.0 (default) = disabled,
        // grain start = write head, same as before this field existed.
        // Bounded by Source/Grain.h::GrainLimits::maxGrainReadDepthRange
        // (10s), which is also what the ring buffer is sized to hold.
        "grainReadDepthRangeMin": 0.0,
        "grainReadDepthRangeMax": 0.0,
        "grainReadDepthDistribution": "uniform" // uniform | weightedTowardRecent | weightedTowardOld -- how the depth is sampled within the range
      }
    }
  ]
}
```

## Storage

- `Presets/factory/` -- checked-in, curated example scenes. These are part
  of the repo, every change goes through normal commits.
- `Presets/user/` -- own, unpolished experiments. NOT committed
  automatically (see .gitignore comment); if a user preset turns out good
  enough, deliberately move it to `factory/`.

## Relation to Docs/experiments/

Presets store ONLY the end state (starting configuration). If a preset
produced an interesting sound in combination with live input or manual
interaction that can't be reconstructed purely from the JSON file (e.g.
because you intervened live during playback), the description of that
finding belongs in an experiment log, not in the preset -- see
Docs/experiments/README.md.
