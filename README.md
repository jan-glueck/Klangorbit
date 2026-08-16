# Spatial Audio POC

Object-based Ambisonics encoder with a trajectory/physics engine.
No decoding -- output is raw Ambisonics B-format (ACN/SN3D, AmbiX-compatible),
to be processed further in SPARTA (AmbiBIN/AmbiDEC) or the IEM Plugin Suite.

## Signal flow

```
Live input (up to 8 mono channels)
        |
        v
[SoundObject 0..7]  <-- position/motion from TrajectoryEngine (control rate, ~90 Hz)
        |
        v
[AmbisonicsEncoder]  -- generic SH computation (Legendre recursion),
        |                arbitrary order, currently 3rd order = 16 channels
        v
Ambisonics output (16 channels at order 3)
        |
        v
DAW / SPARTA / IEM Suite -> decoding (binaural or loudspeakers)
```

## Build

Requirements: CMake >= 3.22, Xcode Command Line Tools (macOS).

```bash
# Get JUCE as a submodule (recommended, otherwise CMake re-downloads it on every clean build)
git submodule add https://github.com/juce-framework/JUCE.git JUCE

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

Result: `SpatialAudioPOC.vst3` and the standalone app in the build directory
(`SpatialAudioPOC_artefacts/`). The VST3 is also automatically copied to the
system plugin folder (`COPY_PLUGIN_AFTER_BUILD TRUE`).

## Testing with Reaper + SPARTA/IEM

1. Start the plugin/standalone app, connect a live input (microphone or
   audio interface channel) to Input 0.
2. Route the Ambisonics output (16 channels) to a bus with AmbiBIN (SPARTA)
   or the IEM BinauralDecoder.
3. Drag object 0 in the 2D window with the mouse -> the position change
   should show up as a change in direction in the binaural playback.
4. Double-clicking an object starts an orbit motion around the origin
   (demo for the trajectory mode).
5. Drag an object quickly and release -> throw gesture, the object keeps
   moving freely afterward and is slowed down by `damping`/`dragCoefficient`,
   and reflected/wrapped/absorbed at the room boundary (`roomSize`).
6. Clicking an object (without dragging) selects it -- its parameters
   appear in the panel on the right. "+ Object" activates the next free
   object (only object 0 is active at startup), "- Remove Object"
   deactivates the selected one.

## Objects, motion physics, and the parameter panel

- **Object count is dynamic.** Only object 0 is active at startup (no
  longer all of `SAPOC_MAX_LIVE_INPUTS`). "+ Object"/"- Remove Object" in
  the toolbar activate/deactivate individual slots out of the max. 8 object
  slots (`TrajectoryEngine::activateObject()`/`deactivateObject()`). The
  audio bus itself stays fixed at 8 channels (see the bus layout limitation
  above) -- "add/remove" is purely a matter of `SoundObject::inputChannel >= 0`,
  the same convention that the encoder/snapshot/preset saving already used
  before.
- **Objects now brake to a stop instead of gliding forever.** `maxVelocity`
  caps the speed, `dragCoefficient` is a real, velocity-proportional
  braking force (in addition to the existing `damping`),
  `velocitySnapThreshold` hard-stops very slow residual motion instead of
  letting it decay asymptotically forever.
- **Room boundary.** `SceneSettings::roomSize` (spherical around the
  origin, shown as a red reference line in the 2D window) with three
  behaviors (`reflect` with per-object `restitution` / `wrap` / `absorb`).
- **n-body refinement:** `forceExponent`, `minDistance`, and `maxRange` are
  now per object (previously a global constant); periodic modulation of the
  attraction strength via `attractionPulseRate`/`-Depth`.
- **Orbit extensions:** tilted orbit plane (`orbitPlaneNormal`), elliptical
  orbits (`orbitEccentricity`, simplified approximation, see below),
  shrinking/growing orbits (`orbitDecay`), orbiting around another,
  itself-moving object instead of just a fixed point
  (`orbitReferenceObjectId`).
- **Global field & time scale:** `SceneSettings::globalField` (constant
  force/mass, like wind/gravity, affects Impulse/Attracted objects) and
  `timeScale` (fast-forward/slow-motion for the whole simulation).
- **Parameter panel** (right side of the editor window): shows/edits all
  parameters of the object selected in the 2D view, as well as the scene
  parameters. Writes directly to the engine, no preset file needed to try
  things out. Full field reference including defaults in
  `Presets/schema/README.md`.

## Project structure

```
SpatialAudioPOC/
  CMakeLists.txt
  CHANGELOG.md          <- code versioning (SemVer)
  Source/                <- C++ code
  Tools/
    validate_presets.cpp  <- CLI tool, checks Presets/factory/*.json (see Docs/WORKFLOW.md)
  Presets/
    schema/README.md    <- preset format, own schemaVersion
    factory/             <- curated, checked-in scenes
    user/                 <- own experiments, not committed (.gitignore)
  Docs/
    WORKFLOW.md          <- branch/tag/release conventions
    experiments/          <- session logs for interesting sound findings
      scratch/             <- raw notes, not committed
  JUCE/                   <- submodule, not checked in
```

Core principle: **code version** (CHANGELOG/SemVer), **preset schema
version** (Presets/schema/), and the **experiment log**
(Docs/experiments/) are three separate versioning schemes, because they
change independently of each other -- a preset should still load after a
code refactor, and an interesting sound finding is often not 1:1
reproducible from a preset (e.g. if it emerged from live interaction).
Details in the respective READMEs, branch/release process in
Docs/WORKFLOW.md.

## Known limitations / next steps

- **2D interaction only.** The mouse moves objects in the x/y plane
  (height z fixed at 0). 3D view/interaction is planned as a next step,
  same data backing (TrajectoryEngine/SoundObject are already 3D).
- **No MIDI mapping.** The `InputMapper` module from the architecture
  sketch isn't implemented yet; MIDI CC on object parameters is missing.
- **Ambisonics order is fixed per instance.** `AmbisonicsEncoder::setOrder()`
  exists, but the output bus is fixed at prepare/construction time (VST3
  buses aren't trivially reconfigurable at runtime). For runtime order
  switching: easier to implement in the standalone case than in the plugin
  context, since no host bus contract exists there -- possibly extend the
  standalone case first.
- **Distance attenuation is purely gain-based (1/r law).** No
  frequency-dependent air absorption (high-frequency loss over distance).
  For accurate physical modeling, next step would be a simple one-pole
  low-pass per object, cutoff dependent on distance.
- **No Doppler effect.** Velocity is already available in the snapshot
  (`TrajectoryEngine::Snapshot::velocity`), but the encoder doesn't turn it
  into a pitch/delay modulation yet.
- **n-body attraction is untested with many simultaneously active
  attractors** -- the inverse-square law can produce hard jumps at very
  small distances despite the `minDistance` clamp. `forceExponent` (< 2 =
  softer/longer-range) and `maxRange` (cutoff radius) now offer tools
  against that, but they're no substitute for a real softer force law
  (e.g. a Plummer potential) if that turns out to be needed.
- **Collision between objects is not implemented.** Objects pass through
  each other; a `collisionRadius`/`onCollision` mechanism
  (bounce/merge/trigger) is a possible next step, but deliberately not
  part of this change -- "merge" would raise questions like what happens
  to the fixed input-channel assignment of a merged object, which is its
  own architecture decision.
- **No audio-reactive coupling.** Parameters like attraction strength or
  orbit speed could be modulated by the input level of the respective
  object (`attractionModulatedByAmplitude` or similar) -- needs a new data
  path from the audio thread (level/envelope follower per channel) back to
  the message thread, which doesn't exist yet.
- **`orbitEccentricity` is a simplified approximation**, not a
  focus-based Kepler orbit (fixed semi-axes instead of variable angular
  speed per Kepler's second law) -- deliberately kept simple for the POC.
- **DAW session persistence is still missing.** `getStateInformation`/
  `setStateInformation` are still stubs -- the scene is NOT automatically
  saved/restored in the host project. Deliberately not short-circuited
  with the preset JSON: the host can call `setStateInformation` from any
  thread, but `TrajectoryEngine::getObject()` is not safe for that (see
  the class comment, "only from the message thread"). Without additional
  synchronization of the object list itself (currently only the
  audio-thread snapshot is locked), that would be a race. Loading a preset
  via the GUI is not affected by this (always runs on the message thread).
- **Loading/saving presets is implemented.** `PresetManager`
  (`Source/PresetManager.h/.cpp`) reads/writes scenes in the schemaVersion-1
  format (see `Presets/schema/README.md`), via two buttons in the editor
  toolbar. Loading replaces the entire scene; an unsupported
  `schemaVersion` or broken JSON is rejected with an error message instead
  of being silently interpreted. `Tools/validate_presets` checks all
  presets in a folder via the same code path (prepared for CI, see
  `Docs/WORKFLOW.md`). The default folder in the file dialog
  (`Presets/user/`) is just a convenience default for local dev builds
  from this checkout (absolute path baked in at build time via CMake) --
  not portable to a plugin installed elsewhere.
