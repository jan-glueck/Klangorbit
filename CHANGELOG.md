# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
versioning follows [SemVer](https://semver.org/lang/en/) -- while the major
version is 0, the rule is: every minor version (0.X.0) may break presets
(see Presets/schema/), patch versions (0.X.Y) may not.

## [Unreleased]
### Added
- **Input routing matrix** -- each object's live-input signal is no
  longer a fixed 1:1 mapping to its own channel. A new "Audio
  Routing..." window (inputs x objects grid) lets any object sum one or
  more input channels, or move off its own channel entirely.
  `SoundObject::routedInputChannels` is optional and empty by default,
  so an object nobody has routed behaves exactly as before. Resolution/
  mixing logic lives in `Source/InputRouting.h`, verified via
  `Tools/verify_input_routing.cpp`. Persisted optionally, no
  schemaVersion bump.
- **Settings menu** -- the toolbar's separate Mappings.../Output...
  buttons are now one "Settings..." button with a popup menu
  (Mappings.../Output.../Audio Routing...), keeping the toolbar from
  growing a fourth button. Snapshots stays its own toolbar button.
- **Shared randomization foundation** (`Source/RandomEngine.h`) --
  `SeededRandom` (an explicit, persistable-seed RNG wrapper),
  `RandomizedParam` (base/spread/distribution: Uniform, Normal, Ramp,
  LogUniform, PowerLaw, Discrete, with optional quantization to a value
  set), and `MeanReverting::step()` (an Ornstein-Uhlenbeck process
  extracted from what used to be `TrajectoryEngine`'s own orbit-radius
  code, now a shared primitive reused throughout the features below).
- **`GrainCloud` and every object's orbit-radius mean-reversion each own
  a seeded, preset-reproducible RNG**, replacing a single scene-wide
  shared one -- a grain pattern or orbit "breathing" found by ear
  reloads exactly, and two objects with identical settings no longer
  wander in lockstep. New `GrainCloudSettings::seed`/`SoundObject::
  orbitNoiseSeed` fields, saved as decimal strings for exact 64-bit
  round-tripping; optional, no schemaVersion bump. `ParameterPanel`'s
  Grain Cloud category shows the current seed and a "New Seed" button.
- **Shared per-voice filter module** (`Source/VoiceFilter.h`) -- a
  multi-mode resonant filter (LowPass/HighPass/BandPass/Notch,
  zero-delay-feedback/"TPT" topology, numerically stable under
  continuous coefficient changes) cheap enough to give every active
  voice its own instance. Cutoff/resonance are randomizable per voice
  at spawn, with optional continuous drift. Verified via
  `Tools/verify_voice_filter.cpp`.
- **Per-grain filter** (`GrainCloudSettings::filter`) wires the
  per-voice filter into `GrainCloud`'s grains -- each grain samples its
  own cutoff/resonance at spawn, with optional drift; off by default.
  Persisted optionally, no schemaVersion bump. Verified via
  `Tools/verify_grain_cloud.cpp`/`verify_grain_filter_preset.cpp`.
- **`DelayInstance`: a multi-tap delay with real feedback, as its own
  spatial sound source** (`Source/DelayInstance.h/.cpp`) -- genuinely
  separate from `GrainCloud`: taps read from a buffer that recirculates
  its own past output, so echoes decay over repeated passes. `numTaps`
  (1-8) taps are multiple read offsets into one buffer, not independent
  feedback paths; `feedbackDamping` darkens the recirculating signal via
  a shared filter. Taps sit evenly around a circle (`tapSpreadRadius`)
  centered on the parent, optionally orbiting it
  (`tapOrbitAngularSpeed`). **Grains and Delay are mutually exclusive
  per object** (UI-enforced). Persisted optionally, no schemaVersion
  bump. Verified via `Tools/verify_delay_instance.cpp`/
  `verify_delay_instance_preset.cpp`.
- **Modifier stack**: an ordered, stackable per-voice list of modifiers
  (`Source/Modifier.h`), one independent stack each for `GrainCloud` and
  `DelayInstance`, up to 8 slots. Six types: **Gain**/**Time Offset**
  (commutative scalars -- order never matters), **Filter** (cascaded in
  stack order -- the one case where reordering is audible), **Pitch**
  (GrainCloud-only -- a delay tap is a continuous readback with no
  resampler to pitch-shift it), **Movement** (additive Linear/Rotating
  drift on top of a voice's own primary movement, via Rodrigues'
  rotation for Rotating), and **Spatial Offset** (five shapes -- Sphere,
  Spherical Shell, Ring, Line, Cuboid -- sampled once per voice and held
  constant for its life). Every value is a `RandomizedParam`. New
  `ModifierStackEditor` UI (reorderable Add/Remove/Up/Down rows). A
  separate **generation mode** (`GrainCloudSettings::generationMode`:
  `Emitter`, the existing continuous spawn, or `Group`, a whole burst of
  `groupSize` grains per interval with optional per-grain start-delay
  spread) rounds out the stack. Persisted optionally throughout, no
  schemaVersion bump. Verified via `Tools/verify_modifier_stack.cpp`,
  integration tests in `verify_grain_cloud.cpp`/`verify_delay_instance.cpp`,
  and preset round-trip tests.
- **Agitation**: a toggleable random wobble around a parameter's
  current value, for any registered parameter (`Source/Agitation.h`,
  `Source/AgitationEngine.h`), built on the same mean-reverting process
  as orbit-radius noise and the per-voice filter's drift. Generic over
  parameter type -- drives every enabled parameter through
  `ParameterRegistry`'s own `getValue()`/`setValue()`, with no
  per-parameter-type integration code. Rate can sync to a tempo
  division, using the host's own tempo when available or a
  Standalone-only BPM fallback otherwise. New "Agitation" category in
  the parameter panel, one row per eligible parameter grouped by
  category, collapsed until enabled. Persisted as part of the scene
  preset, no schemaVersion bump; required adding a "Delay" category to
  `ParameterRegistry` (`DelayInstanceSettings` had never been
  registered there). Verified via `Tools/verify_agitation.cpp`/
  `verify_agitation_engine.cpp`/`verify_agitation_preset.cpp`.
- **Snapshots**: capture/recall a whole parameter set
  (`Source/SnapshotManager.h/.cpp`) -- a snapshot is a captured
  `{parameter id -> value}` list, scoped by category, with its own file
  format and `schemaVersion` (`Snapshots/schema/README.md`), kept
  deliberately separate from both the scene-preset and mapping-profile
  schemas. New "Snapshots..." toolbar window: a category checklist,
  Load/Save Snapshot (loading recalls immediately). `SnapshotMorphEngine`
  soft-crossfades between two loaded snapshots ("Load A"/"Load B", not
  auto-recalled, plus a Morph/Smoothing slider pair, smoothed by one
  shared filter) -- only parameters present in both snapshots are
  morphed. `randomVariant()`/`randomizeEverything()` produce a
  Strength-scaled or fully randomized variant of a snapshot. Verified
  via `Tools/verify_snapshot_manager.cpp`/`verify_snapshot_morph_engine.cpp`.

### Fixed
- **Modifier row's Enabled toggle overlapped the type dropdown** in the
  Delay/Grain modifier stack editor -- a missing gap between the two
  controls.
- **Grain's Delay-mutual-exclusion hint label was easy to miss** -- it
  was registered far down the Grain category's layout instead of
  immediately after its own Enabled toggle, unlike Delay's equivalent
  hint.

## [0.2.0] - 2026-09-17
### Added
- **GitHub Actions Windows build** -- builds the VST3 + Standalone on a
  hosted runner on every push, uploading both as downloadable artifacts.

### Fixed
- **One object's grains could permanently starve every other object's
  grains.** Root cause: the scene-wide grain budget was handed out
  first-come-first-served in object-index order each tick, so an early
  object's cloud could permanently consume the entire remaining budget
  once it reached its own setting. Fixed by splitting the remaining
  headroom proportionally to what each enabled cloud's own setting
  actually asks for, recomputed every tick.

### Added
- **`ParameterPanel`'s Grain Cloud category warns when the shared grain
  budget is actively being split between multiple objects** -- without
  it, a cloud using fewer grains than configured could read as a bug
  rather than expected shared-budget behavior.
- **Windows build support (VST3 + Standalone).** Added
  `Source/GamepadBridge_Windows.cpp`, an XInput-based gamepad backend
  alongside the existing macOS GameController one, plus per-platform
  CMake build/install configuration. Not yet compiled/tested on a real
  Windows machine.

### Changed
- **Scene-wide grain budget ceiling raised 256 -> 2000**
  (`GrainLimits::maxConcurrentGrainsGlobal`) -- the original ceiling was
  sized conservatively around 5th-order-Ambisonics-per-grain cost, which
  most output formats don't approach. The toolbar's own CPU meter is the
  real safety net to tune against. Each object's own per-cloud default
  stays at 256.
- **Output Format naming unified** across the dropdown and docs to one
  "Name (qualifiers, channel count)" pattern; the FOA/SOA/TOA
  abbreviations were removed in favor of spelled-out order names.
- **Quad/Octophonic/Circular Array/5.1/7.1/the four Atmos-bed formats
  now pan each object directly to the real speakers via VBAP**, instead
  of decoding a shared, fixed-order-3 Ambisonics bus first. Root cause
  of the earlier poor pan sharpness: order-3 Ambisonics has an inherent
  beamwidth (~35-40°) that spreads meaningful energy across every real
  speaker once there are only a handful of them -- not fixable by a
  better remap algorithm. New `AmbisonicsEncoder::panDirectBlock()`/
  `AmbisonicsDecoder::computeDirectPanGains()` bypass the shared bus
  entirely. Stereo, Binaural, and the raw Ambisonics outputs are
  unaffected. A real, intentional change in character (sharper
  point-source panning instead of a diffuse decode). Verified via
  `Tools/verify_ambisonics_decoder.cpp`'s new direct-pan sharpness checks.

### Added
- **New per-object "Momentum" toggle** (`SoundObject::momentumEnabled`,
  default on) -- when off, releasing a mouse drag always leaves the
  object exactly where released, instead of throwing it into Impulse
  mode. Only gates the plain mouse-drag release; the sling gesture and
  gamepad throw buttons are unaffected. Persisted and automatable.
- **`SoundObject::Mode::Attracted` merged into `Mode::Impulse`** -- the
  two were byte-for-byte identical in `TrajectoryEngine::update()`,
  differing only in which situation auto-assigned them. Old presets
  with the `"attracted"` mode string still load correctly, mapped to
  `Impulse`.
- **Two new raw Ambisonics output options**, 4th order (25ch) and 5th
  order (36ch), for higher precision when decoding externally.
- **Real DAW-automatable parameters (AU/VST3/Standalone) -- 493
  parameters.** New `Source/AutomationParameterBridge.h/.cpp` bridges
  `ParameterRegistry`'s existing descriptors into
  `juce::AudioProcessorParameter`, grouped by object slot and category.
  `Scope::SelectedObject` descriptors aren't bridged (no stable identity
  for a host automation lane to target). Manual GUI/gamepad/MIDI/OSC
  tweaks are not themselves recorded as host automation. Verified via
  `Tools/verify_automation_parameters.cpp` and `auval`.
- **Session state save/restore actually implemented**
  (`getStateInformation`/`setStateInformation`), reusing `PresetManager`'s
  own JSON serialization -- previously empty stubs, so a host reloading
  a session restored no state at all. Routed through
  `juce::MessageManager::callSync()`, since the underlying
  `TrajectoryEngine` access is message-thread-only.
- **Object Position (X/Y/Z) is now a real, host-automatable
  parameter** -- writing it also switches the object into Manual mode,
  same as a mouse drag. +24 automation parameters (493 -> 517).
- **Mouse object-drag can now move height (Z)**, not just the ground
  plane (X/Y) -- holding **Alt** while dragging switches to a
  height-only drag mode.

### Fixed
- **VST3/Standalone output could get stuck at, or shrink back to, a
  small channel count regardless of the selected Output Format.** Root
  cause: the whole dynamic bus-negotiation design was unsound --
  selecting a smaller Output Format inside the plugin permanently
  narrowed which other formats stayed selectable afterward, and higher
  Ambisonics orders were unreachable even with enough host channels
  offered. Real multichannel Ambisonics VST3 plugins (e.g. IEM Suite)
  don't negotiate dynamically at all -- they declare one generous fixed
  bus once. Fixed the same way: VST3/Standalone now declare exactly one
  fixed 36-channel output bus, decided once at construction and never
  renegotiated; Output Format selection became a purely internal
  decode-routing choice. AU is unaffected (Logic's own per-track
  negotiation is a separate mechanism). Verified via
  `Tools/vst3_bus_probe.cpp` against the real compiled VST3 ABI.

### Added
- **AU is no longer capped at Logic's own 12-channel ceiling when
  hosted by anything other than Logic** (e.g. Reaper, which also loads
  AU) -- added runtime host detection
  (`KlangorbitProcessor::isLogicHost`), since `wrapperType` alone can't
  distinguish Logic from any other AU host. Non-Logic AU hosts now get
  the same fixed 36-channel bus VST3/Standalone use.
- **Third bundled Binaural HRTF dataset: KU100 2° Grid** (TH Köln /
  Bernschütz, CC BY 3.0) -- the densest of the three bundled datasets.

### Fixed
- **Grain Doppler produced audible clicks.** Root cause: the ring-buffer
  read position was recomputed from scratch each block assuming a
  constant playback rate, which broke once per-grain Doppler's own ratio
  started changing block to block. Fixed by making the read position a
  persistent, continuously-advanced accumulator instead.
- **Binaural (HRTF) output came out substantially louder than every
  other Output Format**, and inconsistently so between datasets. Root
  cause: the decode summed 50 deliberately un-normalized HRIR
  convolutions with no calibration on the overall level. Fixed with the
  same loudness-calibration recipe the other decode modes already use.
- **Output/Mappings window sometimes didn't come back after closing
  it** -- hiding an always-on-top window doesn't itself hand focus back
  to whatever was behind it in every host. Fixed with an `onClosed`
  callback that re-fronts the main editor window.

### Changed
- **Gamepad default control scheme: D-pad Left/Right reassigned to
  object cycling, and the Learn-mode paging modifier moved off the
  shoulder buttons** (which were double-booked with the built-in
  Slingshot throw binding) **onto the left trigger**.

### Added
- **Pitch Jitter Mode**: quantize per-grain pitch jitter to a musical
  scale instead of continuous random deviation -- 13 standard interval
  sets, including an "Acoustic"/overtone scale.

### Fixed
- **AU never appeared as insertable on any track in Logic Pro**, despite
  `auval` passing and Logic's own Plugin Manager showing it as
  installed. Root cause: the AU category (`AU_MAIN_TYPE`) was Music
  Effect (`aumf`); switching to plain Effect (`aufx`) was the fix.
  Tradeoff: `auval` now flags a MIDI-handling type mismatch as a warning
  (not a failure); whether Logic still routes MIDI to an `aufx`-typed AU
  isn't confirmed by an in-Logic test yet.

### Added
- **Output Format reordered/renamed, conditional visibility for
  format-specific controls, a unified window-focus fix, and full
  channel-layout documentation for every format.**
  - Reordered/renamed the Output Format list; Octophonic/Circular Array
    moved next to Quad.
  - Format-specific controls (Circular Array speaker count, the
    horizontal-only hint, Bass Management) are now only shown for
    formats they actually apply to.
  - New `WindowUtils::forceToFront()`, applied to all three toolbar
    windows -- toggling `setAlwaysOnTop()` off then back on before
    `toFront()` reliably wins the OS-level window-ordering race a
    same-level `toFront()` alone sometimes loses.
  - Full channel-layout/standards documentation added to the in-app
    Help, README, and UserGuide.
- **Audio Unit (AU v2) plugin format**, for Logic Pro/GarageBand/other
  AU hosts -- classic AU v2 (Component Manager), not AUv3. Verified
  with `auval`.
- **AU: the "Live Inputs" bus now accepts Mono/Stereo/Quad/7.1**, not
  just the fixed 8-channel layout VST3/Standalone still require -- the
  original 8-channel-only input bus matched none of Logic's standard
  track formats, so Logic's own per-track filtering excluded the plugin
  everywhere. Required a runtime (not compile-time) format check, since
  `PluginProcessor.cpp` is compiled once and shared across all three
  format targets.
- **AU: the output side is now also flexible** (9 of the 14 Output
  Formats are usable in Logic: named layouts within its 12-channel
  ceiling), **and every format now starts up in Stereo instead of raw
  Ambisonics** -- a real behavior change for existing Reaper/Standalone
  sessions that may have relied on the old default.
- **Binaural (HRTF-based) headphone output**, a new selectable Output
  Format -- decodes via the same virtual-speaker-array technique the
  other irregular layouts use, convolving each virtual speaker through
  its own measured HRIR instead of VBAP-remapping to real speakers. Two
  bundled datasets (KEMAR, SADIE II D1) plus custom SOFA file import.
  Verified via `Tools/verify_binaural_decoder.cpp`.
- **Camera orientation gizmo**, bottom-left of the 3D viewport -- a
  small red/green/blue arm indicator showing the world axes as the
  camera currently sees them.
- **Double-click any parameter slider to reset it to its default
  value.**
- **Grain "Movement Mode" now only shows the parameters that apply to
  the currently selected mode.**
- **Doppler Enabled toggle**, top of the (per-object) Doppler category
  -- a quick on/off switch that doesn't touch the `dopplerFactor` dial
  underneath it.
- **Output Format/Bass Management/Circular Array speaker count moved to
  their own "Output..." toolbar window.**
- **Fixed default gamepad control scheme** -- a full set of sensible
  out-of-the-box bindings (object cycle/add/remove, gamepad-driven Free
  Throw/Orbit Shot/Slingshot, camera look/zoom), all still overridable
  via Learn mode.
- **MIDI and OSC drivers** -- two more input sources on the canonical
  input layer, bindable via the same Learn-mode UI as gamepad controls,
  no separate MIDI-Learn/OSC-Learn workflow.
- **Controller mapping -- Learn mode, paging/banking, and saveable
  mapping profiles** (`Source/MappingEngine.h/.cpp`). A new
  "Mappings..." toolbar window: pick a target parameter, press Learn,
  move a control. A held modifier source unlocks a second mapping layer
  (bank). Mapping profiles are their own file format/schema, separate
  from scene presets.
- **Gamepad control** -- the selected object moves with a connected
  controller's left stick, rate-control by default (stick deflection
  sets current velocity continuously; centering it stops the object
  immediately with no drift). A separate, off-by-default inertia mode
  nudges real Impulse-mode velocity instead. Deadzone + exponential
  response curve, extracted into a reusable, protocol-neutral helper
  (`Source/AxisShaping.h`).
- **Canonical controller-input layer** -- a protocol-neutral abstraction
  (`Source/CanonicalInput.h/.cpp`) so gamepad/MIDI/OSC drivers can all
  post into one shared hub without the mapping layer knowing which
  protocol produced an event.
- **Parameter registry** -- a central register of every controllable
  parameter (`Source/ParameterRegistry.h/.cpp`), built once at
  construction, foundation for controller mapping. Deliberately not
  built on `juce::AudioProcessorParameter` (a poor fit for
  `Scope::SelectedObject`, which retargets which field it resolves to
  from one call to the next).
- **Octophonic + Circular Array output formats** -- a fixed 8-speaker
  circular array, and a generic adjustable ring (4-24 speakers), both
  decoded via plain SH-sampling (a regular array has no irregularity
  for AllRAD's remap machinery to correct for).
- **Internal Ambisonics decoder: Stereo/Quad/5.1/7.1/Atmos-bed output
  formats** -- previously the plugin only ever output raw Ambisonics
  B-format. New `AmbisonicsDecoder` module decodes via AllRAD (Zotter &
  Frank 2012) to these real-speaker layouts; Binaural/HRTF deferred to
  a later pass.
- **Orbit-mode usage hint** -- the Orbit category now shows an inline
  reminder when the edited object's Mode isn't actually Orbit.
- **Orbit Radius Noise Smoothing** -- low-pass-filters the raw per-tick
  noise sample before it's folded into the mean-reverting orbit radius,
  for a smoother "breathing" motion.
- **Grains: Orbit Around Parent, 2D -> 3D sphere spread**
  (`orbitSphereSpread`) -- blends each grain's own orbit-plane normal
  toward a random direction, so the swept shape grows from a flat disc
  toward a sphere over many grains/rotations.
- **Voice stealing for grain spawning, plus per-parameter grain
  jitter.** Once `maxConcurrentGrains` is hit, the oldest active grain
  gets a short forced fade-out instead of stalling new spawns. Five new
  dedicated `*Jitter` fields replace the old single, mutually-exclusive
  `jitterTarget`/`jitterRange` pair.
- **"Mute Original Audio" toggle** in the Grains category -- silences
  just an object's own dry signal while leaving its grains untouched.
- **English user documentation + an in-app Help window**
  (`Docs/UserGuide.md`, `Source/HelpContent.h`, a "?" toolbar button).
- **`PresetManager`**: load/save scenes in the preset JSON format, two
  toolbar buttons.
- **`Tools/validate_presets`** -- CLI tool, checks all presets in a
  folder via the same code path as the GUI.
- **Dynamic object count** -- starts with 1 active object instead of
  all 8.
- **Extended motion physics** -- `maxVelocity`/`dragCoefficient`/
  `velocitySnapThreshold`/`restitution` per object; spherical room
  boundary with reflect/wrap/absorb.
- **n-body refinement** -- `forceExponent`/`minDistance`/`maxRange` now
  per object; periodic attraction modulation.
- **Orbit extensions** -- tilted orbit plane, simplified elliptical
  orbits, orbit decay, orbiting around another object.
- **Scene-wide parameters** -- `globalField` (constant force),
  `timeScale` (fast-forward/slow-motion).
- **`ParameterPanel`** -- side panel, shows/edits all parameters of the
  selected object and the scene directly on the engine.
- **`PropagationProcessor`** -- per-object acoustic propagation (delay +
  Doppler as one unified variable delay line, simplified air
  absorption, wind, directivity).
- **`Tools/verify_propagation`** -- CLI tool, runs `PropagationProcessor`
  against synthetic signals.
- **`GrainCloud`** -- granular synthesis per `SoundObject`, multiplying
  a source into many independently-moving grains with physics-based
  movement; five movement modes; skips `PropagationProcessor` for
  performance. Preset schema bumped to **schemaVersion 2**.
- **Sling launch gesture** -- Shift+drag+release to launch an object
  like a catapult, with two launch modes (free throw, orbit shot)
  switchable mid-gesture via Ctrl.
- **`Tools/verify_orbit`** -- CLI tool for the orbit/sling math.
- **3D orbit-camera view**, replacing the previous fixed top-down 2D
  rendering -- one projection, no 2D/3D mode switch.
- **`Tools/verify_camera`** -- CLI tool for the camera projection/
  rotation/zoom math.
- **Live movement preview for the sling gesture** -- a dashed element
  previewing the actual result live while aiming.
- **Mean-reverting orbit radius** (Ornstein-Uhlenbeck) -- an alternative
  to `orbitDecay` for a "living, breathing" orbit that wanders around a
  baseline instead of drifting away.
- **Grain density and duration raised**, plus a CPU-load display and a
  ring-buffer fix needed to do so safely -- `maxConcurrentGrainsGlobal`
  32 -> 128, `grainDuration`/`grainRate` ranges widened, the ring
  buffer resized to cover the new worst case.
- **Object list sidebar** -- lists every active `SoundObject`, click a
  row to select it.
- **Optional per-grain Doppler pitch shift**, off by default -- a much
  cheaper per-grain approximation than the main object's own Doppler.
- **Solo/Mute per object** -- own `muted` always wins over `soloed`;
  soloing is non-exclusive. Applies to an object's `GrainCloud` too.
- **Grain read-depth range** -- an independent parameter set
  controlling how far into the ring buffer's past a grain's start point
  may be drawn from.
- **Room boundary visibility toggle** -- purely cosmetic, the boundary
  still applies physically while hidden.
- **On-screen reminder of the Orbit and Sling-launch gestures.**
- **Sling gesture: "slingshot" targeting for the orbit shot** -- tap
  **Tab** while pulling to cycle the orbit shot's center through every
  other active object.
- **App/plugin icon and vendor name** -- `Assets/AppIcon` baked into
  the Standalone/VST3 bundles; `COMPANY_NAME` set to "Jan Glueck".
- **Sling gesture: Slingshot mode** -- a real, physics-based gravity
  assist (not a scripted path): an object thrown near another gets
  deflected by that object's real gravity, continuing on a new course
  or captured into an orbit.

### Changed
- **Object list's add/remove buttons unified to bare "+"/"-"**, side by
  side.
- **Toolbar simplified to a single row**; "+ Object"/"- Remove Object"
  and the object count moved to the object list sidebar.
- **Acoustics split back out into its own category**; Propagation Wind
  moved there from Scene.
- **Parameter panel polish** -- various label/layout tweaks (Boundary
  Size, Force Field, Propagation Wind renames; window-button grouping).
- **Parameter panel reorganized** -- Scene settings visually separated
  from Object settings; Acoustics folded into Scene; Output moved out
  into its own window.
- **Objects are now numbered from 1 instead of 0** everywhere they're
  displayed (purely a display change, internal ids stay 0-based).
- Label simplifications in the Grains category ("Doppler", "Isolate
  Grains").
- **Max Concurrent Grains's default and range raised again, 32 -> 256.**
- "Grain Rate" renamed to "Grain Rate (Spawn Rate)".
- "Mute Original Audio" renamed to "Grains Only (Mute Original Audio)".
- **"Grain Cloud" renamed to "Grains" everywhere it's shown to the
  user** (internal identifiers unchanged).
- **Renamed the product from "Spatial Audio POC" to "Klangorbit"** --
  applied to the plugin name, class names, bundle identifier, and
  plugin code everywhere.
- **Solo/Mute controls moved to the object-list sidebar**, removed from
  the parameter panel.
- **Room boundary now renders as a shaded, translucent sphere** instead
  of a flat wireframe outline.
- `Camera3D::maxDistance` raised from 30 to 150 meters.
- Clicking an object (without dragging) selects it without affecting
  its motion mode anymore.
- **Full GUI visual redesign** -- dark, cyan-accented "sci-fi HUD"
  theme, applied consistently across the whole editor
  (`Source/UiTheme.h`, `Source/SciFiLookAndFeel.h/.cpp`).

### Fixed
- **Reaper (and potentially other hosts) could permanently negotiate
  the plugin's output down to plain Stereo**, regardless of the
  selected Output Format. Root cause: `isBusesLayoutSupported()`
  accepted every mode's layout as valid, so a host probing several
  candidates could settle on whichever got accepted first. Fixed by
  restricting it to only the currently active mode's own layout.
- **The Mappings window (and, latently, Help) could open behind the
  host's own window when hosted as a VST3.** Fixed with
  `setAlwaysOnTop(true)` plus a deferred re-front.
- **The whole simulation froze when the editor window closed** --
  physics, panning, and grain spawning all ran only from the editor's
  own timer. Fixed by moving timer ownership to the processor itself,
  so it keeps running regardless of whether an editor exists.
- Investigated a report that "grain duration also affects spawn rate"
  -- traced and confirmed the scheduling itself was already correct
  (`grainRate` and `grainDuration` are independent at that level); the
  real interaction is one step removed, through `maxConcurrentGrains`.
  Documented more clearly rather than changed.
- **Slingshot mode: thrown objects fell almost straight into the
  target and jittered there** instead of swinging past or settling
  into an orbit. Root cause: the object's own damping/drag were still
  being applied during a slingshot pull, which should be (nearly)
  frictionless. Fixed by skipping damping/drag while a slingshot pull
  is active, and retuning the pull's softening floor and strength.
- **Grain click/discontinuity bug** -- the Hann envelope's last
  rendered sample fell just short of a true zero-crossing, producing an
  audible jump. Fixed by using `grainLengthSamples - 1` as the envelope
  denominator.
- A leftover orbit reference id could silently override the center a
  fresh orbit/sling shot was just told to use. Fixed by resetting it
  inside `startOrbit()` itself.
- First working build fixes (a custom `Vec3` type instead of
  `juce::Vector3D`, a `BusesProperties` construction fix, a
  shadow-field warning).
- `PropagationProcessor`: at `dopplerFactor=0`, the delay line's own
  drift-correction term reintroduced a small pitch shift during
  continuous fast movement. Fixed by rate-limiting the correction.
- `GrainCloud`: `AttractRepelSiblings` grains all spawned exactly
  coincident at the parent position, causing the softened inverse-square
  force to spike to a near-infinite value on the first tick. Fixed with
  a small random spawn offset and a larger softening distance.

## [0.1.0] - POC skeleton
### Added
- TrajectoryEngine: Static/Manual/Orbit/Impulse/Attracted modes
- AmbisonicsEncoder: generic SH computation, order 0-7, SN3D/ACN
- PluginProcessor: 8 mono inputs -> Ambisonics bus (order 3 = 16 ch.)
- 2D editor: mouse drag, throw gesture, orbit via double-click
