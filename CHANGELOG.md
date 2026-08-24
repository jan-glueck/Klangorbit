# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
versioning follows [SemVer](https://semver.org/lang/en/) -- while the major
version is 0, the rule is: every minor version (0.X.0) may break presets
(see Presets/schema/), patch versions (0.X.Y) may not.

## [Unreleased]
### Added
- **"Mute Original Audio" toggle in the Grains parameter category
  (`GrainCloudSettings::sourceMuted`).** Silences just an object's own
  dry/unGranulated signal while leaving its grains completely
  untouched, so the grains can be heard in isolation -- independent of
  the object list's existing Mute button, which silences an object's
  source AND its grains together (see `MuteSoloLogic.h`; that logic is
  intentionally unchanged and still governs both signals when used).
  - `PluginProcessor::processBlock()`'s main-object rendering pass
    (the object's own dry audio) gained its own smoothed mute ramp
    (`sourceMuteRampGain`, same click-free ramping treatment and
    `muteRampSeconds` as the existing Mute/Solo ramp), read from
    `GrainCloudSettings::sourceMuted` and multiplied into that pass's
    final gain alongside the existing ramp. The separate grain-
    rendering pass (a different loop over the same objects) is
    deliberately left untouched -- it never reads this new field, only
    the existing `muted`/`soloed`, so grains keep playing regardless.
    Two independent ramps rather than one shared ramp specifically
    because the existing `muteRampGain` is also read by that grain loop
    and must stay unaffected by this new, source-only mute.
  - Persisted in presets (`grainCloud.sourceMuted`, optional/additive
    like every other schemaVersion-2 `grainCloud` field -- no schema
    bump needed); reset to `false` like the rest of `GrainCloudSettings`
    when an object is deactivated/reused.
  - Verified: zero-warning rebuild, the full `Tools/verify_*` suite
    (untouched by this change, still passing), a manual roundtrip
    check (`sceneToVar`/`loadFromVar`) confirming the new field
    survives a save/load cycle, and a temporary forced-category
    screenshot (reverted before commit) confirming the toggle renders
    correctly in the Grains panel, right below "Enabled". The actual
    audio-thread mute/isolation behavior itself is reasoned through
    and mirrors the existing, already-verified Mute/Solo ramp exactly,
    but -- like the rest of this plugin's audio path -- has no
    automated test harness (`PluginProcessor` isn't linked by any
    `Tools/verify_*` target) and was not confirmed by ear in a DAW in
    this environment.
- **English user documentation + an in-app Help window.**
  `Docs/UserGuide.md`: a full standalone manual (scene view/camera
  controls, object list, all five motion modes, the sling launch
  gesture's three modes, every parameter-panel category and field,
  grain clouds, presets, keyboard shortcuts, tips for use with Reaper +
  SPARTA/IEM). A "?" button in the toolbar (top-right of the preset
  row) opens `HelpWindow` -- a real, separate OS-level window (not a
  panel docked in the editor) showing a condensed copy of the same
  material (`Source/HelpContent.h`) in a scrollable, read-only,
  monospaced text view, styled to match the app's dark theme. A
  separate top-level window rather than an in-editor overlay
  specifically so it works identically whether Klangorbit is the
  Standalone app or hosted as a VST3, where the editor itself has no
  spare screen space of its own for a large reference document.
  `HelpWindow` is created lazily on first click and then just
  shown/hidden (never destroyed) on repeat opens, so reopening is
  instant. Verified with a temporary auto-open-on-launch (removed
  before commit) confirming the window renders correctly -- readable
  text, correct dark styling, working scrollbar -- since this
  environment has no accessibility permission to script a real button
  click; the button itself is wired the same way every other toolbar
  button already is (`onClick` -> a named handler), which is
  exercised by every other manual launch/rebuild check already, so a
  real click was low-risk to leave unverified for this one addition.
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
    (`KlangorbitProcessor::maxConcurrentGrainsGlobal = 32`) is
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
- **Sling launch gesture**: Shift+drag+release an object to launch it like
  a catapult, as an alternative to the plain throw gesture -- launch
  direction/strength come from how far and which way it was pulled, not
  from release velocity.
  - The object's real position stays frozen at the "anchor" (its position
    when the gesture started) while aiming; only a visual marker follows
    the cursor, connected to the anchor by a bow line
    (`PluginEditor::startSling()`/`paint()`).
  - Two launch modes, switchable mid-gesture without releasing Shift:
    free throw (default, reuses the existing `TrajectoryEngine::throwObject()`
    Impulse physics) and orbit shot (hold Ctrl to switch, tap Alt to step
    through circular/elliptical shapes) via `TrajectoryEngine::startOrbit()`.
  - `SoundObject::orbitOrientation` (new field): rotates an elliptical
    orbit's major axis within its plane. `TrajectoryEngine::startOrbit()`
    gained optional `eccentricity`/`orientation` parameters (default 0 =
    unchanged circular behavior, so the existing double-click orbit
    gesture and every preset written before this feature are unaffected).
  - Orbit shots are always centered on the world origin (same convention
    as the double-click gesture); the spin direction (CW/CCW) is derived
    from the gesture's own geometry (a signed 2D cross product of the
    anchor's position relative to the center and the launch direction)
    rather than a separate control.
  - `Source/SlingGesture.h`: header-only, JUCE-GUI-free math (pull vector,
    orbit direction sign, orbit orientation, the fixed eccentricity-step
    list) shared between `PluginEditor` and `Tools/verify_orbit`, same
    "shared, not reimplemented" pattern as `GrainRenderer.h`.
  - Preset schema extended with per-object `orbitOrientation` (optional,
    additive, no schemaVersion bump).
- `Tools/verify_orbit`: CLI tool, checks that `orbitOrientation` rotates
  the ellipse's major axis by exactly the given angle, that a circular
  orbit is unaffected by it, that `startOrbit()`'s new optional arguments
  default to the old circular/unrotated behavior, and the
  `SlingGesture.h` pull-vector/direction-sign/orientation math against
  known geometry.
- **3D orbit-camera view**, replacing the previous fixed top-down 2D
  rendering path -- one projection, not a 2D/3D mode switch.
  - New `Camera3D` (`Source/Camera3D.h/.cpp`): azimuth/elevation/distance
    orbit camera around the world origin, hand-written perspective
    projection (no OpenGL, no `juce_graphics` dependency in the class
    itself -- pure `Vec3`/float math, same spirit as `SlingGesture.h`).
    Its default state (straight down) is mathematically the exact same
    screen mapping the old fixed 2D view always used, which is what lets
    the old view be replaced by this camera's default framing rather than
    kept as a second rendering path.
  - Interaction: drag on empty space (no object/grain under the cursor --
    object and sling gestures always take priority when something is
    actually hit) orbits the camera; mouse wheel zooms
    (`KlangorbitEditor::mouseDrag()`/`mouseWheelMove()`). Dragging an
    object now works via a camera ray cast onto the world's ground plane
    (`Camera3D::screenToGroundPlane()`) instead of a fixed inverse-
    projection formula, so it keeps following the cursor correctly at any
    camera angle/zoom.
  - Rendering: objects and grains are projected, depth-sorted back-to-
    front, and drawn in that order (`PluginEditor::paint()`) so nearer
    things correctly occlude farther ones; marker size and opacity scale
    with camera distance for a spatial depth cue. The room boundary
    (`SceneSettings.roomSize`, already conceptually spherical) now renders
    as an actual wireframe sphere (three orthogonal great circles) instead
    of a flat reference circle.
  - Orbit-path preview: an object in `Mode::Orbit` draws its whole
    ellipse, not just the current point. The ellipse formula was factored
    out of `TrajectoryEngine::integrate()` into `Source/OrbitMath.h`
    specifically so this preview samples the exact same math the physics
    itself uses, rather than a separate (and driftable) reimplementation.
  - Visual refresh: distinct, consistent per-object color (golden-ratio
    hue stepping, scales to any object count without a fixed palette
    table); grains render in a paler variant of their parent's color;
    short fading movement trails behind moving objects (a handful of
    recent positions, not a particle system); a clearly stronger highlight
    while an object is actively being dragged/slung, on top of the
    existing selection ring.
  - Performance: object/grain counts are already bounded (<= 8 objects,
    <= `maxConcurrentGrainsGlobal` = 32 grains, see the GrainCloud entry
    above), so depth-sorting and re-projecting every repaint needed no
    additional pooling/caching -- `Camera3D` itself still avoids
    recomputing its own basis vectors per drawn point, only when the
    camera actually moves (see its class comment).
  - Preset schema unaffected (the camera is pure view state, not part of
    the scene); the new `SoundObject::orbitOrientation` field (see above)
    is unrelated to this entry, just landed alongside it.
- `Tools/verify_camera`: CLI tool, checks `Camera3D`'s default-state basis
  vectors against the old 2D view's exact mapping, projection axis
  directions/signs, rotation/zoom clamping, near-clip visibility,
  perspective size scaling, and the ground-plane raycast's round-trip
  accuracy with `project()` (including after rotating the camera).
- **Live movement preview for the sling gesture** (the optional second
  step from the original sling-launch design, now implemented): while
  Shift-dragging, a second, dashed element previews what will actually
  happen on release, live-updating with pull distance/direction and (in
  orbit mode) the current eccentricity step.
  - Free throw: a straight dashed line from the anchor in the launch
    direction (length proportional to pull distance) with a small dot at
    its tip -- deliberately not a full trajectory simulation, just a
    clear directional hint.
  - Orbit shot: the actual resulting ellipse/circle outline, sampled with
    the same `OrbitMath.h` formula `TrajectoryEngine` itself uses to move
    an orbiting object (via a scratch `SoundObject` holding only the
    orbit-shape fields), so the preview can't drift out of sync with what
    a release would actually produce.
  - Dashed specifically to stay visually distinct from the real,
    already-happened movement trail and from a confirmed orbit path
    (both solid); disappears completely and immediately on release
    (gated on the existing `slingActive` flag, nothing new needed to make
    it vanish).
  - Deliberately no special-cased preview for a slung object's own
    `GrainCloud` (e.g. in `AttractRepelSiblings` mode) -- grains already
    move and render independently of their parent object, and the sling
    preview only ever describes the parent object's own upcoming motion.
- **Mean-reverting orbit radius** (Ornstein-Uhlenbeck process), as an
  alternative to `orbitDecay` for a "living, breathing" orbit that wanders
  around a baseline instead of drifting away permanently in one direction.
  New `SoundObject` fields: `orbitRadiusBaseline`, `orbitRadiusReversionRate`,
  `orbitRadiusNoiseAmplitude` (both rate and amplitude default to 0 =
  disabled, so existing behavior/presets are completely unaffected --
  verified by `Tools/verify_orbit`). Discrete Euler-Maruyama update in
  `TrajectoryEngine::integrate()`'s Orbit case:
  `radius += reversionRate*(baseline-radius)*dt + noiseAmplitude*sqrt(dt)*gaussianRandom()`,
  clamped to `[0.05, 1000]`. `orbitDecay` itself is unchanged and can still
  be combined with this (the two would pull against each other -- a niche
  but not forbidden combination). Gaussian noise via a self-contained
  Box-Muller transform (`TrajectoryEngine::nextGaussian()`); `orbitDecay`'s
  existing deterministic drift is untouched.
  - `Tools/verify_orbit` gained statistical checks: the no-op default,
    monotonic convergence under pure reversion, that noise alone actually
    perturbs the radius while respecting the hard clamp, and that the
    combined process stays bounded near the baseline over a long
    (20000-step) run rather than drifting away (17/17 checks passing).
  - **Not applied to `orbitEccentricity`** -- assessed and proposed as a
    possible follow-up rather than silently added; eccentricity is
    bounded in `[0, 0.95)` rather than open-ended like a radius, so the
    same reversion/noise formula would need boundary handling that
    behaves quite differently (clamping near 0.95 is far more visually
    disruptive -- an ellipse suddenly flattening/degenerating -- than
    clamping a radius near a floor). See the PR discussion for the
    reasoning; happy to add it as its own opt-in field set if wanted.
- **Grain density and duration raised**, plus a CPU-load display and a
  ring-buffer fix that were both needed to do so safely.
  - `maxConcurrentGrainsGlobal` (the system-wide simultaneous-grain cap)
    raised from 32 to 128. Assessment: a rough operation-count estimate
    (order-3 Ambisonics encode = 16 channels, block-rate spherical-harmonic
    coefficients + a cheap per-sample ramp, no per-sample trig) suggests
    128 concurrent grains stays comfortably real-time-safe on any
    reasonably modern CPU -- but this is *not* a measurement on real
    hardware (not available in this environment), so instead of raising
    the limit uncommented, a real, measured CPU-load indicator was added:
    the toolbar now shows the smoothed fraction of each audio block's
    actual time budget spent in `processBlock()`
    (`KlangorbitProcessor::getEstimatedCpuLoad()`), turning
    amber/red as it approaches/exceeds 100%, so the user can verify this
    for themselves rather than trusting the estimate.
  - `grainDuration`'s upper bound raised 2.0s -> 5.0s; `grainRate`'s upper
    bound raised 200/sec -> 500/sec (`Source/Grain.h`'s new `GrainLimits`
    namespace).
  - Ring buffer sizing fixed to actually cover the resulting worst case:
    a grain can look back into its ring buffer by up to
    `positionJitterInBuffer` (already 1.5s), then read forward through up
    to `grainDuration` seconds of output time, consuming up to 2x that
    much SOURCE material if pitched up via `pitchJitter` -- at the new
    5.0s duration that's a required reach of 1.5 + 5.0*2.0 = 11.5s, far
    beyond the previous fixed 2.0s buffer. `GrainLimits` is now the single
    source of truth both `ParameterPanel`'s slider ranges and
    `PluginProcessor`'s buffer allocation read from, so they can't
    silently drift out of sync again, backed by a `static_assert` that
    fails the build if the buffer formula ever stops covering the
    required reach.
- **Object list sidebar** (`Source/ObjectListPanel.h/.cpp`, left side of
  the editor window): lists every active `SoundObject` by id, click a row
  to select it -- an alternative to clicking the object directly in the
  scene view, for objects too small, fast, or far away to reliably hit
  with the mouse. Reuses `PluginEditor::selectObject()`, the exact path a
  scene-view click already used, so the highlight and parameter-panel
  wiring are identical either way; selection stays in sync in both
  directions (a scene-view click also updates the list's own highlight).
  The panel owns no selection state itself -- `PluginEditor::selectedObjectIndex`
  remains the single source of truth, unchanged from before this existed.
  Refreshed after anything that can change which objects are active
  (`+`/`-` Object buttons, preset load). Only lists active objects, no
  per-grain entries (no such selection concept exists, see the Grain
  Cloud parameter category).
- **Optional per-grain Doppler pitch shift**, off by default
  (`GrainCloudSettings::dopplerEnabled`, new "Doppler" toggle in the
  Grain Cloud parameter category). Main-object Doppler already existed
  (`PropagationProcessor`/`SoundObject::dopplerFactor`); this is a
  deliberately much cheaper per-grain approximation, not a reuse of that
  machinery -- grains still skip `PropagationProcessor` entirely (no
  delay line, no per-sample cost). New `Source/GrainDoppler.h`
  (header-only, JUCE-GUI-free, same spirit as `SlingGesture.h`/`OrbitMath.h`):
  a single classic-Doppler-formula pitch ratio
  (`speedOfSound / (speedOfSound - radialVelocity)`) computed once per
  grain per audio block from its control-rate position/velocity snapshot
  (`GrainCloud::Snapshot` gained a `velocity` field), multiplied into the
  existing `pitchJitter`-based playback rate. Uses the parent
  `SoundObject`'s own `dopplerFactor` to scale strength (0 = no shift, 1 =
  physical, >1 = exaggerated) -- one familiar knob, not a second one.
  Defensively clamped (denominator floor, final ratio bounded to
  `[0.25, 4.0]`) so a single mispitched grain can't become a jarring
  artifact even at extreme/pathological inputs (velocity far exceeding
  `speedOfSound`, an artistically very low `speedOfSound`, or a grain
  exactly at the listener position). Default off because it's a real (if
  small -- one `asin`-free sqrt+dot-product per grain per block, not per
  sample) added cost per grain, and at up to 128 concurrent grains
  (see above) that adds up -- opt-in rather than silently changing
  existing grain-cloud sound.
  - `Tools/verify_grain_cloud` gained checks: `dopplerFactor=0` disables
    it exactly, approaching/receding grains raise/lower pitch, purely
    tangential motion produces no shift, `dopplerFactor` scales the
    effect proportionally, the approaching-case ratio matches the classic
    Doppler formula numerically, and extreme/pathological inputs stay
    finite and within the clamp (9/9 passing).
