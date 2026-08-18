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
    (`SpatialAudioPOCEditor::mouseDrag()`/`mouseWheelMove()`). Dragging an
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
    (`SpatialAudioPOCProcessor::getEstimatedCpuLoad()`), turning
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

### Changed
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
  (transparent center -> semi-opaque rim) plus a small offset highlight
  gives a "translucent shell" look that keeps objects/grains inside fully
  visible while still reading clearly as a spatial boundary.
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

### Fixed
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
  performance trade-off (see Added, above), not a bug.
- Grain window shape is Hann only; no other envelope shapes yet.
- `maxConcurrentGrains` is enforced per cloud and globally, but the
  global budget is currently a first-come-first-served allocation across
  clouds each control-rate tick, not prioritized by e.g. object gain or
  distance to the listener.
- The 3D camera's drag/zoom direction sign conventions are untested by
  hand (no interactive GUI testing available in this environment) -- the
  underlying projection math is verified (`Tools/verify_camera`), but the
  drag/scroll polarity itself is a reasonable guess that may feel
  inverted; each is a one-line sign flip in `PluginEditor` if so.
- Object dragging is still constrained to the ground plane (z=0), now via
  a camera ray cast rather than a fixed formula, but still no direct way
  to drag an object's height with the mouse.

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
