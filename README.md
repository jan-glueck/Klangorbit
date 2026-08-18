# Spatial Audio POC

Object-based Ambisonics encoder with a trajectory/physics engine.
No decoding -- output is raw Ambisonics B-format (ACN/SN3D, AmbiX-compatible),
to be processed further in SPARTA (AmbiBIN/AmbiDEC) or the IEM Plugin Suite.

## Signal flow

```
Live input (up to 8 mono channels)
        |
        +-------------------------------------------------+
        v                                                   v
[SoundObject 0..7]  <-- position/motion            [ring buffer per object]
   from TrajectoryEngine                             (continuously filled,
   (control rate, ~90 Hz)                             see GrainCloud below)
        |                                                   |
        v                                                   v
[PropagationProcessor]  -- per-object              [GrainCloud, if enabled]
   propagation delay + Doppler (unified               grain pool, 5 movement
   variable delay line), air                          modes, control rate;
   absorption (simplified one-pole                    each active grain reads
   lowpass), directivity gain                          a short, windowed burst
        |                                              from the ring buffer
        |                                                   |
        v                                                   v
        +-------------------------> [AmbisonicsEncoder] <---+
                                       generic SH computation (Legendre
                                       recursion), arbitrary order, currently
                                       3rd order = 16 channels. Each
                                       SoundObject and each active grain is
                                       encoded as its own mono source with
                                       its own ramped gains.
                                            |
                                            v
                              Ambisonics output (16 channels at order 3)
                                            |
                                            v
                     DAW / SPARTA / IEM Suite -> decoding (binaural or loudspeakers)
```

Note: grains skip `PropagationProcessor` (no per-grain Doppler/delay/air
absorption/directivity) -- only `SoundObject`s go through it. See
"GrainCloud: granular synthesis" below.

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
3. Drag object 0 in the scene view with the mouse -> the position change
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
   position like a catapult (an orange bow line follows the cursor, plus
   a dashed preview showing the actual upcoming throw/orbit -- see below)
   and release to fire it in the opposite direction. Try it a few times to
   compare with the plain throw gesture (4) -- the sling's launch speed is
   proportional to how far you pulled, not to how fast you moved the
   mouse. While pulling, hold Ctrl to switch the shot from a free throw to
   an orbit shot (the bow line turns violet); with Ctrl held, tap Alt to
   step through circular/elliptical orbit shapes -- the dashed orbit
   preview updates live as you do. Releasing far enough from the object
   fires the shot (the preview disappears the instant it does); releasing
   very close to the anchor (a barely-there pull) cancels it, same as a
   plain click. See `SlingGesture.h`/`PluginEditor::startSling()` for the
   full mechanics.
8. Drag on EMPTY space (no object/grain under the cursor) -> orbits the
   camera around the scene instead of moving anything; scroll the mouse
   wheel to zoom. The view starts pointing straight down (the same
   framing the old fixed 2D view always had) -- rotate it to see orbits
   tilted out of the ground plane, movement trails, and the room-boundary
   sphere from any angle. See "3D camera view" below.
9. Click a row in the object list on the left to select that object --
   same effect as clicking it in the scene, but doesn't require actually
   hitting it with the mouse. Useful once several objects are orbiting or
   flying around and one is hard to click directly.

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
  origin, shown as a red wireframe sphere in the scene view -- see "3D
  camera view" below) with three behaviors (`reflect` with per-object
  `restitution` / `wrap` / `absorb`).
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
  parameters of the object selected in the scene view, as well as the
  scene parameters. Writes directly to the engine, no preset file needed
  to try things out. Full field reference including defaults in
  `Presets/schema/README.md`.
- **Object list** (left side of the editor window, `Source/ObjectListPanel.h/.cpp`):
  lists every currently active object by id, click a row to select it --
  an alternative to clicking the object directly in the scene view, for
  objects that are small, far away, or moving/orbiting too fast to
  reliably hit with the mouse. Selecting via the list highlights the
  object in the scene exactly like a direct click would (same
  `selectObject()` path, same highlight, same parameter panel), and stays
  in sync the other way too -- clicking in the scene updates the list's
  own highlight. Only active objects are listed, matching what's actually
  visible/clickable in the scene; individual grains aren't listed (no
  per-grain selection exists, see the Grain Cloud parameter category).