- **Solo/Mute per object.** New `SoundObject::muted`/`soloed` fields
  ("Muted"/"Soloed" toggles in the parameter panel's Object category).
  - Own `muted` always wins over `soloed` (an object can't be
    simultaneously "definitely silent" and "definitely audible");
    otherwise, if any object is soloed, every non-soloed object goes
    silent while every soloed object stays audible -- classic
    non-exclusive DAW solo, not a single-object radio button. Decision
    logic factored into `Source/MuteSoloLogic.h` (header-only, no JUCE
    dependency at all, same "shared, not reimplemented" principle as
    `SlingGesture.h`/`OrbitMath.h`/`GrainDoppler.h`), shared between
    `PluginProcessor`'s two per-object loops (main object + its
    `GrainCloud`'s grains) and `Tools/verify_mute_solo` (8/8 checks
    passing, every muted/soloed/anySoloed combination).
  - Applies to a muted/soloed-out object's `GrainCloud` too -- its grains
    fade and get skipped right alongside the parent, not just the
    object's own signal.
  - Smoothly ramped (`muteRampSeconds` = 20ms) rather than switched
    instantly, so toggling never clicks; once an object's ramp has
    actually reached silence (not just close to it), its
    propagation/encoding work is skipped entirely for that block -- the
    actual performance win, rather than paying full cost every block just
    to encode silence.
  - Serialized in presets (optional, additive, no schemaVersion bump).
  - Not yet wired into the object-list sidebar (`Source/ObjectListPanel.h`,
    merged separately) -- both features share a `SoundObject` and a
    selection index, but the sidebar rows themselves don't yet expose
    Solo/Mute controls; only accessible from the parameter panel with an
    object selected for now. Still an open follow-up now that both are
    on `main` together, not a merge conflict to resolve -- the actual
    UI hookup in `Source/ObjectListPanel.cpp` hasn't been written yet.
