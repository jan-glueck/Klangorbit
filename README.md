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
[PropagationProcessor]  -- per-object propagation delay + Doppler (unified
        |                  variable delay line), air absorption (simplified
        |                  one-pole lowpass), directivity gain
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
7. Shift+drag an object -> "sling" launch gesture: pull it away from its
   position like a catapult (an orange bow line follows the cursor) and
   release to fire it in the opposite direction. Try it a few times to
   compare with the plain throw gesture (4) -- the sling's launch speed is
   proportional to how far you pulled, not to how fast you moved the
   mouse. While pulling, hold Ctrl to switch the shot from a free throw to
   an orbit shot (the bow line turns violet); with Ctrl held, tap Alt to
   step through circular/elliptical orbit shapes. Releasing far enough
   from the object fires the shot; releasing very close to the anchor
   (a barely-there pull) cancels it, same as a plain click. See
   `SlingGesture.h`/`PluginEditor::startSling()` for the full mechanics.

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
  orbits (`orbitEccentricity`, simplified approximation, see below) with a
  rotatable major axis (`orbitOrientation`), shrinking/growing orbits
  (`orbitDecay`), orbiting around another, itself-moving object instead of
  just a fixed point (`orbitReferenceObjectId`).
- **Global field & time scale:** `SceneSettings::globalField` (constant
  force/mass, like wind/gravity, affects Impulse/Attracted objects) and
  `timeScale` (fast-forward/slow-motion for the whole simulation).
- **Parameter panel** (right side of the editor window): shows/edits all
  parameters of the object selected in the 2D view, as well as the scene
  parameters. Writes directly to the engine, no preset file needed to try
  things out. Full field reference including defaults in
  `Presets/schema/README.md`.

## Sling launch gesture

Shift+drag+release on an object -- a catapult-style alternative to the
plain throw gesture (4 in "Testing with Reaper" above), for launching an
object with a precisely aimed direction and strength instead of however
fast the mouse happened to move.

- **The gesture.** Shift+click an object to grab it -- its real position
  freezes at that "anchor" point (`PluginEditor::startSling()`) and is not
  touched again until release; only a visual marker follows the cursor,
  connected to the anchor by a bow line, so pulling the marker away and
  releasing reads as drawing back and firing a catapult. The pull vector
  (anchor minus release point, `SlingGesture::computePullVector()`)
  determines both the launch direction (opposite the pull) and its
  strength (proportional to the pull distance). Pulling less than ~15 cm
  cancels the shot, same as a plain click.
- **Two launch modes, switchable mid-gesture.** While still holding Shift
  and dragging:
  - **Free throw** (default): reuses the existing throw/`Impulse` physics
    (`TrajectoryEngine::throwObject()`), just aimed by the pull instead of
    by release velocity.
  - **Orbit shot** (hold Ctrl to switch into it, tap Alt to step through
    circular/elliptical shapes): launches the object directly onto an
    orbit instead of a free trajectory
    (`TrajectoryEngine::startOrbit()`). Pull distance sets the orbit's
    size (semi-major axis), the pull line's own direction sets the
    ellipse's orientation (`SoundObject::orbitOrientation`, new field, see
    below), and the spin direction (CW/CCW) is derived from the gesture's
    geometry rather than a separate control -- pulling to one side of the
    object versus the other naturally produces the opposite spin
    (`SlingGesture::computeOrbitDirectionSign()`, a signed 2D cross
    product of the anchor's position relative to the orbit center and the
    launch direction). The orbit is always centered on the world origin,
    consistent with the existing double-click orbit gesture.
- **`SoundObject::orbitOrientation`** (new field): rotates an elliptical
  orbit's major axis within its orbit plane, radians, irrelevant at
  `orbitEccentricity=0`. `TrajectoryEngine::startOrbit()` gained matching
  optional `eccentricity`/`orientation` parameters (default 0 = unchanged,
  circular behavior, so the existing double-click gesture and any preset
  written before this feature keep working exactly as before).
- **All pull-to-launch math lives in `SlingGesture.h`**, deliberately
  separate from the JUCE mouse-handling code in `PluginEditor`, so the
  exact same functions run in the editor and in
  `Tools/verify_orbit.cpp` (checks the direction-sign/orientation math
  plus the ellipse-rotation formula in `TrajectoryEngine`) -- not a test
  reimplementation.
- **Trajectory/orbit-shape preview while aiming is not implemented yet**
  (only the bow line itself) -- a natural follow-up once the basic gesture
  has been tried out.

## Acoustic propagation: Doppler, delay, air absorption, directivity

