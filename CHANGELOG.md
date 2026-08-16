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