- **Grain read-depth range** (`grainReadDepthRangeMin`/`grainReadDepthRangeMax`,
  `grainReadDepthDistribution`, `Source/Grain.h`/`GrainCloud.cpp`): a new,
  independent parameter set controlling how far into the ring buffer's
  *past* a grain's start point may be drawn from, deliberately kept
  separate from `pitchJitter`/`positionJitterInBuffer` (both sampled and
  applied additively) rather than just extending
  `positionJitterInBuffer`'s existing range -- so a small amount of
  de-clicking jitter and a large, deliberate reach into history can be
  configured independently instead of being the same knob. 0/0 (default)
  disables it, reproducing exactly the previous behavior.
  - Assessment on the distribution model, since this was flagged as a
    judgment call rather than pure implementation: went with three fixed
    presets (`Uniform`, `WeightedTowardRecent`, `WeightedTowardOld`, simple
    `u^2`/`1-(1-u)^2` power-curve shaping of the uniform draw) instead of a
    general parametric distribution (e.g. a tunable skew/beta exponent
    exposed in the UI). A full parametric model would add a slider whose
    musical effect is hard to predict from the number alone, for a use
    case (biasing where in history grains are drawn from) that in
    practice only needs "even", "prefer recent", or "prefer old" --
    exactly the brief's own "no overengineering, two or three presets is
    enough". If a specific bias curve turns out to be wanted later, it's
    a self-contained addition to `sampleDepthFraction()`
    (`Source/GrainCloud.cpp`), not a rearchitecture.
  - `GrainLimits::maxGrainReadDepthRange` (10s) extends
    `GrainLimits::requiredRingBufferSeconds`'s formula (additive with the
    existing `positionJitterInBuffer` term, since the two offsets are
    sampled independently and stack) and is the same constant
    `ParameterPanel`'s new sliders are bounded to -- the user cannot
    configure a depth beyond what the ring buffer actually holds, by
    construction, rather than via a separate runtime clamp/warning.
  - `Tools/verify_grain_cloud` extended with tests for backward-compatible
    no-op at the 0/0 default, range enforcement (40 grains, range
    respected in all cases), and a statistical check that the two weighted
    presets actually shift the average sampled depth in the expected
    direction relative to `Uniform` (measured over 300 grains: recent
    ~1.31s / uniform ~1.97s / old ~2.58s for a configured [0, 4]s range).
