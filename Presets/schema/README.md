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
    "globalField": [0.0, 0.0, 0.0],   // constant force/mass (like wind/gravity), only affects impulse-mode objects
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
      "mode": "orbit",                // required, static | manual | orbit | impulse ("attracted" still accepted on read, maps to impulse -- identical physics, merged)
      "momentumEnabled": true,        // if false, releasing a mouse drag on this object always leaves it exactly where released (no throw), regardless of release speed

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
      "orbitRadiusNoiseSmoothing": 0.0,       // seconds, 0 = disabled (raw per-tick noise, unchanged behavior); low-pass-filters the noise source itself for a smoother "breathing" wander instead of a jagged one
      "orbitNoiseSeed": "123456789",  // string (not a JSON number, to round-trip a 64-bit value exactly) -- seeds this object's own orbit-radius noise source (see Source/RandomEngine.h), so a specific "breathing" pattern reloads exactly, not just approximately. Optional -- if missing, the object keeps whichever seed it was already assigned on (re)activation (see TrajectoryEngine::reseedOrbitNoise())
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
        "sourceMuted": false,          // mutes just this object's own dry signal, leaving its grains untouched ("Isolate Grains" in the UI) -- independent of the object's own muted/soloed
        "dopplerEnabled": false,       // per-grain Doppler pitch shift, off by default (see above); uses this object's own dopplerFactor to scale strength
        "grainRate": 10.0,             // grains/sec, spawn rate while enabled ("Grain Rate (Spawn Rate)" in the UI) -- independent of grainDuration at the scheduling level, see Source/Grain.h::GrainCloudSettings
        "grainRateJitter": 0.0,        // 0..1, random +/- variation of the spawn INTERVAL per spawn (not a per-grain property)
        "grainDuration": 0.15,         // seconds, both the audio envelope length AND the movement lifetime (single-shot grain model)
        "grainDurationJitter": 0.0,    // 0..1, random +/- variation of grainDuration per spawned grain
        "pitchJitter": 0.0,            // 0..1, random per-grain playback-rate variation
        "positionJitterInBuffer": 0.05,// 0..1 fraction of the ring buffer, randomizes the read start position
        "maxConcurrentGrains": 256,    // per-cloud cap; the effective cap is also limited by a global budget shared across all clouds (see Source/PluginProcessor.h::maxConcurrentGrainsGlobal, also 256). If grainRate*grainDuration exceeds this, the OLDEST active grain is voice-stolen (quick fade-out, slot reused) rather than stalling new spawns -- see GrainCloud::beginVoiceSteal()
        "windowShape": "hann",         // grain envelope shape; hann is currently the only option
        "movementMode": "randomWalk",  // randomWalk | bounce | radialExplosion | orbitAroundParent | attractRepelSiblings

        "randomWalkSpeed": 1.0,        // m/s, used by randomWalk
        "boundaryRadius": 1.0,         // meters, used by bounce (elastic reflection around the spawn position)
        "boundaryRadiusJitter": 0.0,   // 0..1, random +/- variation of boundaryRadius per spawned grain (bounce)
        "restitution": 0.6,            // 0..1, elasticity for bounce
        "initialSpeed": 2.0,           // m/s, used by radialExplosion
        "initialSpeedJitter": 0.0,     // 0..1, random +/- variation of initialSpeed per spawned grain (radialExplosion)
        "acceleration": 0.0,           // m/s^2, used by radialExplosion (outward)
        "orbitRadius": 0.5,            // meters, used by orbitAroundParent
        "orbitRadiusJitter": 0.0,      // 0..1, random +/- variation of orbitRadius per spawned grain (orbitAroundParent)
        "orbitAngularSpeed": 2.0,      // rad/s, used by orbitAroundParent
        "orbitSphereSpread": 0.0,      // 0..1, used by orbitAroundParent -- 0 = flat (all grains circle in the same horizontal plane), 1 = each grain's own orbit plane is essentially random (approaches a sphere over many grains/rotations); picked once per grain at spawn
        "attractionStrength": 1.0,     // used by attractRepelSiblings; negative = repulsive, n-body only within this cloud (see TrajectoryEngine::computeAttractionForce)

        // "jitterTarget"/"jitterRange" (a single shared, mutually-exclusive
        // pair) are LEGACY -- superseded by the dedicated *Jitter fields
        // above, which can all be used simultaneously. Still read (and
        // migrated onto the matching field above) when loading an older
        // preset that has them, but never written when saving.

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
        "grainReadDepthDistribution": "uniform", // uniform | weightedTowardRecent | weightedTowardOld -- how the depth is sampled within the range

        // String (not a JSON number, to round-trip a 64-bit value exactly) --
        // seeds this cloud's own randomization source (spawn timing, pitch/
        // position jitter, movement-mode randomization; see Source/
        // RandomEngine.h). Optional -- if missing, the cloud keeps whichever
        // seed it was already assigned on construction/reset (see
        // GrainCloud::reset()). Saved so a specific grain pattern reloads
        // exactly, not just approximately.
        "seed": "987654321",

        // Optional -- missing entirely (every preset saved before
        // feature/grain-filter-integration existed) behaves exactly like
        // "enabled": false below, so an older preset's sound is
        // unaffected. Per-grain multi-mode filter (Source/VoiceFilter.h)
        // applied to each grain's own rendered audio independently.
        "filter": {
          "enabled": false,
          "type": "lowPass",           // lowPass | highPass | bandPass | notch
          // RandomizedParam (Source/RandomEngine.h) -- each grain samples
          // its OWN cutoff once at spawn from [base-spread, base+spread]
          // (or base/spread as a ratio for "logUniform", see
          // RandomEngine.h), same "decided once" treatment as pitchJitter.
          "cutoffHz": {
            "base": 2000.0,
            "spread": 0.0,
            "distribution": "uniform", // uniform | normal | ramp | logUniform | powerLaw | discrete
            "rampTowardMax": true,     // ramp only
            "powerExponent": 2.0,      // powerLaw only
            "discreteValues": [],      // discrete only
            "quantizeToSet": false,    // snaps the sampled value to the nearest entry in quantizeValues afterward, any distribution
            "quantizeValues": []
          },
          "resonance": {               // same RandomizedParam shape as cutoffHz, for Q
            "base": 0.707,
            "spread": 0.0,
            "distribution": "uniform",
            "rampTowardMax": true,
            "powerExponent": 2.0,
            "discreteValues": [],
            "quantizeToSet": false,
            "quantizeValues": []
          },
          // Optional continuous wander of a grain's cutoff around its own
          // spawn-time sampled value, for as long as that grain lives
          // (Ornstein-Uhlenbeck, Source/RandomEngine.h::MeanReverting) --
          // driftAmplitude == 0 (the default) disables this entirely.
          "driftReversionRate": 1.0,   // 1/s, pull-back strength toward the grain's own sampled cutoff
          "driftAmplitude": 0.0,       // Hz, noise strength -- 0 = disabled
          "driftSmoothing": 0.2        // seconds, smooths the underlying noise
        },

        // Optional -- missing entirely (every preset saved before
        // feature/modifier-stack existed) is the same as an empty array.
        // An ORDERED, stackable list (Source/Modifier.h) applied ON TOP
        // OF everything above -- coexists with, doesn't replace, the
        // dedicated "filter" block above. Each entry's "type" selects
        // which of gain/timeOffset/filter/pitchSemitones/movement* is
        // actually read; the others are still present (same "struct
        // holds every variant's fields" convention as elsewhere) but
        // ignored. "filter" here is the exact same shape as the
        // dedicated "filter" block above; "gain"/"timeOffset"/
        // "pitchSemitones"/"movementSpeed"/"movementAcceleration" are
        // each a RandomizedParam, same shape as "filter.cutoffHz" above
        // (base/spread/distribution/...). Order only matters between
        // multiple "filter"-type entries, and between/among "rotating"
        // "movement"-type entries or one mixed with anything else that
        // moves the voice (see Modifier.h's own comment).
        "modifiers": [
          {
            "enabled": true,
            "type": "gain",            // gain | timeOffset | filter | pitch | movement ("pitch" not offered for delayInstance, see below)
            "gain": { "base": 1.0, "spread": 0.0, "distribution": "uniform", "rampTowardMax": true, "powerExponent": 2.0, "discreteValues": [], "quantizeToSet": false, "quantizeValues": [] },
            "timeOffset": { "base": 0.0, "spread": 0.0, "distribution": "uniform", "rampTowardMax": true, "powerExponent": 2.0, "discreteValues": [], "quantizeToSet": false, "quantizeValues": [] },
            "filter": { "enabled": false, "type": "lowPass", "cutoffHz": { "base": 2000.0, "spread": 0.0, "distribution": "uniform", "rampTowardMax": true, "powerExponent": 2.0, "discreteValues": [], "quantizeToSet": false, "quantizeValues": [] }, "resonance": { "base": 0.707, "spread": 0.0, "distribution": "uniform", "rampTowardMax": true, "powerExponent": 2.0, "discreteValues": [], "quantizeToSet": false, "quantizeValues": [] }, "driftReversionRate": 1.0, "driftAmplitude": 0.0, "driftSmoothing": 0.2 },
            "pitchSemitones": { "base": 0.0, "spread": 0.0, "distribution": "uniform", "rampTowardMax": true, "powerExponent": 2.0, "discreteValues": [], "quantizeToSet": false, "quantizeValues": [] },
            "movementShape": "linear",   // linear | rotating -- linear: movementSpeed/-Acceleration are m/s, m/s^2 along a random direction picked once per voice; rotating: rad/s, rad/s^2 around a random axis picked once per voice
            "movementSpeed": { "base": 0.0, "spread": 0.0, "distribution": "uniform", "rampTowardMax": true, "powerExponent": 2.0, "discreteValues": [], "quantizeToSet": false, "quantizeValues": [] },
            "movementAcceleration": { "base": 0.0, "spread": 0.0, "distribution": "uniform", "rampTowardMax": true, "powerExponent": 2.0, "discreteValues": [], "quantizeToSet": false, "quantizeValues": [] }
          }
        ]
      },

      // Optional -- missing entirely (every preset saved before
      // feature/delay-instance existed) behaves exactly like
      // "enabled": false below. A genuinely separate spatial sound
      // source from grainCloud above (Source/DelayInstance.h/
      // DelayTap.h) -- a multi-tap delay with real feedback
      // (recirculation), mutually exclusive with grainCloud per object
      // (see ParameterPanel's Grains/Delay categories: enabling one
      // greys out the other's toggle in the UI, though the preset
      // format itself doesn't enforce this -- both blocks can
      // technically be "enabled": true at once if hand-edited).
      "delayInstance": {
        "enabled": false,
        "numTaps": 3,                 // 1..8 -- tap i's delay == delayTime*(i+1)
        "delayTime": 0.3,             // seconds, the base unit
        "delayTimeJitter": 0.0,       // 0..1, random +/- variation of each tap's own multiple, sampled once per config change
        "feedback": 0.3,              // 0..0.95, recirculation gain -- the SAME recirculating buffer taps read from already contains its own decayed repeats, so taps don't each need an independent feedback path
        "feedbackDamping": 0.0,       // 0..1, darkens the recirculating signal (a VoiceFilter LowPass in the feedback path) -- 0 = bright/undamped
        "tapSpreadRadius": 1.0,       // meters -- taps sit evenly spaced around a circle of this radius, centered on the parent's current position
        "tapOrbitAngularSpeed": 0.0,  // rad/s -- 0 = static tap positions, >0 = the whole ring of taps rotates around the parent
        // String (not a JSON number, same 64-bit-exact-round-trip reason
        // as grainCloud's own "seed") -- seeds this instance's own
        // delayTimeJitter sampling.
        "seed": "123456789",

        // Optional -- same shape and defaults as grainCloud.modifiers
        // above, applied per tap instead of per grain. "pitch" is a
        // valid "type" value here too (the schema doesn't forbid it)
        // but has NO audible effect -- ParameterPanel's Delay category
        // simply never offers it, see Source/DelayTap.h's own comment
        // on why (a tap is a fixed-rate readback of an already-
        // recirculating signal, not a spawn-based voice like a grain).
        "modifiers": []
      }
    }
  ]
}
```

## Output format is deliberately NOT part of the schema

`AmbisonicsDecoder::Mode` (the "Output Format" dropdown, Output parameter
category) and bass management are intentionally excluded from the preset
JSON, unlike everything else in `scene`/`objects`. Reasoning: output
format is a property of how the plugin instance is PATCHED into a
session -- which bus layout the host has actually negotiated -- not of
the scene being composed. Saving it into a preset and having a later
`loadPreset()` silently request a different output channel count than
what the host currently has wired up would either silently fail (most
hosts don't renegotiate buses outside of their own explicit request-and-
respond flow, see `KlangorbitProcessor::setDecoderMode()`) or, worse,
briefly present the wrong channel layout to the host's already-connected
routing. A scene preset should move objects/physics/acoustics around
freely between an Ambisonics-in-Reaper session and a 5.1-in-a-different-
session setup without also trying to drag the second session's channel
count along with it. If this changes (e.g. output format becomes a real
per-project setting worth recalling), it should be a new, its own
schemaVersion bump with an explicit migration decision for existing
presets, not a silent addition.

The same reasoning extends to the Binaural HRTF dataset choice
(`KlangorbitProcessor::BinauralDatasetSource` -- KEMAR/SADIE II D1/a
custom SOFA file, only meaningful while Output Format == Binaural): it's
plugin-instance state exactly like the output format itself, not
scene/composition state. A custom SOFA file path is additionally
machine-local (a filesystem path that may not exist, or may point to a
completely different file, on whatever machine later loads the preset) --
another reason it doesn't belong in portable scene JSON alongside
objects/scene settings that are meant to move freely between sessions and
machines.

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

## Relation to MappingProfiles/schema/

Controller-mapping profiles (which gamepad/MIDI/OSC input drives which
parameter, see `Source/MappingEngine.h`) are a completely SEPARATE
schema, with their own `schemaVersion` counter, own file format, and own
storage tree (`MappingProfiles/factory/`, `MappingProfiles/user/`) --
see `MappingProfiles/schema/README.md`. A mapping profile never appears
inside a preset's own JSON, and a preset's `schemaVersion` bumping has no
effect on mapping-profile compatibility, or vice versa: which controller
drives which parameter is independent of which scene happens to be
loaded.