## 3D camera view

The scene view is a single orbit-camera projection (`Source/Camera3D.h/.cpp`)
around the world origin -- there is no separate 2D/3D mode and no OpenGL;
the camera projects world positions to screen pixels with a hand-written
perspective projection, drawn with plain `juce::Graphics`. The view's
*default* state (looking straight down) is mathematically the exact same
mapping the old fixed top-down view always used, so nothing about the
existing gestures/interaction changed at that default framing -- rotating
away from it is purely additive.

- **Orbit/zoom.** Drag on empty space (no object or grain under the
  cursor -- an object/sling drag always takes priority when something is
  actually hit) to rotate the camera's azimuth/elevation; scroll to zoom
  (changes the camera's distance from the origin, multiplicatively --
  proportional at any zoom level rather than a fixed step that would feel
  too fast zoomed in and too slow zoomed out). Elevation is clamped to
  straight-down..straight-up, azimuth is unbounded. Distance itself is
  clamped to a 1..150m range -- generous enough to fit a much
  larger-than-default custom `SceneSettings.roomSize` (5m default) in
  frame without an artificial cap, but still finite. The wheel is its own
  independent input, so zooming never collides with or interrupts an
  object drag or the sling gesture.
- **Dragging objects now happens on the ground plane (z=0)**, via a
  camera ray cast onto that plane (`Camera3D::screenToGroundPlane()`)
  instead of the old fixed screen<->world formula -- works the same as
  before at the default top-down framing, and correctly follows the
  cursor at any camera angle/zoom.
- **Depth sorting.** Objects and grains are projected, sorted back-to-front
  by camera-space depth, and drawn in that order each frame
  (`PluginEditor::paint()`) so nearer things correctly draw over farther
  ones. Bounded, small item count (<= 8 objects + <= 32 grains), so this
  (and the perspective math itself) is cheap enough to redo every repaint
  without caching -- see the Performance note below.
- **Size and opacity scale with camera distance** for a spatial depth cue,
  on top of the perspective projection's natural size falloff.
- **Room boundary** renders as a wireframe sphere (three orthogonal great
  circles) instead of the old flat reference circle -- `SceneSettings.roomSize`
  was already conceptually spherical, this just makes that visible from
  any angle. A flat ground grid (1m/2m/3m circles) and a small "Front"
  marker/label at the origin remain as orientation aids.
- **Orbit-path preview.** An object currently in `Mode::Orbit` draws its
  full ellipse, not just the current point, sampled via the same formula
  `TrajectoryEngine` itself uses to move it (`Source/OrbitMath.h`, factored
  out specifically so this preview can't drift out of sync with the actual
  physics).
- **Distinct per-object color**, consistent across selection/drag state
  (golden-ratio hue stepping, so any number of objects stays visually
  distinguishable without a fixed-size palette table). Grains render in a
  paler variant of their parent object's color, additionally fading with
  age (existing behavior) and now also with camera distance.
- **Short, fading movement trail** behind each moving object (a handful of
  recent positions, captured at a decimated rate -- not a particle
  system).
- **Stronger highlight while actively dragging/slinging** an object, on
  top of the existing selection ring, so "selected" and "currently being
  manipulated" read as visibly different states.