- **Room boundary visibility toggle** (`SceneSettings::showRoomBoundary`,
  "Show Boundary" in the Scene category). Purely cosmetic -- the boundary
  keeps applying physically (`reflect`/`wrap`/`absorb`) while hidden, only
  `PluginEditor::paint()`'s `drawShadedBoundarySphere()` call is skipped.
  Default `true` (unchanged appearance for existing presets/behavior).
- **On-screen reminder of the Orbit and Sling-launch gestures**, drawn at
  the bottom of the scene view every frame. Both are pure mouse+modifier-key
  gestures (double-click; Shift+drag, with Ctrl/Alt/Tab held/tapped while
  pulling) with no other UI affordance -- no button, no menu entry -- so
  previously a first-time user had no way to discover them at all. Two
  lines: the gesture names themselves (brighter), and the Ctrl/Alt/Tab
  modifiers plus the two other scene-view mouse gestures (camera rotate,
  zoom) underneath (dimmer, secondary). Purely a static text overlay, not
  interactive. (Tab's own mention was added alongside the "slingshot
  targeting" feature below -- see there.)
- **Sling gesture: "slingshot" targeting for the orbit shot** -- while
  pulling (Shift+drag, Ctrl held for orbit mode), tap **Tab** repeatedly
  to cycle the orbit shot's center through every other currently active
  object in the scene and back to the world origin ("Center"), like
  choosing which body a spacecraft's gravity-assist flyby swings around.
  Previously the orbit shot was unconditionally centered on the origin
  (a deliberate simplification at the time, see that entry's own
  comment). New `TrajectoryEngine::startOrbit()` parameter
  `referenceObjectId` (default `-1`, fully backward compatible --
  unchanged behavior for the existing 4/5-argument call the double-click
  gesture still uses): when set, the object's `orbitReferenceObjectId`
  is set instead of resetting it, so the launched orbit tracks that
  object's LIVE, possibly-moving position every control-rate tick
  (`TrajectoryEngine::integrate()` already re-read this field every tick
  for the ordinary orbit-reference-object feature -- this reuses that
  existing mechanism rather than adding a second one).
  - New pure function `SlingGesture::cycleSlingReference()`: given the
    current selection, the currently active object ids, and the slung
    object's own id (excluded from the cycle -- it can't slingshot
    around itself), returns the next selection in `[Center, id, id, ...]`,
    wrapping around; recovers to the first real candidate rather than
    getting stuck if the current selection is no longer in the active
    list (e.g. an object deactivated mid-gesture). Kept as pure,
    JUCE-free logic in `SlingGesture.h` (same "shared, not reimplemented"
    principle as the rest of that header) rather than embedded directly
    in `PluginEditor`, specifically so it stays headlessly testable.
  - Resets to "Center" at the start of every sling gesture
    (`startSling()`); tracked independently of `slingWantsOrbit` (Ctrl),
    so a target can be picked before or after switching into orbit mode.
  - The dashed orbit preview (already live while aiming) and the
    on-screen "Orbit: <shape>" label (gained a second line, "around
    Center" / "around Object N") both immediately reflect the current
    Tab selection, including tracking a moving target live while
    pulling, not just once the shot actually fires.
  - `Tools/verify_orbit` gained 8 checks: `startOrbit()`'s new parameter
    is stored correctly, an orbit with a `referenceObjectId` actually
    tracks a moving reference object's live position over time (not a
    snapshot at call time), the default (`-1`) still resets any leftover
    reference id exactly as before, and `cycleSlingReference()`'s cycle
    order/wraparound/self-exclusion/stale-selection-recovery (24/24
    checks passing).
  - Known caveat: Tab is a common OS/host UI-navigation key. Some DAW
    hosts may intercept it before it reaches the plugin editor, in which
    case this shortcut simply won't fire in that host -- no crash or
    silent misbehavior, just a no-op. Not something a plugin can fully
    control; noted in "Known limitations" below.