Runs in `PropagationProcessor` (`Source/PropagationProcessor.h/.cpp`), on
the mono source signal before Ambisonics encoding, per active object.

- **Propagation delay and Doppler pitch shift are ONE mechanism, not two.**
  A per-object variable-length delay line continuously tracks
  `distance / effectiveSpeedOfSound`. Reading it through a smoothly
  time-varying (interpolated) delay produces the correct Doppler pitch
  ratio as a mathematical side effect of the delay's rate of change -- no
  separate pitch-shifter/resampler. `SoundObject::dopplerFactor` scales
  only that rate-of-change ("AC") component (0 = no audible pitch shift, 1
  = physically correct, >1 = exaggerated); the delay's absolute value
  ("DC" component, i.e. the actual latency) is always kept anchored to the
  true distance-based value via a rate-limited correction, independent of
  `dopplerFactor`, so long-term latency can't drift away from reality.
  `dopplerSmoothing` (seconds) smooths the pitch effect against abrupt
  direction changes (e.g. a bounce off the room boundary).
- **`SceneSettings::speedOfSound`** (m/s, default 343) is deliberately kept
  independent of `temperature` rather than computed from it -- letting it
  drift from the physical value (e.g. down to 50 m/s) is an intentional
  creative tool: normal movement speeds then produce strongly audible,
  surreal Doppler shift and propagation delay instead of a naturalistic
  one.
- **Air absorption** is a simplified one-pole lowpass per object, cutoff
  derived from distance, `temperature`, `relativeHumidity`, and
  `atmosphericPressure` via cheap closed-form curves -- captures the
  general, documented trend (absorption peaks around medium humidity for
  mid/high frequencies, lower at both extremes) but is explicitly **not**
  an implementation of the full ISO 9613-1 relaxation-frequency model.
  Good enough for sound design, not for acoustic measurement.
- **Wind** (`SceneSettings::windVector`, m/s) shifts the effective speed of
  sound in the propagation direction (source -> listener) -- a tailwind
  speeds up arrival, a headwind slows it down. Feeds into the same delay
  line as `speedOfSound`, so it affects both latency and Doppler together,
  physically consistently.
- **Directivity** (`SoundObject::directivityPattern`: omni/cardioid/
  figure-8, plus `sourceOrientation`) computes an angle-dependent gain
  between the object's facing direction and the listener, folded into the
  same gain parameter the encoder already ramps smoothly -- so an object
  can move and "turn away" independently of each other.
- **Verification:** `Tools/verify_propagation.cpp` runs the DSP core
  against synthetic signals (no plugin/audio device needed) and checks
  measured latency against `distance/speedOfSound`, pitch direction and
  magnitude for approaching/receding sources against the classic Doppler
  formula, that `dopplerFactor=0` actually suppresses the pitch shift, that
  air absorption reduces high-frequency energy at distance, and directivity
  gain by angle -- catches DSP math regressions fast without a full
  plugin build or manual listening test each time.

## Project structure

```
SpatialAudioPOC/
  CMakeLists.txt
  CHANGELOG.md          <- code versioning (SemVer)
  Source/                <- C++ code
  Tools/
    validate_presets.cpp    <- CLI tool, checks Presets/factory/*.json (see Docs/WORKFLOW.md)
    verify_propagation.cpp  <- CLI tool, checks PropagationProcessor DSP math (delay/Doppler/absorption/directivity)
    verify_orbit.cpp         <- CLI tool, checks the ellipse/orbitOrientation math and SlingGesture.h helpers
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
- **Air absorption is a simplified approximation, not ISO 9613-1
  accurate.** See "Acoustic propagation" above -- captures the general
  distance/humidity/temperature trends via cheap closed-form curves, not
  the full relaxation-frequency equations. Fine for sound design, not for
  acoustic measurement.
- **Doppler effect and propagation delay are implemented** (see "Acoustic
  propagation" above), as a unified variable delay line rather than a
  separate pitch-shifter + fixed-delay pair -- deliberately, since that
  keeps them physically consistent by construction and avoids the two
  effects fighting or double-applying.
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
- **Sling gesture has no live trajectory/orbit-shape preview yet.** Only
  the bow line (anchor -> cursor) is drawn while aiming; showing the
  predicted orbit ellipse or throw arc before release is a natural
  follow-up (see "Sling launch gesture" above).
- **Sling orbit shots are always centered on the origin**, matching the
  existing double-click orbit gesture -- a freely positionable orbit
  center (e.g. via a separate marking click) was considered but
  deliberately left out to keep the gesture to a single, uninterrupted
  Shift+drag+release motion.
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
