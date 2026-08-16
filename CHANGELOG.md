# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
versioning follows [SemVer](https://semver.org/lang/en/) -- while the major
version is 0, the rule is: every minor version (0.X.0) may break presets
(see Presets/schema/), patch versions (0.X.Y) may not.

## [Unreleased]
### Added
- PresetManager: load/save scenes in the preset JSON format
  (schemaVersion 1), two buttons in the editor toolbar. Unknown
  schemaVersion or broken JSON is rejected with an error message.
- `Tools/validate_presets`: CLI tool, checks all presets in a folder via
  the same code path as the GUI (preparation for CI, see
  Docs/WORKFLOW.md).
- Dynamic object count: start with only 1 active object instead of all 8,
  "+ Object"/"- Remove Object" in the toolbar
  (`TrajectoryEngine::activateObject()`/`deactivateObject()`).
- Extended motion physics: `maxVelocity`, `dragCoefficient`,
  `velocitySnapThreshold`, `restitution` per object; spherical room
  boundary `SceneSettings::roomSize` with `reflect`/`wrap`/`absorb`.
- n-body refinement: `forceExponent`, `minDistance`, `maxRange` now per
  object instead of a global constant; periodic attraction modulation
  (`attractionPulseRate`/`-Depth`).
- Orbit extensions: `orbitPlaneNormal` (tilted orbit plane),
  `orbitEccentricity` (simplified ellipse), `orbitDecay`,
  `orbitReferenceObjectId` (orbit around another object).
- Scene-wide parameters: `globalField` (constant force, like
  wind/gravity), `timeScale` (fast-forward/slow-motion).
- `ParameterPanel`: side panel in the editor, shows/edits all parameters
  of the selected object as well as scene parameters directly on the
  engine.
- Preset schema extended with all fields above (all optional with a code
  default, see Presets/schema/README.md) -- **no** schemaVersion bump
  needed, since it's purely additive; `orbit_pair_demo.json` still loads
  unchanged (verified via `validate_presets` and a manual save/load
  roundtrip).
- `PropagationProcessor` (`Source/PropagationProcessor.h/.cpp`): per-object
  acoustic propagation effects, applied to the mono source signal before
  Ambisonics encoding.
  - Propagation delay and Doppler pitch shift, implemented as one unified
    variable delay line (pitch shift emerges from the delay's rate of
    change) rather than two separate mechanisms. New parameters:
    `SceneSettings::speedOfSound` (m/s, deliberately independent of
    `temperature` -- artistic decoupling from physical realism is
    intentional), `SoundObject::dopplerFactor` (0 = off, 1 = physical, >1 =
    exaggerated), `SoundObject::dopplerSmoothing`.
  - Air absorption: simplified one-pole lowpass per object, cutoff derived
    from distance/`temperature`/`relativeHumidity`/`atmosphericPressure`.
    Explicitly a simplified approximation, not ISO 9613-1 accurate.
  - Wind: `SceneSettings::windVector` shifts the effective speed of sound
    directionally, feeding into the same delay line as `speedOfSound`.
  - Directivity: `SoundObject::directivityPattern`
    (omni/cardioid/figure8) + `sourceOrientation`, angle-dependent gain
    folded into the encoder's existing ramped gain parameter.
- `Tools/verify_propagation`: CLI tool, runs `PropagationProcessor` against
  synthetic signals (no plugin/audio device needed) and checks measured
  latency, Doppler pitch direction/magnitude against the classic formula,
  that `dopplerFactor=0` suppresses the pitch shift, air-absorption
  distance trend, and directivity gain by angle. Caught a real bug during
  development (see Fixed).
- Preset schema extended with `scene.speedOfSound`/`temperature`/
  `relativeHumidity`/`atmosphericPressure`/`windVector` and per-object
  `dopplerFactor`/`dopplerSmoothing`/`directivityPattern`/
  `sourceOrientation` (all optional, no schemaVersion bump, same reasoning
  as above).
- **GrainCloud**: granular synthesis per `SoundObject`, multiplying a
  source into many independently-moving grains (conceptually similar to
  the IEM GranularEncoder, but with physics-based rather than purely
  random per-grain movement).
  - Audio: a ring buffer per granulatable object continuously records its
    live input; each spawned grain reads a short, windowed (Hann) burst
    from it with its own pitch (`pitchJitter`) and start-position jitter
    (`positionJitterInBuffer`). Single-shot grain model: `grainDuration`
    is both the audio envelope length and the movement lifetime -- a
    grain is spawned, moves, fades out, and is done, rather than an
    audio burst repeating inside a longer-lived physics object.
  - Movement: new lightweight, pool-managed `Grain` struct
    (`Source/Grain.h`) -- not a `SoundObject`, since grains are numerous
    and short-lived and must never allocate on the audio thread. Five
    movement modes: `RandomWalk`, `Bounce` (elastic reflection within
    `boundaryRadius` around the spawn position), `RadialExplosion`
    (`initialSpeed` + outward `acceleration`), `OrbitAroundParent`
    (relative to the parent object's current, possibly moving,
    position), `AttractRepelSiblings` (n-body force *within the same
    cloud only*, reusing the softened inverse-square force from
    `TrajectoryEngine::computeAttractionForce` as its model). Configurable
    `jitterTarget`/`jitterRange` randomizes one field per spawn.
  - New `GrainCloud` module (`Source/GrainCloud.h/.cpp`), architecturally
    analogous to `TrajectoryEngine`: fixed-size grain pool
    (activate/deactivate, no realtime allocation), control-rate `update()`
    on the message thread, its own lock-protected snapshot for the audio
    thread. One `GrainCloud` instance per `SoundObject`, owned by
    `TrajectoryEngine` (not `PluginProcessor`) so `PresetManager` doesn't
    need a dependency on the full plugin class.
  - `PluginProcessor::processBlock` renders each active grain like its
    own mono object -- own `previousChannelGains` for zipper-free
    Ambisonics ramping -- via the existing `AmbisonicsEncoder`. Grains
    deliberately skip `PropagationProcessor` (no per-grain Doppler/delay/
    air absorption/directivity): with dozens of concurrent grains, a full
    propagation pass per grain would be disproportionately expensive.
  - Global spawn budget: `maxConcurrentGrains` caps each cloud
    individually, and a further system-wide cap
    (`SpatialAudioPOCProcessor::maxConcurrentGrainsGlobal = 32`) is
    shared across all clouds, since every active grain costs a full
    Ambisonics encoding pass.
  - GUI: active grains render as small dots in the 2D editor, orbiting/
    scattering around their parent object and fading out with age
    (`PluginEditor::paint()`); a new "Grain Cloud" category in
    `ParameterPanel` exposes all cloud-level parameters (not per-grain).
  - `Tools/verify_grain_cloud`: CLI tool covering `renderGrainBlock`
    (Hann envelope shape, ring-wrap continuity, pitch-rate -> playback
    frequency) and `GrainCloud` (per-cloud and shared-global spawn caps,
    lifetime expiry/pool-slot reuse, and per-movement-mode invariants for
    all five modes). Caught a real bug during development (see Fixed).
  - Preset schema bumped to **schemaVersion 2**: adds an optional
    per-object `grainCloud` block (all fields optional/additive, default
    = disabled cloud). schemaVersion 1 presets are accepted and migrated
    automatically (`PresetManager::migrateSchemaV1toV2()`); presets
    outside `[1, 2]` are rejected with a clear error. See
    `Presets/schema/README.md`.

### Changed
- Clicking an object (without dragging) selects it for the parameter
  panel, without affecting its motion mode anymore -- previously every
  click was immediately translated into `Mode::Manual` (`beginDrag()` in
  `mouseDown()`), which would have reset a running orbit/impulse to
  `Static` on every selection click.

### Fixed
- First working build (VST3 + standalone): custom `Vec3` type instead of
  `juce::Vector3D` (which lives in the `juce_opengl` module and would have
  pulled in an unnecessary OpenGL dependency), `BusesProperties`
  construction via a member function instead of a free function (access
  protection), shadow-field warning in the editor fixed.
- `PropagationProcessor`: at `dopplerFactor=0`, the delay line's
  drift-correction term (meant to keep absolute latency anchored to the
  true distance over time, independent of `dopplerFactor`) was itself
  proportional to the gap between current and target delay -- during
  continuous fast movement this reintroduced a ~3% pitch shift that
  `dopplerFactor=0` was supposed to suppress entirely. Found by
  `Tools/verify_propagation`. Fixed by rate-limiting the correction to an
  absolute cap instead of a proportional one, so its own contribution to
  pitch deviation stays negligible (<0.1%) regardless of how far out of
  sync the delay is.
- `GrainCloud`: `AttractRepelSiblings` grains all spawned exactly
  coincident at the parent position, so the softened inverse-square force
  spiked to a near-infinite value on the very first update tick
  regardless of attraction sign -- both "attract" and "repel" settings
  exploded outward identically instead of pulling together/pushing apart
  as configured. Found by `Tools/verify_grain_cloud`. Fixed with a small
  random spawn-position offset, a larger softening `minDistance`
  (0.05 -> 0.15), and a hard per-grain velocity clamp as a safety net.

### Known limitations
- GrainCloud grains skip `PropagationProcessor` entirely -- no per-grain
  Doppler shift, propagation delay, air absorption, or directivity, only
  `AmbisonicsEncoder`'s spatial encoding and distance gain. A deliberate
  performance trade-off (see Added, above), not a bug.
- Grain window shape is Hann only; no other envelope shapes yet.
- `maxConcurrentGrains` is enforced per cloud and globally, but the
  global budget is currently a first-come-first-served allocation across
  clouds each control-rate tick, not prioritized by e.g. object gain or
  distance to the listener.

## [0.1.0] - POC skeleton
### Added
- TrajectoryEngine: Static/Manual/Orbit/Impulse/Attracted modes
- AmbisonicsEncoder: generic SH computation, order 0-7, SN3D/ACN
- PluginProcessor: 8 mono inputs -> Ambisonics bus (order 3 = 16 ch.)
- 2D editor: mouse drag, throw gesture, orbit via double-click
### Known limitations
- No Doppler effect, no frequency-dependent distance attenuation
- No 3D interaction, no MIDI mapping
- No preset persistence (state save is a stub)