- **App/plugin icon and vendor name.** `Assets/AppIcon.png` (1024x1024,
  editable vector source at `Assets/AppIcon.svg`) is now baked into a
  proper `.icns` for both the Standalone `.app` and the VST3 bundle at
  build time, via `ICON_BIG`/`ICON_SMALL` in `CMakeLists.txt`'s
  `juce_add_plugin()` call -- JUCE's own icon tooling (`juceaide`) parses
  the source image directly (SVG or raster) and rescales it as needed
  per icon size, so a single square master image covers both slots, same
  as JUCE's own example projects. The `.icns` itself is generated fresh
  every build, not checked into the repo.
  - `COMPANY_NAME` changed from the placeholder `"YourName"` to
    `"Jan Glueck"` -- this is what a host's plugin browser (e.g. Reaper)
    shows as the vendor/manufacturer, read from the VST3's own
    `moduleinfo.json` (`"Vendor": "Jan Glueck"`, verified in the built
    bundle). `PLUGIN_MANUFACTURER_CODE` updated to match (`Yrnm` -> `Jgck`,
    a 4-character Steinberg-style code derived from the new name) --
    changing this changes the plugin's persistent VST3 class ID, which is
    fine to do now (still pre-release, no real users/presets depend on
    the old identity) but would NOT be a safe change to make later after
    any real release.
  - Added an explicit `BUNDLE_ID "com.janglueck.klangorbit"`: JUCE's
    own default bundle ID is derived from `COMPANY_NAME`, which now
    contains a space and would otherwise produce an invalid (space-
    containing) bundle identifier -- caught immediately via a CMake
    configure-time warning, fixed before ever building.
  - Verified: both the Standalone `.app` (`Contents/Resources/AppIcon.icns`
    correctly referenced via `CFBundleIconFile`) and the VST3 bundle
    (`Contents/Resources/AppIcon.icns`, `moduleinfo.json`'s `Vendor` field)
    checked directly in the built artifacts, plus the usual full
    rebuild/test-suite/app-launch check.
- **Sling gesture: Slingshot mode -- a real, physics-based gravity assist,
  not a scripted path.** Supersedes/extends the earlier "slingshot
  targeting" entry above: that one only let Orbit Shot's scripted ellipse
  center on a chosen object, which turned out not to be what "slingshot
  maneuver" actually meant -- an object thrown so it passes near another,
  gets deflected by that object's real gravity, and continues on a new
  course or gets captured into an orbit, as an emergent RESULT of the
  physics rather than a chosen shape. The Tab-based reference-selection
  infrastructure from that earlier entry is kept and reused here, now
  serving two different interpretations depending on the launch mode.
  - The sling gesture's Ctrl key now CYCLES through three launch modes
    (`PluginEditor::SlingLaunchMode`: FreeThrow -> OrbitShot -> Slingshot
    -> FreeThrow) instead of toggling two. Every gesture starts on
    FreeThrow regardless of Ctrl's state when the drag begins -- only an
    actual fresh press advances anything, unifying Ctrl's edge-detection
    behavior with Alt's (previously slightly inconsistent: Ctrl used to
    read its *held* state at gesture start, Alt never did).
  - New `SoundObject::slingshotTargetId`/`slingshotStrength`: a one-off,
    gesture-set pull toward another object's LIVE position, integrated by
    a small, self-contained addition to the existing
    `TrajectoryEngine::computeAttractionForce()` (same inverse-square law
    and `gravityLikeConstant`, now `public` specifically so the preview
    code below can share it too, fixed small softening floor). Kept fully
    separate from `SoundObject::attractionStrength` on purpose: firing a
    Slingshot never mutates the target object's own, independently
    configured Attraction settings -- the strength is fixed
    (`SlingGesture::slingshotGravityStrength`) and scaled only by the
    target's real `Mass` (already user-adjustable), not by pull distance
    (which already controls launch *speed*, same as Free Throw -- tying
    gravity strength to the same gesture dimension would read as two
    controls fighting over one drag).
  - `TrajectoryEngine::throwObject()` gained optional
    `slingshotTargetId`/`slingshotStrength` parameters (default `-1`/`0`),
    mirroring `startOrbit()`'s existing `referenceObjectId` pattern
    exactly: the default clears any leftover pull from a previous throw
    of the same object, the same stale-state hazard already fixed once
    for `orbitReferenceObjectId`.
  - Not serialized by `PresetManager` -- transient, gesture-driven runtime
    state, same treatment as `orbitPhase`/`attractionPulsePhase`/
    `velocity` (explicitly reset to inert defaults on preset load, so a
    freshly loaded object never keeps a leftover mid-flight pull).
  - New `SlingGesture::simulateSlingshotPreview()`: forward-simulates the
    Slingshot preview a couple of seconds ahead with the SAME force
    law/constant `computeAttractionForce()` uses (simple Euler
    integration, target treated as momentarily fixed for the short
    preview horizon -- the same kind of simplification the pre-existing
    Free Throw preview already documents for itself). A real, physically
    accurate preview, not an approximation -- 0 strength (no target
    selected) collapses it to the same straight line Free Throw draws.
  - Picking "Center" (no real object) for Slingshot has no gravity-well
    meaning, so it degrades gracefully to a plain, unaffected throw
    rather than needing special-casing anywhere. Switching INTO Slingshot
    while still on "Center" auto-selects the first available object
    instead of silently doing nothing, if one exists -- directly
    addresses the exact confusion that prompted this feature (a user
    testing the earlier Tab-cycling entry with only one active object in
    the scene had nothing to cycle to, and no other feedback that this
    was expected).
  - `Tools/verify_orbit` gained 6 checks: `throwObject()`'s new
    parameters are stored correctly and the default clears a stale pull,
    a pulled throw is measurably deflected toward the target compared to
    an otherwise-identical unaffected throw (direct force-integration
    check, not just that the fields are set), and
    `simulateSlingshotPreview()`'s zero-strength/nonzero-strength/
    always-finite behavior (30/30 checks passing).

### Changed
- **"Grain Rate" renamed to "Spawn Rate" in the Grains parameter
  category** (`README.md`, `Docs/UserGuide.md`, the in-app Help window)
  -- reported as unclear/confusable with Grain Duration. The internal
  field name (`GrainCloudSettings::grainRate`, the `grainRate` preset
  JSON key) is deliberately unchanged, same reasoning as the "Grains"
  rename below.
- **"Mute Original Audio" renamed to "Grains Only (Mute Original
  Audio)"** in the Grains parameter category, README, UserGuide, and
  the in-app Help window -- clearer at a glance about what you'll
  actually hear with it on.
- **"Grain Cloud" renamed to "Grains" everywhere it's shown to the
  user** -- the parameter panel's category tab, `README.md`,
  `Docs/UserGuide.md`, and the in-app Help window's content
  (`Source/HelpContent.h`), including fixing two pre-existing wrong
  section-number cross-references found while editing ("(see section
  7)" -> the actual Grains section in each document). The internal
  C++ identifiers (`GrainCloud` class, `GrainCloudSettings`, the
  `grainCloud` preset JSON key, the `verify_grain_cloud` CLI tool) were
  deliberately left as-is -- renaming those would touch many more
  files for an internal-only detail no user ever sees, and renaming
  the JSON key specifically would risk breaking existing saved
  presets, unlike a display-label change.