- **Performance.** Everything above (camera basis, depth sort, per-point
  projection, trail/orbit-path sampling) runs on the bounded, small object
  and grain counts already established by the room boundary and the
  grain-cloud global spawn cap -- no pooling or extra caching was needed
  beyond what `Camera3D` already does internally (its own basis vectors
  are recomputed only when the camera actually moves, not per drawn point,
  see its class comment).

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
- **Live movement preview while aiming.** In addition to the bow line
  (which shows the pull/aim, i.e. the *opposite* of where the object will
  actually go), a second, dashed element previews the actual result and
  updates live as you drag:
  - **Free throw:** a straight dashed line from the anchor in the launch
    direction, with a small dot at its tip, length proportional to pull
    distance -- deliberately not a full trajectory simulation (no
    `globalField`/damping curvature), just a clear directional hint, per
    the design brief.
  - **Orbit shot:** the actual resulting ellipse/circle outline, sampled
    with the same `OrbitMath.h` formula `TrajectoryEngine` itself uses to
    move an orbiting object -- reflects the current pull distance,
    direction, and eccentricity step live, exactly as it will look the
    instant the shot fires.
  - Both are dashed specifically so they can never be confused with the
    real, already-happened movement trail or a confirmed orbit path
    (both solid) -- and both disappear completely the moment the mouse is
    released, nothing lingers once the object actually starts moving.
  - Grains are unaffected by any of this: a `GrainCloud` on the slung
    object (e.g. in `AttractRepelSiblings` mode) keeps moving independently
    of the parent object's own preview/throw, exactly as already
    established -- the preview only ever describes the parent object's
    own upcoming motion, never the grains'.

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

## GrainCloud: granular synthesis

Optional, per object, off by default. Multiplies a `SoundObject`'s live
input into many independently-moving grains -- conceptually similar to the
IEM GranularEncoder, but each grain's movement is physics-based rather than
purely random.

- **Ring buffer + windowed playback.** Every active object continuously
  records its live input into a per-object ring buffer
  (`SceneSettings`-independent, always filled, regardless of whether
  granulation is on). A spawned grain reads a short, Hann-windowed burst
  from it, with its own pitch (`pitchJitter`) and random start-position
  offset (`positionJitterInBuffer`).
- **Single-shot grain model.** `grainDuration` is both the audio envelope
  length and the movement lifetime -- a grain is spawned, moves for that
  duration while fading, and is done. `grainRate` controls how often new
  grains spawn.
- **Movement modes** (`Source/Grain.h`, `GrainMovementMode`): `RandomWalk`,
  `Bounce` (elastic reflection within `boundaryRadius` around the spawn
  position, `restitution`), `RadialExplosion` (`initialSpeed` +
  `acceleration`, outward), `OrbitAroundParent` (relative to the parent
  object's *current* position, which can itself be moving),
  `AttractRepelSiblings` (n-body force *within the same cloud only*,
  reusing `TrajectoryEngine::computeAttractionForce`'s softened
  inverse-square model; negative `attractionStrength` repels).
  `jitterTarget`/`jitterRange` randomize one field per spawn.
- **Pool-based, not allocated.** `GrainCloud` (`Source/GrainCloud.h/.cpp`)
  manages a fixed-size pool per object -- grains are activated/deactivated,
  never allocated on the audio thread. One `GrainCloud` per `SoundObject`,
  owned by `TrajectoryEngine` (so `PresetManager` doesn't need a dependency
  on the full plugin class), updated at control rate (~90 Hz) alongside the
  trajectory engine, with its own lock-protected snapshot for the audio
  thread -- architecturally a smaller sibling of `TrajectoryEngine` itself.
- **Rendering skips PropagationProcessor.** Each active grain is encoded
  directly via `AmbisonicsEncoder` (spatial encoding + distance gain, own
  ramped `previousChannelGains` per grain for zipper-free gain changes) --
  deliberately *not* run through `PropagationProcessor`, since a full
  per-grain Doppler/delay/air-absorption/directivity pass would be too
  expensive with dozens of concurrent grains.
- **Global spawn budget.** `maxConcurrentGrains` caps each cloud
  individually (up to 128); a further system-wide cap
  (`SpatialAudioPOCProcessor::maxConcurrentGrainsGlobal`, currently 128,
  raised from an initial 32) is shared across all clouds each control-rate
  tick, since every active grain costs a full Ambisonics encoding pass
  regardless of cloud. `grainDuration` (up to 5s) and `grainRate` (up to
  500/sec) were extended alongside it. 128 is a rough operation-count
  estimate for real-time safety, not a number profiled on real hardware in
  this environment -- the toolbar's **CPU meter** (top of the editor, next
  to the object count) shows the actual measured fraction of each audio
  block's time budget being used, turning amber/red if it gets close to or
  exceeds 100%, so you can judge for yourself on your own machine rather
  than trusting the estimate. The per-object grain ring buffer was resized
  to match the new duration/rate/jitter ranges (see `Source/Grain.h`'s
  `GrainLimits` -- the single source of truth both the UI and the buffer
  allocation read from, specifically so they can't silently drift out of
  sync with each other again).