- **Renamed the product from "Spatial Audio POC" to "Klangorbit"** --
  applied consistently everywhere it's visible or referenced: the plugin
  name shown in a DAW's plugin list/browser (`PRODUCT_NAME`,
  `KlangorbitProcessor::getName()`), the CMake project/target names
  (`Klangorbit`, `Klangorbit_VST3`, `Klangorbit_Standalone`), the C++
  class names (`SpatialAudioPOCProcessor`/`SpatialAudioPOCEditor` ->
  `KlangorbitProcessor`/`KlangorbitEditor`), the bundle identifier
  (`com.janglueck.spatialaudiopoc` -> `com.janglueck.klangorbit`), the
  4-char plugin code (`Sapc` -> `Klor`; `PLUGIN_MANUFACTURER_CODE` stays
  `Jgck`, that one identifies the developer, not the product), and every
  mention across `README.md`/`CHANGELOG.md`/`PROJECT_BRIEF.md`. The
  repository folder on disk was deliberately left as-is (a filesystem
  rename is a separate, riskier operation with no code-correctness
  benefit -- nothing reads the checkout folder's own name).
  - The bundle ID change means this is, as far as a DAW is concerned, a
    different plugin from the one previously installed under the old
    name/ID -- it will show up alongside (not replacing) any old
    `Spatial Audio POC.vst3` a host had already scanned. The old bundle
    at `/Library/Audio/Plug-Ins/VST3/Spatial Audio POC.vst3` was removed
    as part of this change (see build verification below) so a rescan
    only finds the new one.
  - Verified via a clean rebuild of both targets (zero renamed-symbol
    leftovers -- confirmed no remaining `SpatialAudioPOC`/`Spatial Audio
    POC` occurrences anywhere in the tracked source tree), the full
    `Tools/verify_*` suite (untouched by this rename, all still passing),
    and the standalone app launching and staying stable under the new
    name.
- **Solo/Mute controls moved to the object-list sidebar, removed from the
  parameter panel.** Each row in `Source/ObjectListPanel.h/.cpp` now has
  its own Mute/Solo buttons next to the select button, writing directly
  into the corresponding `SoundObject::muted`/`soloed` -- the underlying
  fields, ramping, and audio-thread logic (`MuteSoloLogic.h`) are
  unchanged from `feature/solo-mute-objects`, only the control surface
  moved. The parameter panel's Object category no longer has "Muted"/
  "Soloed" checkbox rows, to avoid the same two fields being editable
  from two different places in the GUI. This also resolves the
  previously-open follow-up ("hook Solo/Mute into the object-list
  sidebar") from the earlier entry above -- it's the sidebar now, not a
  second copy in the parameter panel.
- Object-list sidebar width raised 160 -> 190px to fit the new Mute/Solo
  buttons alongside each row's label without crowding it.
- **Room boundary now renders as a shaded, translucent sphere** instead
  of a flat wireframe outline (`PluginEditor::drawShadedBoundarySphere()`).
  Still no 3D mesh/lighting model (no OpenGL, see "3D camera view") --
  a cheap "fake sphere" trick instead: `Camera3D::projectSphereSilhouetteRadius()`
  (new method) computes the sphere's screen-space silhouette radius
  exactly, not approximately, exploiting the fact that this camera always
  looks directly at the world origin -- an origin-centered sphere is
  therefore always exactly on the optical axis, so its projected
  silhouette is a true circle centered on the viewport middle for any
  camera angle/zoom (a general off-axis sphere would project to an
  ellipse under perspective; this one never needs to). A radial gradient
  (transparent center -> semi-opaque rim) gives a "translucent shell"
  look that keeps objects/grains inside fully visible while still
  reading clearly as a spatial boundary. Two specular-highlight attempts
  (first a fixed screen-space offset, then reworked to project a fixed
  world-space light direction through the camera via
  `Camera3D::computeSphereHighlight()` so it would move correctly as the
  view rotates -- see prior revisions of this file) were both tried and
  ultimately removed again: neither read well visually, and the gradient
  alone is enough to suggest a sphere without one.
  `Tools/verify_camera` gained checks for the new method: succeeds/fails
  correctly (no silhouette when the camera is at/inside the sphere),
  larger spheres project larger, and the exact tangent-based geometry
  cross-checks against the already-verified linear `worldSizeToScreenSize()`
  estimate in the small-angle limit (27/27 checks passing).
- `Camera3D::maxDistance` raised from 30 to 150 meters -- generous enough
  to fit a much larger-than-default custom `SceneSettings.roomSize`
  (default 5m) in frame; reviewed alongside the zoom speed
  (`PluginEditor::mouseWheelMove()`'s multiplicative sensitivity), which
  was already implemented and needed no change. No second zoom
  implementation was added -- the mouse wheel already controlled
  `Camera3D`'s distance from `feature/3d-view`; this only adjusts its
  bounds.
- Clicking an object (without dragging) selects it for the parameter
  panel, without affecting its motion mode anymore -- previously every
  click was immediately translated into `Mode::Manual` (`beginDrag()` in
  `mouseDown()`), which would have reset a running orbit/impulse to
  `Static` on every selection click.
- **Full GUI visual redesign: dark, cyan-accented, reduced "sci-fi HUD"
  theme, applied consistently across the whole editor instead of relying
  on JUCE's stock look.** See the README's new "GUI design" section for
  the summary; details:
  - New `Source/UiTheme.h` -- one shared set of colour/spacing tokens
    (backgrounds, borders, the cyan accent, text tones, Mute/Solo's
    red/amber, a 4/8/12/16/24px spacing scale) that `PluginEditor`,
    `ParameterPanel`, and `ObjectListPanel` all read from, replacing each
    file's own ad hoc `juce::Colours::lightgrey`/`darkgrey`/etc. calls.
  - New `Source/SciFiLookAndFeel.h/.cpp` (`juce::LookAndFeel_V4`
    subclass), applied once to the top-level editor
    (`setLookAndFeel()`/`setLookAndFeel(nullptr)` in its
    constructor/destructor) so every child component inherits it
    automatically. Deliberately conservative in scope: mostly just
    recolours JUCE's own stock V4 widget shapes via `setColour()` (safe,
    well-tested colour cascading, essentially zero geometry risk), plus
    exactly three simple hand-drawn overrides where the stock shapes
    didn't fit the brief -- flat buttons with an accent-coloured
    underline for the active state, a thin-track slider (flat fill +
    a thin vertical tick thumb) instead of a filled pill + circular
    knob, and a pill-style toggle switch instead of a checkbox tick.
    Kept intentionally minimal: without an interactive GUI session
    (still not available in this environment) a geometry bug in custom
    LookAndFeel drawing code is very hard to catch, so anything not
    clearly worth that risk was left as V4's default, just recoloured.
  - The "active" button treatment (accent border + underline stripe)
    derives its colour from the button's own `buttonOnColourId` rather
    than a single hardcoded colour -- the same code path handles cyan
    (category tabs, the object-list's selection button) and Mute/Solo's
    red/amber (`ObjectListPanel`, now driven by real `Button` toggle
    state instead of manually swapping `buttonColourId` by hand) without
    special-casing either.
  - `PluginEditor`'s toolbar, `ObjectListPanel`, and `ParameterPanel` each
    now paint their own solid panel background plus a hairline border
    where they meet the 3D viewport or each other, so the window reads as
    distinct regions instead of controls floating over one shared black
    canvas. Toolbar height 64 -> 76px (two even 38px rows), object-list
    width 190 -> 208px, parameter-panel width 340 -> 360px, and every row
    component (`FloatRowComponent`/`Vec3RowComponent`/`ComboRowComponent`/
    `ToggleRowComponent`) gained a small label-to-control gap plus taller
    preferred heights -- all specifically for more breathing room, per
    the request ("ausreichend padding für Felder, Buttons, Text").
  - 3D-viewport recolouring: the ground-reference grid and the boundary
    sphere both moved from plain grey/dark-red to the theme's grid/accent
    colours; the object-selection ring moved from white to the cyan
    accent; the free-throw sling preview moved from orange to the same
    accent (kept distinct from the existing violet orbit-shot preview).
  - Per-object colours (`objectColour()`) are now restricted to a blue ->
    violet -> magenta hue band instead of the full hue wheel, still via
    the same golden-ratio stepping as before. Two reasons: staying within
    one hue family reads as more cohesive with the rest of the theme, and
    it keeps every object colour clear of both the cyan accent (an object
    landing on that exact hue would make its own selection ring nearly
    invisible against its fill) and Mute/Solo's red/amber.
  - No functional/audio changes anywhere in this entry -- purely visual.
    Verified via full rebuild (zero warnings), all `Tools/verify_*` +
    `validate_presets` still passing (none of them touch GUI code), and
    the standalone app launching and staying stable; the actual on-screen
    result is unverified, see "Known limitations" below.

### Fixed
- **Investigated a report that "grain duration also affects spawn
  rate."** Traced `GrainCloud::update()`'s spawn-scheduling code
  carefully: the spawn-interval timer only ever reads `grainRate`
  ("Spawn Rate"), never `grainDuration` -- the two are, and always
  were, independent at the scheduling level. The actual, real
  interaction is one step removed: `maxConcurrentGrains` is a hard cap
  on simultaneously-alive grains, and a grain occupies its pool slot
  for the full `grainDuration`, so if `grainRate * grainDuration`
  (the natural steady-state overlap count) exceeds that cap, new
  spawns stall until an old grain expires and frees a slot --
  throttling the *effective* spawn rate below the configured
  `grainRate` without `grainRate` itself having changed. Raising
  `grainDuration` alone (leaving `maxConcurrentGrains` at its default
  of 8) is exactly the scenario that triggers this, which is almost
  certainly what was actually observed.
  - No scheduling-logic change needed (verified as already correct,
    see the new test below) -- addressed by making the relationship
    explicit and hard to miss: an expanded class comment on
    `GrainCloudSettings` (`Source/Grain.h`), inline comments at the
    exact point in `GrainCloud::update()` where the throttling
    happens, and equivalent explanations in `README.md`,
    `Docs/UserGuide.md`, and the in-app Help window's Grains section.
  - `Tools/verify_grain_cloud` gained
    `testSpawnRateIndependentOfDuration()`: with `grainRate=1`,
    `grainDuration=4` and `maxConcurrentGrains` sized to match (4, i.e.
    the cap has no headroom problem), confirms ~4 grains overlap
    simultaneously as expected, then keeps running well past the
    initial fill (8.5s total) and confirms pool slots keep getting
    reused at the same ~1/sec cadence throughout -- proving spawn rate
    is sustained over time, not just achieved once at start-up.
- **Slingshot mode: thrown objects fell almost straight into the target and
  jittered there instead of swinging past or settling into a smooth orbit.**
  Reported right after the Slingshot mode above shipped: the bow-line
  preview looked right, but the actual release didn't follow it. Root
  cause was that the thrown object's own `damping`/`dragCoefficient` --
  tuned for ordinary decelerating throws elsewhere in the scene -- were
  also being applied while a slingshot pull was active. A real
  gravity-assist maneuver is (approximately) frictionless: with drag
  active, the throw's own launch velocity decayed to a fraction of its
  original size within well under a second, while the pull itself (never
  damped, since it's a continuously reapplied force, not a velocity) kept
  re-accelerating the object toward the target -- so the dominant
  direction of motion became "toward the target" almost immediately
  regardless of the throw's aim, and once close, the inverse-square force
  spiking against a too-small softening floor (0.05 m, tuned for the
  ordinary n-body attraction system's much gentler typical strengths) made
  the fixed-timestep integrator overshoot and bounce back and forth --
  the reported "jitter".
  - `TrajectoryEngine::integrate()`'s Impulse case now skips both
    `damping` and `dragCoefficient` entirely while
    `SoundObject::slingshotTargetId` is active, treating the pull as real,
    (nearly) energy-conserving space flight -- matching what the preview
    (`SlingGesture::simulateSlingshotPreview()`) already assumed and drew,
    which is now actually true instead of an inaccurate simplification.
  - The slingshot pull's own softening floor
    (`TrajectoryEngine::computeAttractionForce()`) raised from 0.05m to
    0.3m, giving the fixed-rate integrator enough margin at closest
    approach to stay numerically stable without a hard force spike.
  - `SlingGesture::slingshotGravityStrength` tuned down from 8.0 to 4.0 --
    at typical throw speeds (a few m/s) and scene scale (~1m), 8.0 let the
    pull dominate the throw's own velocity almost instantly regardless of
    aim; 4.0 still allows a close/slow throw to be captured into a real
    orbit (a legitimate outcome, not a bug) while leaving room for a
    fast/wide throw to actually fly by.
  - Verified with two hand-run scenarios via `TrajectoryEngine` directly
    (not just the preview math): a fast, wide throw now swings past the
    target and continues on a new course (a real flyby); a slow, close
    throw settles into a stable, periodic bound orbit (closest/farthest
    approach oscillating between fixed bounds indefinitely, not decaying
    or diverging) -- neither collapses onto the target.
  - `Tools/verify_orbit` gained a regression test isolating the root
    cause: an object with heavy `damping`/`dragCoefficient` loses most of
    its speed on an ordinary throw but keeps it (frictionless) once a
    slingshot pull is active, even when the pull's actual force is
    negligible (target placed far away) -- 32/32 checks passing.
  - Verified headlessly (the two scenarios above, plus the full
    `Tools/verify_*` suite and a standalone app launch/stability check);
    still not verified by ear/eye in a real DAW session.
- **Grain click/discontinuity bug, reported as happening with `pitchJitter`.**
  Investigated both suggested hypotheses rather than assuming either was
  correct:
  - *Non-interpolated ring-buffer read position* -- checked and already
    correctly implemented (`GrainRenderer.h` already does linear
    interpolation between adjacent samples via a fractional read
    position); this was not the cause.
  - *Envelope not reliably reaching exactly 0* -- this WAS a real bug,
    but a different mechanism than described: the Hann window's
    denominator was `grainLengthSamples`, so the last actually-rendered
    sample's phase fell just short of a true zero-crossing (residual
    ~`(pi/grainLengthSamples)^2` of peak amplitude -- e.g. ~10% for a
    ~2ms/100-sample grain), which then jumped to a hard `0.0` on the next
    call -- a real, audible discontinuity. Crucially, this is
    rate-*independent* (`grainLengthSamples` is fixed in output samples
    regardless of `playbackRate`, see `Grain.h`), so it affected every
    short grain, not specifically pitch-shifted ones -- `pitchJitter` is
    presumably how it was noticed, since jitter naturally produces the
    short, dense grain clouds where the residual is largest relative to
    peak amplitude, not because pitch itself was the cause.
  - Fixed in `GrainRenderer.h` by using `grainLengthSamples - 1` as the
    envelope denominator, so the last sample's phase reaches exactly 1.0
    (envelope exactly 0.0) by construction, for any grain length.
  - `Tools/verify_grain_cloud` gained a direct, empirical measurement:
    worst-case tail residual and tail-to-silence jump across
    `playbackRate` 0.1..2.0 (the full range `pitchJitter` can actually
    produce) for a short (100-sample) grain -- both now measure exactly
    `0.00000` for every tested rate, confirming the fix holds at the
    extremes the bug report specifically asked to re-check, not just at
    `rate=1.0`.
- `TrajectoryEngine::startOrbit()` always sets an explicit, fixed orbit
  center, but `integrate()` prefers a valid `orbitReferenceObjectId`'s
  live position over `orbitCenter` whenever one is set -- a leftover
  reference id from earlier `ParameterPanel` editing could therefore
  silently override the center `startOrbit()` was just told to use,
  affecting both the double-click orbit gesture and the sling orbit-shot
  (both of which document "always centered on the origin"). Found while
  building the sling orbit preview (which assumes the same guarantee, to
  match what a release actually produces). Fixed by resetting
  `orbitReferenceObjectId` to -1 inside `startOrbit()` itself, covering
  every caller. `orbitPlaneNormal` is deliberately left untouched (no
  such conflict -- a tilted plane around an explicit center is coherent).
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
  performance trade-off (see Added, above), not a bug. The optional
  per-grain Doppler approximation (`GrainCloudSettings::dopplerEnabled`,
  see above) is a deliberately cheap partial exception to this, not a
  full propagation pass.
- Grain window shape is Hann only; no other envelope shapes yet.
- `maxConcurrentGrains` is enforced per cloud and globally, but the
  global budget is currently a first-come-first-served allocation across
  clouds each control-rate tick, not prioritized by e.g. object gain or
  distance to the listener.
- `maxConcurrentGrainsGlobal = 128` is a rough operation-count estimate
  (see the "Grain density and duration raised" entry above), not a
  measurement on real audio hardware -- not available in this
  environment. The CPU-load percentage in the toolbar
  (`KlangorbitProcessor::getEstimatedCpuLoad()`) exists specifically
  so this can be checked under real load; it has not been checked here.
- Interactive/visual feel across the GUI is largely unverified by hand
  (no way to drive a running JUCE GUI or take a real screenshot of it in
  this environment -- screen-capture tooling here only ever captures the
  agent's own chat window, not the actual screen the app opens on).
  Concretely unverified: the 3D camera's drag/zoom direction sign
  conventions (the underlying projection math is verified via
  `Tools/verify_camera`, only the polarity itself is a guess -- each is a
  one-line sign flip in `PluginEditor` if it feels inverted), the shaded
  boundary sphere's actual on-screen appearance (gradient/transparency
  tuning), the object-list sidebar's row layout including its Mute/Solo
  buttons, all new parameter-panel slider ranges/step sizes, and the
  audible result of every new audio feature (grain Doppler, Solo/Mute
  fades, grain read-depth range) -- all of these are only verified
  mathematically/numerically (`Tools/verify_*`), never by ear or by eye.
  Applies in full to the entire "GUI design" overhaul (see the README
  section of that name): the whole dark/cyan `SciFiLookAndFeel` theme,
  every panel's new padding/spacing, and the three hand-drawn control
  shapes (buttons, sliders, toggle switches) were built and reasoned
  through carefully but never actually seen rendered on a screen.
- Object dragging is still constrained to the ground plane (z=0), now via
  a camera ray cast rather than a fixed formula, but still no direct way
  to drag an object's height with the mouse.
- Building only the `Klangorbit_Standalone` target does NOT update
  the VST3 copy in the system plugin folder -- that requires building
  the separate `Klangorbit_VST3` target (`COPY_PLUGIN_AFTER_BUILD`
  only fires for that target). A DAW loading an old VST3 build will not
  reflect recent source changes even though the standalone app does.
- The sling gesture's new slingshot-targeting shortcut (Tab, see Added
  above) may be intercepted by some DAW hosts before it ever reaches the
  plugin editor -- Tab is a common OS/host UI-navigation key, and this
  plugin has no way to claim it exclusively. Where that happens the
  shortcut is simply a no-op in that host (cycling through Ctrl/Alt still
  works normally); the standalone app is not affected by any host-level
  interception.

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