- **GUI:** active grains render as small dots around their parent object in
  the scene view, in a paler variant of the parent's color, fading out
  with age (and now also with camera distance -- see "3D camera view"
  above); a "Grain Cloud" category in the
  parameter panel exposes all cloud-level parameters (one parameter set per
  cloud, not per individual grain).
- **Verification:** `Tools/verify_grain_cloud.cpp` exercises the exact
  grain-rendering function the plugin uses (`GrainRenderer.h`, shared, not
  reimplemented) plus all five movement modes and the spawn-budget logic,
  no GUI/audio device needed.

## Project structure

```
SpatialAudioPOC/
  CMakeLists.txt
  CHANGELOG.md          <- code versioning (SemVer)
  Source/                <- C++ code
  Tools/
    validate_presets.cpp     <- CLI tool, checks Presets/factory/*.json (see Docs/WORKFLOW.md)
    verify_propagation.cpp   <- CLI tool, checks PropagationProcessor DSP math (delay/Doppler/absorption/directivity)
    verify_grain_cloud.cpp   <- CLI tool, checks GrainCloud (5 movement modes, spawn caps) + grain rendering
    verify_orbit.cpp         <- CLI tool, checks the ellipse/orbitOrientation math and SlingGesture.h helpers
    verify_camera.cpp        <- CLI tool, checks Camera3D's projection/rotation/zoom/ground-plane math
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

- **3D camera view interaction is unverified by hand.** `Camera3D`'s
  projection/rotation/zoom math is covered by `Tools/verify_camera`
  (22+ passing checks, including that the default framing exactly matches
  the old fixed top-down view), but the actual *feel* of dragging to
  rotate and scrolling to zoom hasn't been manually tried in a running
  app in this environment (no interactive GUI testing available here) --
  in particular the drag/zoom direction sign conventions
  (`PluginEditor::mouseDrag()`/`mouseWheelMove()`) are a reasonable but
  untested guess and may feel inverted; each is a one-line sign flip if so.
  The zoom distance bounds (1..150m) and the sling movement preview added
  alongside it are likewise verified mathematically/by build+run stability
  only, not by eye.
- **Object dragging is still constrained to the ground plane (z=0)**,
  now via a camera ray cast onto that plane rather than a fixed formula
  (see "3D camera view" above) -- there's no way to drag an object's
  height directly with the mouse yet (it still only changes through
  physics: orbit planes, global field, n-body forces, etc.).
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
  (`Source/PresetManager.h/.cpp`) reads/writes scenes in the schemaVersion-2
  format (see `Presets/schema/README.md`), via two buttons in the editor
  toolbar. Loading replaces the entire scene; schemaVersion-1 presets are
  migrated automatically, and anything outside the supported range is
  rejected with an error message instead of being silently interpreted.
  `Tools/validate_presets` checks all presets in a folder via the same code
  path (prepared for CI, see `Docs/WORKFLOW.md`). The default folder in the
  file dialog (`Presets/user/`) is just a convenience default for local dev
  builds from this checkout (absolute path baked in at build time via
  CMake) -- not portable to a plugin installed elsewhere.
- **GrainCloud grains skip acoustic propagation entirely.** No per-grain
  Doppler shift, propagation delay, air absorption, or directivity -- only
  `AmbisonicsEncoder`'s spatial encoding and distance gain, for
  performance reasons (see "GrainCloud: granular synthesis" above). Only
  the parent `SoundObject`'s own signal (before granulation) goes through
  `PropagationProcessor`.
- **GrainCloud window shape is Hann only.** No alternative envelope shapes
  (Tukey, Gaussian, etc.) yet.
- **GrainCloud's global spawn budget is first-come-first-served**, not
  prioritized by object gain, distance to the listener, or any other
  criterion -- with several clouds active simultaneously near the global
  cap, which cloud gets the remaining budget on a given tick is
  effectively arbitrary (iteration order).
