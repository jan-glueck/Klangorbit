# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
versioning follows [SemVer](https://semver.org/lang/en/) -- while the major
version is 0, the rule is: every minor version (0.X.0) may break presets
(see Presets/schema/), patch versions (0.X.Y) may not.

## [Unreleased]
### Changed
- **Quad/Octophonic/CircularArray/5.1/7.1/all four Atmos-bed formats now
  pan each object DIRECTLY to the real speakers (VBAP), instead of
  decoding a shared, fixed-order-3 Ambisonics bus.** Reported and
  measured by the user: panning a source exactly onto one speaker still
  left the OPPOSITE speaker only ~18dB down in Quad and 7.1 -- much less
  suppression than a sharp pan should give. Root cause, confirmed by
  reading the actual decode math (the max-rE weighting and the AllRAD/
  VBAP remap stage were both individually verified correct against their
  own cited references -- this was not a bug in either): every real-
  speaker format worked by encoding every object into a SHARED Ambisonics
  bus fixed at order 3 (16ch) first, then decoding that ONE summed bus to
  the real speakers once per block (AllRAD -- Zotter & Frank 2012 -- for
  the irregular layouts; a plain SH-sampling decode for the regular
  rings). Order-3 Ambisonics has a real, inherent beamwidth (~35-40deg)
  -- even a mathematically correct decode of it spreads meaningful energy
  across every real speaker once there are only a handful of them. This
  is the fundamental resolution ceiling of a low, FIXED Ambisonics order
  squeezed through few loudspeakers, not something a better remap
  algorithm can fix.
  - **Fix**: every one of these 9 formats now panning each object/grain
    DIRECTLY to its own real speakers via VBAP (Pulkki 1997, `Source/
    VBAP.h`, already used -- just not this way -- for AllRAD's own
    virtual-array remap stage before), bypassing the shared Ambisonics
    bus entirely. New `AmbisonicsDecoder::usesDirectPan(Mode)`,
    `computeDirectPanGains(Vec3 direction)` (caches a VBAP triangulation
    per mode/circular-speaker-count change, mirrors the old
    `buildAllRadMatrix()`'s own `realDirs`/`realDirToFullIndex` pattern),
    and `applyLfeFilterDirect()` (the direct-pan equivalent of `decode()`'s
    own W-channel-derived LFE, sharing the same one-pole ~120Hz filter/
    state -- these modes have no shared bus W channel to derive LFE from
    anymore, so `KlangorbitProcessor::processBlock()` now sums every
    active object/grain's own dry signal into a small mono scratch buffer
    instead, same gain weighting as what feeds the pan). New
    `AmbisonicsEncoder::panDirectBlock()` (a sibling to the existing
    `encodeBlock()`, same ramped/additive-mix mechanics and
    `distanceGain()` reuse, just fed caller-supplied VBAP gains instead of
    this class's own SH coefficients) -- `AmbisonicsEncoder` needs no new
    knowledge of `AmbisonicsDecoder`/`Mode`/VBAP to do this.
  - **Measured effect**: for every migrated format, panning exactly onto
    one speaker's own direction now gives that speaker ~1.0 gain and
    EVERY other speaker ~0.0 (see `Tools/verify_ambisonics_decoder.cpp`'s
    new direct-pan sharpness section) -- vs. the reported ~18dB
    (~0.126 linear) worst-case leakage before.
  - **Octophonic/CircularArray migrated too**, on the reporting user's own
    request for an assessment: their old `buildCircularMatrix()` decode
    didn't have AllRAD's specific "irregular VBAP coverage" diffuseness
    mechanism (a regular ring has no coverage imbalance to correct for),
    but was still a fixed order-3 SH-sampling decode with its own inherent
    blur (worse for a small ring) -- direct VBAP is sharper regardless of
    whether the array is regular or irregular.
  - **`buildAllRadMatrix()`/`buildCircularMatrix()` removed entirely**
    (100% dead once every mode that called them migrated) -- matches this
    project's own established discipline against leaving unreachable code
    whose documentation would now be actively wrong (the `rawModeForOrder()`
    precedent, earlier this file). `AmbisonicsDecoder`'s class comment
    rewritten to describe the new three-strategy split (raw passthrough /
    Stereo's own simple decode, unchanged / direct VBAP pan for everything
    else). Stereo, Binaural, and the raw Ambisonics passthrough outputs
    are completely unaffected -- unchanged behavior.
  - **Tradeoff, disclosed, not hidden**: these 9 formats are no longer
    "true" diffuse-field Ambisonics decodes the way AllRAD was -- they're
    now genuinely sharp, point-source-style panning instead, which is
    what was actually wanted here, but is a real, intentional change in
    character, not a side effect to double-check for.
  - **Verification**: `Tools/verify_ambisonics_decoder.cpp` substantially
    rewritten -- the direct-pan sharpness regression check described
    above (this is the test that would have caught the reported
    diffuseness had it existed before), LFE always gets exactly 0
    direction-gain from `computeDirectPanGains()`, `applyLfeFilterDirect()`
    silent/filtered correctly, `CircularArray`'s direct-pan setup tracks
    `setCircularArraySpeakerCount()` across its full range. Full rebuild,
    complete `verify_*`/`validate_presets` suite green, `auval -v aufx
    Klor Jgck` re-run (confirms the AU-side bus/channel-count behavior for
    every migrated mode is unaffected by the internal decode-strategy
    swap -- Logic's own bus negotiation logic doesn't know or care how the
    decode itself is computed). This is a real, audible DSP character
    change the user's own ears still need to confirm once built --
    sharper localization is the direct, intended goal here, not
    incidental.
### Added
- **Two new raw Ambisonics output options: 4th order (25ch) and 5th
  order (36ch)**, alongside the existing FOA/SOA/TOA (1st/2nd/3rd order)
  -- for higher precision when decoding externally via a third-party tool
  (IEM Plugin Suite, SPARTA). Purely additive and low-risk: the existing
  raw-passthrough architecture already generalized to any order with zero
  pipeline changes needed (`AmbisonicsEncoder::setOrder()` was already
  order-agnostic up to 7; the per-object gain-ramp state was already
  sized generically off `encoder.getNumChannels()`; raw passthrough
  already bypasses the shared Ambisonics bus entirely, encoding straight
  into the output buffer). `AmbisonicsDecoder::Mode` gained
  `AmbisonicsRawOrder4`/`AmbisonicsRawOrder5`, appended after
  `AmbisonicsRawOrder3` (preserves every existing mode's own ordinal
  value); `numModes` 14 -> 16. Two new "Output Format" dropdown entries.
  Excluded from AU automatically (same as orders 1-3 -- `discreteChannels(N)`
  has no Logic-recognizable layout tag, and 25/36ch is far beyond Logic's
  12ch ceiling regardless); available for VST3/Standalone automatically
  (the existing generic bus-negotiation loop needed no changes, just the
  `numModes` bump).
- **Real DAW-automatable parameters (AU/VST3/Standalone alike) -- 493 of
  them.** Reported: "no parameter automation can be written" in Logic
  (AU). Root cause: not a bug, a genuine, disclosed architectural gap --
  ZERO parameters were ever registered via `juce::AudioProcessorParameter`/
  `addParameter()` anywhere in this codebase (confirmed by grep: no
  `AudioProcessorValueTreeState`/`AudioParameterFloat`/etc. exist in
  `Source/`). The existing `ParameterRegistry` is a completely separate
  system built only for gamepad/MIDI/OSC "Learn mode" controller mapping --
  its own class comment already says outright: "This registry deliberately
  has no host-automation ambition." DAW automation lanes need real
  `AudioProcessorParameter` objects, which simply didn't exist, in any
  format (this isn't AU-specific, the reporting user just found it while
  testing AU first).
  - **Design: bridge `ParameterRegistry`, don't duplicate it.**
    `buildParameterRegistry()` already builds a complete, correctly-ranged/
    named `Descriptor` for every automatable field, across all 8 object
    slots (`SAPOC_MAX_LIVE_INPUTS`) and their grain clouds, plus every
    `SceneSettings` field. New `Source/AutomationParameterBridge.h/.cpp`:
    `RegistryAutomationParameter` (a `juce::RangedAudioParameter`
    subclass) wraps one `Descriptor` and delegates `getValue()`/
    `setValue()` straight to that descriptor's own `getValue`/`setValue`
    closures -- no separate storage, so host automation writes into the
    EXACT same `SoundObject`/`GrainCloudSettings`/`SceneSettings` field
    the GUI/mapping system already touch. `buildAutomationParameterGroups()`
    builds the actual `juce::AudioProcessorParameterGroup` hierarchy: one
    top-level group per object slot ("Object 1".."Object 8", 1-based,
    matching `ParameterPanel`'s own display convention), each subgrouped
    by `Descriptor::category` (already exactly the right taxonomy --
    "Object Physics"/"Attraction"/"Orbit"/"Doppler"/"Grain Cloud" -- reused
    as-is), plus one "Global" top-level group for `SceneSettings`. AU can't
    nest subgroups (JUCE's own doc comment) -- flattened automatically via
    each group's separator string (`" | "`), no special-casing needed.
    `KlangorbitProcessor::buildAutomationParameters()` (new, called once
    from the constructor immediately after `buildParameterRegistry()`) is
    just the `addParameterGroup()` glue.
  - **`Scope::SelectedObject` descriptors (e.g. `"selectedObject.mass"`)
    are deliberately never bridged** -- same reasoning `ParameterRegistry`'s
    own class comment already gives for why that scope is a poor fit for
    `juce::AudioProcessorParameter`: it retargets which object it resolves
    to from one call to the next, incompatible with a host automation
    lane's own identity-stable-parameter model. Only `Scope::Global`/
    `Scope::SpecificObject` descriptors are bridged.
  - **Count**: every currently-registered field, for every one of the 8
    object slots (active or not, same "always exists" pool `TrajectoryEngine`
    already uses) plus 13 `SceneSettings` fields = **493 parameters**,
    confirmed directly by `auval`'s own "PUBLISHED PARAMETER INFO: # # #
    493 Global Scope Parameters" output, matching the hand-computed total
    exactly. Large, but not unusual for plugins with many voices/objects,
    and requires no subjective "which fields matter enough" judgment call --
    everything the mapping system already exposes becomes automatable too,
    symmetric and simple to explain.
  - **Threading**: a host may call `setValue()` from the audio thread
    during automation playback -- the first time these fields would be
    written from that specific thread, though they're already written
    unsynchronized from the message thread (GUI) and the gamepad/MIDI/OSC
    control paths today, per `ParameterRegistry`'s own "no abstraction
    layer at all" design (see its class comment). A torn read on a
    multi-field `Vec3` (three separate parameters for x/y/z) is a benign,
    momentary glitch, not a crash -- the same concurrency model this
    codebase already accepts everywhere else for this class of data, not a
    new category of risk. No new synchronization was added.
  - **Not in scope, disclosed**: the plugin's OWN GUI (`ParameterPanel`
    sliders, gamepad, MIDI/OSC mapping) does not call
    `setValueNotifyingHost()` when the user manually changes something --
    so a host's automation lane won't show a written breakpoint from a
    manual mouse-drag/gamepad/MIDI-mapped tweak, only from playing back
    automation the host already has or from the host's own explicit
    "write" mode. Wiring every existing control path to also notify the
    host would be a substantially larger, separate follow-up. Also: the
    editor's own `resyncFromBackgroundObjectChanges()` (polled at ~90Hz)
    already re-syncs the object list/selection when a background change
    (gamepad, and now also host automation/state restore) alters either,
    but does NOT live-refresh `ParameterPanel`'s displayed slider VALUES
    for whichever object is currently shown -- a pre-existing gap (already
    true for gamepad/MIDI/OSC-driven background changes before this),
    not something newly introduced here.
  - **Verification**: new `Tools/verify_automation_parameters.cpp`
    (own `verify_automation_parameters` CMake target, linking only
    `juce_audio_processors_headless` -- not the full `juce_audio_processors`,
    and not a full `KlangorbitProcessor`, same "no full juce::AudioProcessor
    outside a host context" precedent `verify_parameter_registry`'s own
    comment already establishes) exercises `buildAutomationParameterGroups()`/
    `RegistryAutomationParameter` against synthetic descriptors: correct
    top-level/category grouping, `Scope::SelectedObject` exclusion, value/
    range/default bridging, and independent per-object storage. `auval -v
    aufx Klor Jgck` re-run and passes, including its own "Checking
    parameter setting"/"Checking ramped parameter scheduling" checks
    (previously trivially passing on zero parameters, now actually
    exercising all 493).
- **Session state save/restore (`getStateInformation`/`setStateInformation`)
  actually implemented** -- previously empty `TODO` stubs, meaning a host
  reloading a saved session restored NO object/scene/grain-cloud state at
  all. A second, closely related gap to the automation one above: even
  with real automation parameters, a host session reload would have
  silently reset the whole scene, inconsistent with whatever the
  automation lanes assumed. Fixed by reusing `PresetManager`'s existing
  JSON serialization directly (no new format): `getStateInformation()`
  calls the same `PresetManager::sceneToVar()` "Save Preset..." already
  uses, stringifies via `juce::JSON::toString()`, and writes the UTF-8
  bytes; `setStateInformation()` reverses it (`juce::JSON::parse()` ->
  `PresetManager::loadFromVar()`, which already handles schema validation/
  migration -- a malformed/empty block just leaves the scene at its
  current defaults rather than crashing).
  - **Why these were left as stubs before, and how that's actually
    addressed now** (not just overridden): `TrajectoryEngine::getObject()`
    is documented message-thread-only (its own comment) -- the ~90Hz
    physics tick (`TrajectoryEngine::update()`) also runs there, so a host
    calling `getStateInformation()`/`setStateInformation()` from some OTHER
    thread (the JUCE contract allows this; not guaranteed to be the
    message thread, even though most real hosts' own "save/load project"
    actions do call it there) would otherwise race with it. Rather than
    calling `sceneToVar()`/`loadFromVar()` directly, both now go through
    `juce::MessageManager::callSync()` -- runs the lambda inline if
    already on the message thread (the common case, no overhead), or
    safely posts it and blocks until the message thread has run it
    otherwise. `setStateInformation()` additionally parses the incoming
    JSON text OUTSIDE that call (pure text parsing, no `TrajectoryEngine`
    touch, safe on any thread) so only the actual `loadFromVar()` apply
    goes through the message-thread hop.
  - **Verification**: the JSON-text (de)serialization layer itself
    (`Tools/verify_automation_parameters.cpp`'s own round-trip check, the
    one thing this adds beyond `PresetManager`'s own, separately-tested
    round-trip correctness) is covered headlessly; the `callSync()`
    message-thread dispatch is NOT independently unit-tested (host
    cross-thread call timing isn't something a headless test can
    exercise) -- verified by full rebuild, the complete `verify_*`/
    `validate_presets` suite, and `auval` (which calls both from its own
    thread context without failure).
- **Object Position (X/Y/Z) is now a real parameter: shown/settable in
  the parameter panel's Object category and, unlike every other runtime
  physics field, host-automatable.** Requested alongside the mouse
  height-drag below, since Position is what that new drag axis (and
  automation) both need to actually control. `registerObjectPositionParam()`
  (`Source/PluginProcessor.cpp`) registers it through the same
  `ParameterRegistry` path as every other automatable field, but with a
  write side effect the generic `registerObjectVec3Param()` can't
  express: writing any axis also forces the object into `Mode::Manual`
  (and clears `manualVelocityActive`), exactly what
  `TrajectoryEngine::beginDrag()`/`dragTo()` already do for a mouse drag
  -- otherwise the very next physics tick would silently overwrite the
  automated value if the object was in Orbit/Impulse/Attracted at the
  time. Adds 24 new automation parameters (3 axes x 8 object slots),
  bringing the DAW automation total from 493 to **517** (`auval`'s own
  "# Global Scope Parameters" count, re-verified). `ParameterPanel`'s new
  `positionRow` (top of the Object category, above Mass) is likewise a
  custom-setter `Vec3RowComponent`, not the generic `addObjectVec3Row()`
  helper, for the same reason -- and like every other row in that panel,
  it is only refreshed on selection change/preset load (not on a timer,
  by the panel's own long-standing design -- see its class comment), so
  it goes visibly stale while the object moves under Orbit/Impulse/
  Attracted; a disclosed limitation, not a bug.
- **Mouse object-drag can now move height (Z), not just the ground plane
  (X/Y).** Reported: dragging with the mouse was 2D-only ("Panning mit
  Maus ist nur in einer 2D Ebene"), height only ever changed through
  physics (orbit planes, global field, n-body forces). Root cause:
  `PluginEditor::screenToGroundWorld()` always raycasts onto the world's
  z=0 plane by construction -- there was no path to any other z at all.
  **Fix**: holding **Alt** while dragging an object now switches
  `mouseDrag()` to a height-only mode instead of the ground-plane
  raycast: X/Y stay exactly where they are, and vertical mouse movement
  alone moves Z, scaled to world meters via the object's own on-screen
  size at its current camera depth (`Camera3D::worldSizeToScreenSize()`)
  so it feels proportional at any zoom level, same as the existing
  ground-plane drag already does. Ctrl/Alt/Shift/Tab were confirmed free
  during a plain (non-sling) drag before picking Alt -- all four are
  otherwise scoped exclusively to the Shift-triggered sling gesture
  (`updateSlingModifiers()`, gated by `slingActive`), so this has zero
  interaction with that gesture.
### Fixed
- **VST3/Standalone stuck at Stereo (2ch) regardless of which Output
  Format is selected, reported across multiple DAWs.** Root cause: two
  individually-reasonable earlier changes combined to fully break
  multi-channel output. (1) A much earlier fix (see this file's own
  "Reaper (and potentially other hosts) could permanently negotiate the
  plugin's output down to plain Stereo..." entry) restricted
  `isBusesLayoutSupported()` to accept only `decoder.getMode()`'s own
  CURRENT layout, to stop hosts probing multiple candidates from settling
  on the wrong one -- this worked at the time because a fresh instance's
  default mode was raw Ambisonics (16ch), so there was exactly one
  candidate to negotiate to either way. (2) A later change (elsewhere in
  this file) switched the construction-time default mode to Stereo for
  every format. Combined: `isBusesLayoutSupported()`'s "only the current
  mode" now means "only Stereo" for a fresh instance, and switching modes
  afterward via the "Output..." window can never inform a VST3 host live
  -- confirmed directly in JUCE's own VST3 client wrapper source
  (`juce_audio_plugin_client_VST3.cpp`): there is no code path anywhere in
  it that ever sends the VST3 SDK's `Vst::kIoChanged` restart flag: a
  plugin genuinely cannot ask a VST3 host to live-rescan its bus layout in
  this JUCE version, full stop. The result: every fresh VST3/Standalone
  instance negotiates Stereo and stays there no matter what's picked in
  the Output Format dropdown, in every host (not host-specific the way the
  original Reaper report was).
  - **Fix**: `isBusesLayoutSupported()` now accepts ALL 13 of
    `AmbisonicsDecoder::Mode`'s output layouts at once again (restoring
    the pre-regression-fix behavior), but this time WITHOUT reintroducing
    the original symptom: `setDecoderMode()`/`setCircularArraySpeakerCount()`
    never request a new layout from a live host after insertion (the same
    "many layouts available at once, decode uses fewer channels than
    negotiated, no live renegotiation attempted" model already proven for
    AU/Logic, see the "AU never appeared as insertable..." entries below),
    and `isOutputModeAvailable()` (previously an AU-only gate, now applied
    to VST3/Standalone too, minus AU's named-layout-only restriction) greys
    out any Output Format that doesn't fit within whatever channel count
    the host/track actually negotiated at insertion. `setCircularArraySpeakerCount()`'s
    own upper clamp now additionally respects `getTotalNumOutputChannels()`,
    not just `[minCircularSpeakers, maxCircularSpeakers]`.
  - **Practical effect, disclosed, not hidden**: getting more than Stereo
    out of VST3/Standalone now requires setting the host's own track/bus
    channel count first (or, for Standalone, having an audio device with
    enough output channels) -- the plugin can advertise every format but
    can never force a host to grant more channels live. This matches how
    other multichannel Ambisonics/spatial-audio plugins already work in
    practice (e.g. the SPARTA/IEM workflow this project's own README
    already documents), and is now the same model AU/Logic already uses.
    Not independently unit-tested (host bus-negotiation behavior isn't
    something a headless test can exercise) -- verified via full rebuild,
    the complete `Tools/verify_*`/`validate_presets` suite, and `auval`
    (AU unaffected by this change beyond shared code); real VST3-host
    behavior needs the reporting user's own re-test.
### Added
- **Third bundled Binaural HRTF dataset: "KU100 -- 2deg Grid (TH Koeln /
  Bernschuetz)".** `HRIR_FULL2DEG.sofa` from Benjamin Bernschütz's
  "Spherical Far Field HRIR Compilation of the Neumann KU 100" (Zenodo,
  DOI 10.5281/zenodo.3928297, CC BY 3.0) -- downloaded directly, MD5
  verified against the record's own published checksum
  (`aa48acb20c1fb8ff3d8de116107b73c2`), not modified. The densest of the
  three bundled datasets: a full-sphere 2-degree Gauss-Legendre grid
  (16020 measurement points) vs. KEMAR's 710 and SADIE II D1's own grid;
  natively 48kHz (the other two bundled files are 44.1kHz variants) --
  doesn't matter functionally, since `HrtfDataset::load()` resamples via
  libmysofa to whatever rate is requested regardless of a file's own
  native rate. Bundled as `Assets/HRTF/ku100_48000.sofa` (~19MB), same
  `juce_add_binary_data`-embedded-then-written-to-a-cached-temp-file
  mechanism as the existing two datasets (`KlangorbitProcessor::
  BinauralDatasetSource::Ku100`, inserted before `CustomFile`). Full
  attribution (author, license, recommended DAGA-2013 paper citation) in
  `THIRD_PARTY_LICENSES.md`; new "HRTF Dataset" picker entry, updated
  hint text (`OutputPanel`), Help (`?`) content, README, and UserGuide
  entries. `Tools/verify_binaural_decoder.cpp` extended to load/exercise
  this dataset alongside the existing two, including in the loudness-
  calibration cross-dataset check (now three-way, still within 2x).
### Fixed
- **Grain Doppler produced audible clicks.** Root cause:
  `GrainRenderer.h`'s `renderGrainBlock()` computed each sample's ring
  buffer read position as `bufferReadStartSample + samplesPlayed *
  playbackRate` -- correct only if `playbackRate` stays constant for a
  grain's whole life (true of `pitchJitter`, fixed once at spawn) but NOT
  of per-grain Doppler (`GrainDoppler.h`), whose ratio is recomputed
  fresh every block from the grain's live position/velocity. Whenever
  the rate changed, the old formula recomputed the ENTIRE elapsed read
  position at the new rate, jumping the read pointer discontinuously
  every block the ratio changed -- an audible click, specific to Doppler
  (not `pitchJitter` alone, which never changes rate mid-grain). Fixed by
  making the read position a persistent, caller-owned accumulator
  (`PluginProcessor::GrainAudioState::readPosition`, advanced by
  `playbackRate` per sample rather than recomputed from scratch each
  call) -- only the RATE of advance changes at a block boundary now, not
  the position itself. `renderGrainBlock()`'s signature changed
  accordingly (`double& readPosition` replaces the old `int
  bufferReadStartSample` parameter); `Tools/verify_grain_cloud.cpp`
  updated to match, plus a new mid-grain-rate-change regression check.
- **Binaural (HRTF) output came out substantially louder than every
  other Output Format**, often pushing the plugin's output above 0dB,
  and inconsistently so between the two bundled HRTF datasets (SADIE II
  louder than KEMAR). Root cause: `BinauralDecoder::decode()` sums 50
  virtual speakers' worth of deliberately un-normalized HRIR convolutions
  (`Normalise::no` is correct and unchanged -- a direction's own level,
  e.g. head-shadow attenuation, is real content, not something to flatten
  away) with no calibration on the SUM's overall level, unlike every
  other decode mode (`AmbisonicsDecoder::calibrateDecodeMatrix()` already
  calibrates AllRAD's own decode matrix the same way). Fixed with the
  same recipe: `BinauralDecoder::prepare()` now also builds, per virtual
  speaker, its own combined impulse response toward 32 test source
  directions (weighted by that direction's own SH decode gain), measures
  the resulting two-ear energy across all of them, and scales the final
  L/R sum (`BinauralDecoder::calibrateOutputGain()`, applied once per
  `decode()` call) by one constant so the average lands at roughly unit
  energy -- now compensating for a given dataset's own absolute
  measurement level along with everything else. `Tools/
  verify_binaural_decoder.cpp` gained a loudness-calibration section
  (sane absolute RMS range, and KEMAR/SADIE II land within 2x of each
  other).
- **Output/Mappings window sometimes didn't come back after closing it**
  (open the plugin editor, open Output or Mappings, close it -- the
  plugin editor's own window sometimes then failed to reappear/refront,
  only recovering once the editor itself was closed and reopened).
  `OutputWindow`/`MappingWindow`/`HelpWindow`'s `closeButtonPressed()`
  used to just `setVisible (false)` and nothing else -- hiding an
  always-on-top window doesn't itself hand focus/z-order back to
  whatever was behind it in every host, so nothing was ever asking the
  editor's own window to reactivate. Fixed with a new `onClosed`
  callback on all three windows, wired by `KlangorbitEditor` to a new
  `bringEditorToFront()` (`toFront (true)`, synchronous + a deferred
  retry via `MessageManager::callAsync`, same "first attempt sometimes
  loses the race" pattern `WindowUtils::forceToFront()` already uses for
  the opposite direction).
### Changed
- **Gamepad default control scheme: D-pad Left/Right reassigned to object
  cycling, and the Learn-mode paging modifier moved off the shoulder
  buttons.** Reported: Left/Right Shoulder held for Orbit Shot/Slingshot
  (`GamepadDriver::driveThrowGesture()`) wasn't working, and object
  cycling should move to the D-pad. (An initial pass reassigned D-pad
  Up/Down to cycling and moved zoom to Left/Right instead -- corrected
  immediately after merge per user feedback: zoom stays on Up/Down as
  before, cycling took Left/Right instead, not the other way around.)
  - **D-pad Left/Right** now cycles the selection to the next/previous
    active object (`GamepadDriver::driveObjectManagement()`), replacing
    Button X (which now has no built-in behavior, free for a Learn-mode
    binding like any other raw source) -- and is now bidirectional,
    which Button X's cycle never was.
  - **D-pad Up/Down** keeps its original camera-zoom assignment
    (`KlangorbitEditor::updateGamepadCamera()`), unchanged.
  - **`MappingEngine`'s default paging-modifier source** changed from
    `Gamepad0.RightShoulder` to `Gamepad0.LeftTrigger`. Root design flaw
    found while investigating the shoulder-button report: Right Shoulder
    was double-booked by default -- both `GamepadDriver`'s own built-in
    Slingshot trigger AND `MappingEngine`'s "hold to unlock bank 2"
    modifier, out of the box, with no user configuration involved. Left
    Trigger has no built-in behavior of its own, and
    `MappingEngine::canonicalInputReceived()` already treats any source's
    value >= 0.5 as "held" generically, so an analog trigger axis works
    exactly like the old digital shoulder button did. This is a genuine,
    disclosed fix for a real design conflict, confirmed by code
    inspection -- **not independently confirmed to be the full
    explanation for "doesn't work with the shoulder buttons" without a
    physical controller in this environment**; please re-test Orbit
    Shot/Slingshot after this change and report back if the shoulder
    buttons still don't fire.
### Added
- **Pitch Jitter Mode: quantize per-grain pitch jitter to a musical scale
  instead of continuous random deviation.** New `GrainCloudSettings::
  pitchJitterMode` (`PitchJitterMode`, `Source/Grain.h`): `Random`
  (default, unchanged original behavior) or `Scale`. In Scale mode, each
  grain's pitch snaps exactly onto a random degree of
  `GrainCloudSettings::pitchQuantizeScale` (`PitchQuantizeScale`, 13
  standard interval sets: Octaves, Fifths, Major/Minor Triad, Major/
  Dorian/Lydian/Mixolydian/Aeolian Scale, Whole-Tone, Octatonic,
  Hexatonic, Acoustic -- the "overtone scale"/Lydian Dominant, the
  closest standard 12-TET scale to the real overtone series, included
  specifically to cover "tune grain pitch jitter to the overtone series")
  within `pitchJitter`'s existing 0..1 reach, now reinterpreted for this
  mode as up to one octave of semitone reach either direction --
  `GrainCloud.cpp`'s new `scaleSemitones()`/`pickQuantizedSemitoneOffset()`.
  The grain's own natural pitch (ratio 1.0) is always the scale's root --
  no separate key/note picker, since these are intervals relative to the
  source material's own pitch, not absolute pitches. Reach capped at one
  octave in both modes so `playbackRate` never exceeds the same
  `GrainLimits::maxPitchJitterPlaybackRate` (2.0) the grain ring buffer
  is already sized against. New "Pitch Jitter Mode"/"Pitch Quantize
  Scale" rows in the parameter panel's Grains category (the latter only
  shown while Pitch Jitter Mode == Scale, mirroring the existing Binaural
  HRTF-dataset/Circular-Array-slider conditional-visibility pattern);
  both persisted in presets (`PresetManager`); both deliberately excluded
  from `ParameterRegistry`/Learn-mode mapping, same as every other
  enum-valued field in this project. `Tools/verify_grain_cloud.cpp`'s new
  `testPitchJitterScaleQuantizes` confirms every Scale-mode grain lands
  on an exact scale degree (Major Triad case), that `pitchJitter=0`
  always yields the root regardless of scale, and that a bare-interval
  scale (Octaves) only ever produces octave-multiple offsets.
### Fixed
- **AU never appeared as insertable on ANY track in Logic Pro, despite
  `auval` fully passing and Logic's own Plugin Manager showing it as
  installed/compatible.** All the input/output bus-flexibility work
  earlier in this section turned out not to be the (whole) cause. Root
  cause, confirmed empirically with a throwaway diagnostic build: the AU
  category itself. `AU_MAIN_TYPE` was `kAudioUnitType_MusicEffect`
  (`aumf`) -- semantically correct (an audio effect that also wants
  MIDI, matching `NEEDS_MIDI_INPUT TRUE`) and JUCE's own inferred
  default for these flags, but Logic's per-track AU filtering excluded
  it from every track/bus type regardless. Switching `AU_MAIN_TYPE` to
  plain `kAudioUnitType_Effect` (`aufx`), with everything else
  unchanged, was the one change that made it appear -- confirmed
  directly by the user after testing the diagnostic build.
  - **Real, currently-open tradeoff, not a free fix**: `auval` flags this
    with a warning -- `AU implements MusicDeviceMIDIEvent but is of type
    'aufx' (it should be 'aumf')`. The plugin's own MIDI-handling code
    (from `NEEDS_MIDI_INPUT TRUE`) is still built in regardless of this
    type, but declaring `aufx` tells a host this isn't a MIDI-interested
    unit -- whether Logic still routes MIDI (the Mappings/Learn feature's
    CC/Note/Pitch Bend input) to an `aufx`-typed AU has NOT been
    confirmed either way by an actual in-Logic test yet. Gamepad and OSC
    control are unaffected regardless of this choice (no dependency on
    the AU type). VST3/Standalone are completely unaffected either way
    -- `AU_MAIN_TYPE` only applies to the AU target.
  - Verified with `auval -v aufx Klor Jgck`: full PASS (the MIDI-type
    mismatch above is a warning, not a failure).
### Added
- **Output Format reordered/renamed, conditional-visibility for
  format-specific controls, a unified window-focus fix, and full
  channel-layout/standards documentation for every format.**
  - **Reorder + rename** (`AmbisonicsDecoder::Mode`'s declaration order,
    `Source/AmbisonicsDecoder.h`, and `OutputPanel`'s combo items to
    match): Stereo, Binaural (HRTF), Quad, Octophonic, Circular Array,
    5.1, 7.1, 5.1.2, 5.1.4, 7.1.2, 7.1.4, FOA (1st Order Ambisonics,
    4ch), SOA (2nd Order, 9ch), TOA (3rd Order, 16ch) -- the raw
    Ambisonics orders move from first to last, and Octophonic/Circular
    Array move next to Quad rather than after the Atmos-bed layouts.
    Confirmed via a full-codebase grep that every reference to `Mode` is
    by symbolic name, not ordinal value, except `OutputPanel`'s own
    position-based combo-item-id mapping (already an established,
    intentional pattern) -- so this reorder needed zero changes to any
    switch statement anywhere, only the enum's declaration order and the
    combo's `addItem()` calls (updated to match, with the new names).
  - **`AmbisonicsDecoder::numModes`** (14) added as the single source of
    truth `OutputPanel`'s combo-availability loop iterates against.
  - **Conditional visibility for format-specific controls**, mirroring
    the pattern the Binaural HRTF-dataset controls already established:
    - Circular Array's own "Speaker Count" slider is now only shown
      while Output Format == Circular Array (previously always visible,
      silently inert for every other format -- including Octophonic,
      whose count is fixed).
    - The Octophonic/Circular-Array horizontal-only hint label is shown
      for either of those two formats (its text applies to both).
    - Bass Management is now only shown for formats that actually have
      an LFE channel (5.1/7.1/5.1.2/5.1.4/7.1.2/7.1.4) --
      `AmbisonicsDecoder::lfeChannelIndexFor(mode) >= 0` -- rather than
      always visible but inert for every other format.
    New `OutputPanel::updateCircularArrayRowsVisibility()`/
    `updateBassManagementRowVisibility()`, called from the same places
    `updateBinauralRowsVisibility()` already was (mode-change callback,
    `refreshFromModel()`); `resized()` now only allocates layout space
    for whichever of these rows is actually visible.
  - **Unified "bring auxiliary window to front" fix** (`Source/
    WindowUtils.h`, new -- `WindowUtils::forceToFront()`), applied to
    all three lazily-created toolbar windows (Help/Mappings/Output,
    `KlangorbitEditor::showHelpClicked()`/`showMappingClicked()`/
    `showOutputClicked()`, `Source/PluginEditor.cpp`) uniformly, for
    every plugin format. Reported in Logic Pro (AU): once one of these
    windows had fallen behind the plugin's own host-provided editor
    window (e.g. the user clicks back on the editor), the existing fix
    (each window's own `setAlwaysOnTop(true)`, plus a synchronous
    `toFront()` and a second deferred one via
    `MessageManager::callAsync()`) no longer reliably brought it back --
    a same-level "most recently activated wins" tiebreak between two
    elevated windows that a same-level `toFront()` alone doesn't always
    resolve. Root-caused and fixed the same way in every host, not just
    Logic, since the underlying mechanism isn't host-specific: toggling
    `setAlwaysOnTop()` off then back on before calling `toFront()` forces
    the OS to actually re-apply the elevated window level (many window
    systems only take visible action on the on/off TRANSITION, not on
    reasserting the same value again), which reliably re-wins the
    ordering race. See `WindowUtils.h`'s own comment for the full
    reasoning.
  - **Full documentation** of every format's exact speaker angles,
    channel order, and standards compliance -- verified directly against
    `Source/SpeakerLayouts.h` (the actual source of truth, including its
    own citations), not summarized from memory -- added to the in-app
    Help window (`Source/HelpContent.h`, section 11), `README.md` (new
    "Output formats: channel layouts and standards" section), and
    `Docs/UserGuide.md` (section 12): which formats are exact ITU-R
    BS.775-4 (Stereo/5.1/7.1), which use ITU-R BS.2051-2 angle sectors
    plus Dolby's own consumer height-speaker guidance for the height
    channels (the four Atmos-bed layouts), and which have no ITU/
    standards-body backing at all (Quad -- conventional consumer layout;
    Octophonic -- matches Blue Ripple Sound's "O3A Decoder -- Octagon",
    the one concrete Ambisonics-ecosystem reference found, with this
    project's own channel ordering; Circular Array -- pure geometry, no
    standard). Also confirms channel STREAM order for every named layout
    matches `juce::AudioChannelSet`'s own ordering exactly (verified
    against JUCE source), i.e. what a host/DAW already expects for that
    format name, not just the physical speaker angles.
- **Audio Unit (AU v2) plugin format, for Logic Pro/GarageBand/other AU
  hosts** -- `AU` added to `juce_add_plugin()`'s `FORMATS` list in
  `CMakeLists.txt` (alongside the existing `VST3 Standalone`), producing
  a new `Klangorbit_AU` build target and `Klangorbit.component` bundle.
  Deliberately classic AU v2 (Component Manager), NOT AUv3 -- a
  completely different app-extension packaging model, unneeded for a
  Mac-only, non-sandboxed plugin distributed outside the App Store.
  Registers as `kAudioUnitType_MusicEffect` ("Music Effect", 4-char type
  `aumf`) -- an audio effect that also accepts MIDI, matching the
  existing `NEEDS_MIDI_INPUT TRUE`/`IS_SYNTH FALSE` plugin
  characteristics already declared for VST3 (the MIDI CC/Note/Pitch Bend
  control surface via `MappingEngine`/`MidiDriver` needed this, not a
  separate synth voice) -- JUCE would infer this exact category
  automatically from those same two flags, set explicitly via the new
  `AU_MAIN_TYPE` parameter instead so the choice is documented rather
  than left as an inferred default a reader would have to trace through
  JUCE's own CMake logic to discover. Initially a pure build-configuration
  change with no source edits (every decoder/UI/driver module was already
  architected to be format-agnostic, see e.g. `AmbisonicsDecoder`'s and
  `BinauralDecoder`'s own "AU-reusable" design notes from earlier
  entries) -- until real-world testing in Logic Pro surfaced that the
  plugin didn't appear as insertable on ANY track at all (see the
  flexible-input-channel-count entry directly below, which needed real
  source changes to fix). `COPY_PLUGIN_AFTER_BUILD` installs the
  `.component` to the system-wide `/Library/Audio/Plug-Ins/Components`
  (`AU_COPY_DIR`, matching `VST3_COPY_DIR`'s own system-wide override, for
  consistency between the two formats) -- unlike the VST3 folder, this one
  is root-owned/not world-writable by default, so it needs a one-time
  `sudo chmod 777 "/Library/Audio/Plug-Ins/Components"` before the install
  step can succeed without sudo (documented in the README's Build
  section; not something the CMake build does on its own). Verified with Apple's
  own `auval` validation tool (`auval -v aumf Klor Jgck`) -- full PASS
  across every section (default formats, required/recommended/optional/
  special properties, custom Cocoa UI, factory presets, host callbacks,
  published parameters, channel-capability/format negotiation, and
  render tests including a MIDI test) -- not automated into the CMake
  build, run manually after building/reinstalling `Klangorbit_AU`.
- **AU: the "Live Inputs" bus now accepts Mono/Stereo/Quad/7.1 (1/2/4/8
  channels), not just the fixed 8-channel discrete layout VST3/Standalone
  still require.** Diagnosed a real-world report from the user: the AU
  build passed `auval` fully but didn't appear as an insertable Audio
  Unit on ANY track in Logic Pro -- not Mono, not Stereo, not anything
  else. Root cause, confirmed by directly reading JUCE's own AU wrapper
  source (`AudioUnitHelpers::getAUChannelInfo()`, `JUCE/modules/
  juce_audio_processors_headless/format_types/juce_AU_Shared.h`): it
  builds the AU's `kAudioUnitProperty_SupportedNumChannels` list by
  probing `checkBusesLayoutSupported()` (-> `isBusesLayoutSupported()`)
  across a matrix of standard channel-count layouts for both input and
  output -- our hard-locked `discreteChannels(8)` input bus matched NONE
  of Logic's standard track formats (Mono=1, Stereo=2, Quad=4, 7.1=8-but-
  a-NAMED-layout-not-raw-discrete), so Logic's own per-track AU filtering
  excluded Klangorbit everywhere.
  - `KlangorbitProcessor::isBusesLayoutSupported()` (`Source/
    PluginProcessor.cpp`) now branches at RUNTIME on the inherited
    `AudioProcessor::wrapperType` member (`wrapperType_AudioUnit`/
    `wrapperType_AudioUnitv3`) -- NOT a compile-time `#if
    JucePlugin_Build_AU`, which would be wrong here: `PluginProcessor.cpp`
    is compiled exactly ONCE into `libKlangorbit_SharedCode.a` and that
    single compiled object is linked into all three format targets
    (confirmed via `find build -iname PluginProcessor.cpp.o`, only one
    exists), so a compile-time macro would silently apply to VST3/
    Standalone too. VST3/Standalone (the existing, working Reaper
    workflow) keep the exact original behavior, completely untouched. For
    AU, the input side now accepts any of `mono()`/`stereo()`/
    `quadraphonic()`/`create7point1()` -- NAMED layouts, not raw
    `discreteChannels(N)`, since a host's own format-matching keys off the
    named identity (same reasoning `AmbisonicsDecoder::
    outputChannelSetFor()` already documents for its own real-speaker
    formats) -- `discreteChannels(numLiveInputs)` is ALSO still accepted
    (not replaced) purely so the plugin's own construction-time default
    bus (see below) stays self-consistent; it's otherwise inert for actual
    track matching, since no standard Logic track uses a raw, unnamed
    8-channel layout either.
  - Accepting FOUR input layouts simultaneously for AU is safe in a way
    the OUTPUT-side restriction (see the earlier `isBusesLayoutSupported()`
    Fixed entry) explicitly is NOT: that fix exists because
    `AmbisonicsDecoder::decode()` needs to know exactly which mode is
    active to use the right decode matrix, so a host settling on the wrong
    OUTPUT config produced silence. Reading live input has no equivalent
    stored-state dependency -- it already self-adapts every block to
    however many channels are ACTUALLY present (see the `numInCh` fix
    below, and every per-object read already gated by `obj.inputChannel <
    numInCh`) -- so there is no "wrong config" for a host to settle on
    here, regardless of which of the four it picks. The OUTPUT-side logic
    is completely unchanged.
  - **Constructor-time default bus, AU only**: `KlangorbitProcessor`'s
    constructor now calls `setBusesLayout()` immediately after
    construction, switching the default INPUT bus to `stereo()` when
    `wrapperType` is AU. This was required, not just widening
    `isBusesLayoutSupported()` above: `auval` failed with "Default Layout
    is not published as a supported layout tag" even after
    `discreteChannels(numLiveInputs)` was accepted, because
    `discreteChannels(N)` has no corresponding NAMED CoreAudio
    `AudioChannelLayoutTag` at all -- accepting a layout in
    `isBusesLayoutSupported()` and that layout having a publishable tag
    turned out to be two different things, confirmed empirically (byte-
    identical `auval` failure before and after that first attempt).
    `makeBusLayout()` itself stays unconditionally
    `discreteChannels(numLiveInputs)` for every format (`wrapperType`
    isn't populated yet at that point -- it's read from a thread-local the
    wrapper sets just before construction, not accessible from a static
    function called as a constructor-initializer argument, confirmed via
    JUCE source) -- the AU-specific default is applied as a follow-up
    adjustment in the constructor BODY instead, once `wrapperType` is
    available.
  - **Required correctness fix, `processBlock()`'s `numInCh` clamp**: was
    `jmin(numLiveInputs, buffer.getNumChannels())` -- always evaluated to
    exactly `numLiveInputs` (8) in practice before this change, since
    input was forced to exactly 8 channels for every format and JUCE's
    shared in-place buffer is sized to
    `max(totalNumInputChannels, totalNumOutputChannels)`, always >= 8 as a
    result. Now that AU can legitimately negotiate FEWER real input
    channels (e.g. 2, Stereo) while still having a much wider output bus
    (e.g. 16, raw Ambisonics), `buffer.getNumChannels()` would be 16 even
    with only 2 real input channels -- without the fix, channels 2-15
    (uninitialized/output-purposed buffer memory) would have been
    silently treated as valid extra live-input channels. Fixed to
    `jmin(numLiveInputs, getTotalNumInputChannels(), buffer.getNumChannels())`
    -- `getTotalNumInputChannels()` reflects the currently negotiated main
    input bus width. Every downstream per-object read was already
    correctly bounded against `numInCh` (`obj.inputChannel < numInCh`),
    so this one-line fix was sufficient -- no array resizing needed.
    `numLiveInputs` itself (`SAPOC_MAX_LIVE_INPUTS` = 8, sizing every
    per-object array) is unchanged for every format; objects beyond the
    currently-negotiated input channel count simply have no live audio,
    the same mechanism already used today for any object with
    `inputChannel == -1`.
  - **No new UI.** Confirmed explicitly with the user: Logic's own
    track-type selection at insert time IS the flexible-choice mechanism
    (exactly like any ordinary AU effect) -- no in-plugin picker needed.
  - **Superseded by the output-side fix directly below** -- this
    input-only round shipped with the output side still hard-locked to
    the 16-channel raw-Ambisonics default, which exceeds Logic's own
    12-channel ceiling and can never be routed there regardless of how
    flexible the input side is. Caught by the user directly (not
    self-discovered) after real testing in Logic.
- **AU: the output side is now ALSO flexible, and the plugin (every
  format, not just AU) now starts in Stereo instead of raw Ambisonics.**
  Of `AmbisonicsDecoder::Mode`'s 14 formats, exactly 9 are usable in
  Logic at all: a NAMED JUCE `AudioChannelSet` (so Logic's own
  layout-tag matching recognizes it) AND `<= 12` channels (Logic's own
  ceiling, 7.1.4 -- confirmed by the user). Stereo, Binaural, Quad, 5.1,
  7.1, and the four Atmos-bed formats (5.1.2/5.1.4/7.1.2/7.1.4) qualify;
  raw Ambisonics Order 1/2/3 (unnamed; Order 3 alone already exceeds
  12ch), Octophonic, and CircularArray (both unnamed) do not, and are
  excluded from AU unconditionally.
  - **Design: Logic fixes the output channel COUNT once, at insertion**
    (based on which track/bus type is chosen), and the plugin never asks
    to change it afterward for AU -- deliberately different from the
    input-side strategy above (which accepts multiple simultaneous
    formats with no live-renegotiation risk, since reading input already
    self-adapts every block). Output can't use that same trick: which
    mode is active determines which decode MATRIX gets used, so this
    plugin requesting a live output-channel-count CHANGE is exactly the
    mechanism that caused the original Reaper silence regression (see
    that Fixed entry). Avoided entirely here: `isBusesLayoutSupported()`'s
    AU output branch accepts all 9 Logic-viable layouts simultaneously
    (so JUCE's AU wrapper discovers the full set at insertion, letting
    Logic offer Klangorbit on Stereo/5.1/7.1/Atmos-bed tracks alike --
    same `AudioUnitHelpers::getAUChannelInfo()` probing mechanism as the
    input-side fix), but `setDecoderMode()` (for AU only) never calls
    `setBusesLayout()` again after that -- switching to a mode needing
    FEWER channels than what's already negotiated just uses fewer of them
    internally, with no bus-change request for a host to get stuck on.
  - **Required correctness fix, `AmbisonicsDecoder::decode()`**: it only
    ever cleared/wrote `numOut` channels (`jmin(decodeMatrix.size(),
    destBuffer.getNumChannels())`), silently leaving any remaining
    destination channels untouched. Never reachable before (VST3/
    Standalone always negotiated an exact channel match) -- now that AU
    can have a wider negotiated bus than the active mode needs, those
    leftover channels would have kept stale data from JUCE's shared
    in-place buffer instead of silence. Fixed by explicitly clearing
    channels `numOut..destBuffer.getNumChannels()-1` too.
  - **New `KlangorbitProcessor::isOutputModeAvailable(Mode)`** -- single
    source of truth for "is this mode selectable right now", used both by
    `setDecoderMode()` (a defensive no-op guard) and by `OutputPanel`
    (greys out unavailable combo items via `ComboBox::setItemEnabled()`,
    not removed -- still visible, just unselectable). Always `true` for
    VST3/Standalone (unchanged behavior, every mode has always been
    switchable there); for AU, `false` for the 5 excluded modes
    unconditionally, otherwise `true` only if the mode's own channel
    count fits within `getTotalNumOutputChannels()`. New
    `AmbisonicsDecoder::numModes` constant (14) is the shared source of
    truth for `OutputPanel`'s combo-item-ID loop bound, so it can't
    silently desync from a hardcoded count if a mode is ever added.
  - **Global default startup Output Format is now Stereo, for every
    format** (`makeBusLayout()`'s default output, and the constructor's
    initial `encoder.setOrder()`/`decoder.setMode()`, all changed from
    `rawModeForOrder(SAPOC_DEFAULT_AMBI_ORDER)` -- raw B-format, 16
    channels -- to `Mode::Stereo` directly). Not just an AU
    accommodation: raw B-format is silent/unusable without an external
    decoder, so a fresh plugin instance used to produce no audible output
    at all until the user wired one up or switched Output Format
    manually -- Stereo is audible immediately, in Reaper/Standalone too.
    `SAPOC_DEFAULT_AMBI_ORDER` and the `rawModeForOrder()` helper are now
    fully unused (confirmed via a whole-repo grep) and were removed
    entirely -- the CMake `CACHE STRING`, its `target_compile_definitions`
    line, the helper function, both call sites -- rather than left as
    dead configuration. **This is a real behavior change for existing
    Reaper/Standalone sessions** that may have been relying on the old
    raw-Ambisonics-by-default startup state; switch Output Format back to
    an Ambisonics order manually if needed (still fully supported, just
    no longer the default).
  - Verified with `auval -v aumf Klor Jgck`: full PASS, default format
    now 2ch in / 2ch out (Stereo/Stereo), and `Reported Channel
    Capabilities (explicit)` now shows the full cross product of all 4
    input formats against 6 distinct output channel-count buckets (2, 4,
    6, 8, 10, 12) -- `[1,2] [1,4] [1,6] [1,8] [1,10] [1,12] [2,2] ...
    [8,12]`, 24 pairs total. Whether Logic's own insert UI actually
    offers/accepts Klangorbit cleanly across all of these -- especially
    whether the Output Format dropdown's greying and live switching
    between available formats behaves as expected -- still needs
    live-testing in Logic itself; `auval` confirms the AU-protocol layer
    is correct, not the full in-host experience.
- **Binaural (HRTF-based) headphone output, the 14th selectable Output
  Format, closing the gap left by the original decoder-feature spec**
  (which explicitly deferred it pending a licensing decision, see the
  older entry below). New `Mode::Binaural` in `AmbisonicsDecoder`
  (`AmbisonicsDecoder.h/.cpp`) represents it as a selectable format only
  (2ch, stereo() bus, fixed order-3 encode) -- the actual decode is a
  separate module, `BinauralDecoder` (`Source/BinauralDecoder.h/.cpp`),
  invoked from `KlangorbitProcessor::processBlock()` instead of
  `AmbisonicsDecoder::decode()` while this mode is active, following the
  same "Binaural is NOT part of `AmbisonicsDecoder`" boundary that class
  already documented (an HRTF dataset + convolution is a genuinely
  different dependency shape, kept out of the AU-reusable decoder
  modules).
  - **Technique**: the same two-stage shape AllRAD already uses for the
    irregular real-speaker layouts (Quad/5.1/7.1/Atmos-bed) -- decode the
    Ambisonics bus to a dense, 50-point virtual loudspeaker array first
    (`SphericalHarmonicsUtils::fibonacciSphere()`, max-rE-weighted SH
    sampling decode -- the exact same point distribution and decode
    weighting AllRAD uses, extracted out of `AmbisonicsDecoder.cpp`'s
    former anonymous namespace into the new shared
    `Source/SphericalHarmonicsUtils.h` specifically so this reuse is
    literal code sharing, not a second hand-copied implementation that
    could drift out of sync) -- except the second stage convolves each
    virtual speaker's own mono signal through that direction's measured
    left/right head-related impulse response (`juce::dsp::Convolution`,
    one L/R pair per virtual speaker, 100 engines total) and sums to a
    stereo output, instead of VBAP-remapping to a handful of real
    speakers. See `BinauralDecoder.h`'s class comment for the full
    per-block breakdown.
  - **HRTF dataset loading**: new `Source/HrtfDataset.h/.cpp`, a thin
    RAII wrapper around [libmysofa](https://github.com/hoene/libmysofa)
    (`mysofa_open`/`mysofa_getfilter_float`/`mysofa_close`) -- vendored
    via CMake `FetchContent`, pinned to `v1.3.5` (a hardening/security
    release). One code path handles both bundled defaults and a
    user-supplied custom file identically. Confirmed via source
    inspection that this project's own Vec3 convention (front=+x,
    left=+y, up=+z) matches SOFA/libmysofa's own Cartesian convention
    exactly -- no coordinate remapping needed anywhere in the new code,
    a direction can be passed straight through to
    `mysofa_getfilter_float()`'s x/y/z parameters.
  - **Two bundled datasets, both selectable via a new "HRTF Dataset"
    picker** in the Output window (only shown while Output Format ==
    Binaural), plus a third "Custom SOFA file..." option -- per explicit
    direction to bundle BOTH candidate datasets as the default rather
    than choosing one, with custom import as an ADDITIONAL option on top
    (not instead of):
    - **MIT KEMAR** (Gardner & Martin, MIT Media Lab) -- freely usable
      with citation, effectively public-domain-with-attribution. 710
      measurement positions, 44.1kHz. Default selection.
    - **SADIE II -- subject D1 (KU100 mannequin)**, University of York --
      Apache License 2.0 (OSI-approved, commercial-compatible), citation
      requested (DOI 10.3390/app8112029). Sourced directly in SOFA
      format from Zenodo record 12092466; only the 44.1kHz variant is
      bundled (extracted from the full multi-sample-rate archive) to
      keep the shipped size down.
    - Both `.sofa` files are embedded via `juce_add_binary_data`
      (`HrtfData` CMake target, JUCE's own resource-embedding mechanism
      -- first use of it in this project) rather than shipped as loose
      files alongside the plugin bundle, so VST3/Standalone/a future AU
      build all behave identically with no install-time asset-location
      logic needed. Since `libmysofa` needs a real filesystem path (not
      an in-memory buffer), `KlangorbitProcessor` writes each bundled
      dataset's embedded bytes to a cached temp file once (skipped on
      subsequent instantiations if a file of the expected size is
      already there) and opens `HrtfDataset` from that path --
      sample-rate-dependent resampling still happens fresh in
      `HrtfDataset::load()` itself on every prepare, only the disk-write
      step is cached.
    - Exact required attribution text for both datasets, plus
      `libmysofa`'s own BSD-3-Clause notice, now lives in a new
      `THIRD_PARTY_LICENSES.md` at the project root.
  - **Custom SOFA import**: "Custom SOFA file..." in the dataset picker
    opens a `juce::FileChooser` (reusing the exact pattern already used
    for preset load/save) filtered to `*.sofa`; `KlangorbitProcessor::
    loadCustomSofaFile()` loads it into a dedicated `HrtfDataset` and
    switches to it immediately on success, or leaves the previously
    active dataset untouched and shows an alert on failure -- a bad file
    the user picks never silences the plugin.
  - **Dataset switching** (`KlangorbitProcessor::setBinauralDataset()`/
    `loadCustomSofaFile()`) rebuilds `BinauralDecoder`'s decode matrix
    and all 100 convolution engines immediately, same "rebuild now, not
    per audio block, message thread only" idiom `setMode()`/
    `setCircularArraySpeakerCount()` already established for the other
    decoded modes. `prepareToPlay()`/switching the Output Format into
    Binaural both trigger this too (at whatever sampleRate/block size is
    current then) -- deliberately NOT unconditional on every
    `prepareToPlay()` call regardless of mode, since reloading SADIE
    II's ~36MB from disk on every playback start/stop for hosts that do
    that frequently would add needless latency for the common case where
    Binaural isn't even the active format.
  - **Disclosed limitations** (see README's "Known limitations" and
    `BinauralDecoder.h`'s own comment): CPU cost (50 virtual speakers x
    2 convolution engines per block) is not measured on real hardware in
    this environment -- 50 was chosen to match AllRAD's own
    already-proven virtual-array density, not independently profiled,
    and is easy to tune down later if needed; interaural delay (ITD) is
    NOT applied in this version -- `HrtfDataset::getFilter()`'s own
    per-ear delay outputs are measured but currently discarded, only
    each ear's amplitude/spectral HRIR shape is used, affecting
    precision of timing-dependent localization cues (mainly front-back/
    elevation) more than level-dependent ones.
  - **Tests**: new `Tools/verify_binaural_decoder.cpp` (registered as
    `verify_binaural_decoder` in CMakeLists.txt) -- loads both real
    bundled SOFA files directly via `HrtfDataset` and sanity-checks
    `getFilter()` at 5 known directions (correctly-sized, finite,
    non-silent taps); separately, prepares a real `BinauralDecoder`
    against KEMAR and asserts basic physical plausibility rather than
    exact sample matches (matching the existing `verify_propagation`/
    `verify_ambisonics_decoder` testing style): a hard-left source comes
    out louder in the left output channel than the right and vice versa
    for hard-right (interaural level difference), silence in produces
    silence out (using a freshly-prepared decoder instance for that
    check specifically, since `juce::dsp::Convolution` is a stateful
    overlap-add engine and would legitimately still be finishing a decay
    tail from a just-processed non-silent block otherwise -- not a bug),
    and `decode()` without a successful `prepare()` is a safe no-op.
    Deliberately tests against the REAL bundled KEMAR dataset rather
    than a synthetic hand-built HRTF (the original plan's stated
    approach) -- KEMAR is small enough (710 directions, ~11ms filters)
    to `prepare()` quickly, had already been empirically confirmed to
    load correctly via a standalone smoke test built while de-risking a
    known libmysofa/SADIE compatibility issue (GitHub issue #42, not
    reproduced with the pinned v1.3.5 against either bundled file), and
    exercises the true end-to-end pipeline rather than only
    `BinauralDecoder`'s own plumbing in isolation.
- **Camera orientation gizmo, bottom-left of the 3D viewport.** A small
  fixed-size red/green/blue arm indicator (`KlangorbitEditor::
  drawAxisGizmo()`) showing the world X/Y/Z directions as the camera's
  own rotation currently sees them -- projects each unit axis through
  `Camera3D::getRight()`/`getUp()` the same dot-product formula
  `Camera3D::project()` uses for its own x/y, but applied to a
  direction instead of a world position, so it needs only the camera's
  rotation, not a full perspective projection -- then draws it at a
  fixed screen anchor/length, independent of zoom or where the camera
  is actually looking. An axis pointing straight at/away from the
  camera correctly foreshortens to a point (e.g. Z in the default
  top-down view, since that view looks straight down the Z axis) --
  expected behavior for any such gizmo, not a bug.
- **Double-click any parameter slider to reset it to its default
  value.** Every `FloatRowComponent`/`Vec3RowComponent` slider across
  every category now has a default wired up
  (`FloatRowComponent::setDefaultValue()`/`Vec3RowComponent::
  setDefaultValue()`, thin wrappers around `juce::Slider`'s own
  built-in `setDoubleClickReturnValue()` -- no custom mouse handling
  needed). Each `ParameterPanel::add*Row()` helper computes its row's
  default by reading the member off a fresh, default-constructed
  instance of the owning struct (`SceneSettings{}.*member`,
  `SoundObject{}.*member`, `GrainCloudSettings{}.*member`) rather than
  needing a separate default value threaded through every call site --
  the schema's own default member initializer IS the reset target.
- **Grain "Movement Mode" now only shows the parameters that apply to
  the currently selected mode.** Previously all ~13 mode-specific rows
  (Random Walk Speed, Boundary Radius/Jitter/Restitution for Bounce,
  Initial Speed/Jitter/Acceleration for Radial Explosion, Orbit
  Radius/Jitter/Angular Speed/Sphere Spread for Orbit Around Parent,
  Attraction Strength for Attract/Repel Siblings) were always visible
  at once regardless of which mode was active, most of them inert.
  `ParameterPanel::addToLayout()`/`addGrainFloatRow()` gained an
  optional `requiredMovementMode` tag; `updateGrainMovementModeVisibility()`
  (new, called alongside `updateOrbitModeHintVisibility()`'s own
  trigger points -- category switch, selection/preset change, and the
  Movement Mode combo's own `onSelected`) hides any tagged row whose
  mode doesn't match `editedGrainCloud->movementMode`, on top of the
  existing per-category visibility pass.
- **Doppler Enabled toggle, top of the (per-object) Doppler category.**
  New `SoundObject::dopplerEnabled` (default `true`) -- a quick on/off
  switch that doesn't touch the `dopplerFactor` dial below it:
  `PropagationProcessor` now treats the AC (pitch-shift-driving)
  contribution as 0 whenever `dopplerEnabled` is false, regardless of
  the stored `dopplerFactor` value, so turning it back on restores
  whatever was actually dialed in rather than needing to remember and
  re-type a value that was overwritten to 0. Mirrors
  `GrainCloudSettings::dopplerEnabled`'s already-existing role for
  grains (that one defaults off, matching Doppler being an optional
  add-on for grains specifically; this one defaults on, matching the
  object's own Doppler being on by default already). Registered in
  `ParameterRegistry` (id `"dopplerEnabled"`, same as the grain-level
  one -- no collision, since grain ids carry a `.grain.` infix) and
  serialized in presets (`PresetManager.cpp`, optional/backward-
  compatible -- an old preset without the field simply gets the
  default `true`, matching pre-existing behavior exactly). New
  `Tools/verify_propagation` case confirms `dopplerEnabled=false`
  suppresses the pitch shift independent of `dopplerFactor` (still at
  its default 1.0), and that the toggle never touches the stored
  `dopplerFactor` value itself.
- **Output Format/Bass Management/Circular Array speaker count moved to
  their own "Output..." toolbar window.** New `OutputPanel`/
  `OutputWindow` (mirroring `MappingPanel`/`MappingWindow`'s own
  structure exactly), opened via a new "Output..." button next to
  "Mappings..." in the toolbar -- same lazily-created, hide-not-destroy
  window lifetime as Help/Mappings. These are plugin-wide settings tied
  to neither the scene nor any object, so they now get their own place
  instead of sharing a tab with either (see the Changed entry below for
  the corresponding removal from `ParameterPanel`). Re-syncs itself
  (`OutputWindow::refreshFromModel()`) every time it's shown, in case
  something else (e.g. a preset load) changed the decoder mode since it
  was last open.
- **Fixed default gamepad control scheme.** Extends `GamepadDriver`'s
  existing "left stick moves the selected object" default with a full
  set of sensible-out-of-the-box bindings, no mapping profile required
  (all still genuinely overridable via Learn mode -- see "Controller
  mapping" above for the pre-existing override mechanism, unchanged
  here):
  - **Object cycle/add/remove**: Button X cycles the selection to the
    next active object (wrapping); Button A activates the next inactive
    object slot and selects it (mirrors the "+ Object" toolbar button);
    Button B deactivates the currently selected object and clears the
    selection (mirrors "- Remove Object"). All edge-triggered (fire once
    per physical press, not once per tick while held) and implemented in
    `GamepadDriver::driveObjectManagement()`, which runs from
    `KlangorbitProcessor`'s own background timer -- keeps working with
    no editor window open, same as gamepad movement always has.
    `KlangorbitEditor::resyncFromBackgroundObjectChanges()` (new) keeps
    the editor's own object-list highlight/selection in sync with
    whatever this changed in the background.
  - **Gamepad-driven Free Throw / Orbit Shot / Slingshot**: holding
    Button Y / Left Shoulder / Right Shoulder aims the corresponding
    launch mode (the same three the mouse's Shift+drag sling gesture
    offers, see `SlingGesture.h`) using the left stick's direction and
    magnitude; releasing fires it.
    `GamepadDriver::driveThrowGesture()` reuses `SlingGesture`'s own pure
    math functions directly (pull-vector velocity/orbit-radius scaling,
    orbit orientation/direction sign, the Slingshot auto-target
    fallback) rather than a second, drifting-out-of-sync copy of the
    same formulas. Deliberately a DIRECT analog mapping ("push the stick
    the way you want it to launch"), not the mouse gesture's pull-BACK-
    then-release metaphor -- that metaphor only makes sense with a
    visible cursor being dragged away from the object, which a gamepad
    stick has no equivalent of. Suspends the left stick's own movement
    behavior while a throw button is held (same "don't fight over the
    same stick" mechanism already used for `MappingEngine` overrides).
    Runs from the background timer too, so it works with no editor open
    -- unlike the mouse gesture, there is deliberately no live visual
    aim preview (Camera3D/rendering are editor-only, see below), and no
    equivalent of the mouse gesture's Ctrl/Alt/Tab modifiers: Orbit Shot
    always centers on the world origin at a fixed circular
    (eccentricity 0) shape, and Slingshot always auto-targets the first
    other active object (falling back to a plain, unaffected throw if
    none exists) -- disclosed scope decisions, not oversights; both
    remain available via the mouse gesture for anyone who wants to pick
    a specific center/target.
  - **Camera look/zoom**: the right stick orbits the camera
    (azimuth/elevation) and the D-pad's Up/Down zoom in/out --
    deliberately NOT part of `GamepadDriver` (`Camera3D` is purely
    editor-owned view state, meaningless with no editor open to look
    at). Polled instead by `KlangorbitEditor`'s own timer via the new
    `GamepadDriver::getLastState()`/`KlangorbitProcessor::
    getGamepadState()` accessors, in a new `updateGamepadCamera()`.
    Continuous polling (not the canonical hub's change-only dispatch) on
    purpose: a look-around control needs to keep turning while the stick
    holds a steady nonzero deflection, not just at the instant it
    changes. Sign convention is a reasonable best guess, not yet
    manually verified against real hardware in this environment -- same
    disclosed caveat as this project's existing camera-drag/gamepad-
    movement conventions (see "Known limitations" below), and just as
    easy to flip if it feels backwards.
  - `GamepadDriver::poll()`'s `selectedObjectIndex` parameter is now
    passed by reference (was by value) so the new object-management
    behavior can update it directly, exactly as if the change had come
    from the editor's own `selectObject()`.
  - New `Tools/verify_gamepad_driver` (cycle wrap-around and inactive-
    slot skipping, add/remove mirroring the toolbar buttons' own
    behavior, edge-triggering vs. holding, the full aim-then-release
    sequence for all three throw modes including the min-pull-distance
    fire gate, "nothing selected at press time never starts an aim,"
    and simultaneous-button-press priority) -- calls
    `driveObjectManagement()`/`driveThrowGesture()` directly against a
    real `TrajectoryEngine` with hand-built `GamepadState` pairs, no
    connected controller needed (both are public specifically for this;
    `GamepadBridge`'s actual hardware polling remains the one part of
    this driver that isn't testable headlessly, verified instead by full
    build + Standalone launch stability as before).
- **MIDI and OSC drivers -- two more sources on the canonical input
  layer.** Confirms the architecture's own promise, made when the
  parameter registry/canonical input layer/`MappingEngine` were first
  built (see below): a new input source can be added as a thin driver
  that only translates its own protocol into `CanonicalInputEvent`s and
  dispatches them via the existing `CanonicalInputHub` -- zero changes
  needed to `ParameterRegistry`, `CanonicalInputHub`, or `MappingEngine`
  itself. `GamepadDriver` was the first proof of that; `MidiDriver`
  (`Source/MidiDriver.h/.cpp`) and `OscDriver`
  (`Source/OscDriver.h/.cpp`) are the second and third. Both are bindable
  via the exact same Learn-mode UI (Mappings window) as gamepad controls
  -- no separate MIDI-Learn or OSC-Learn workflow needed.
  - **MIDI**: translates Control Change, Note On/Off, and Pitch Bend
    (the message types a real controller's knobs/faders/pads/keys/pitch
    strip actually send -- Aftertouch/Program Change/etc. deliberately
    left out, not an oversight, addable the same way if ever needed).
    `sourceId` scheme: `"Midi0.CC<N>.Ch<C>"`, `"Midi0.Note<N>.Ch<C>"`,
    `"Midi0.PitchBend.Ch<C>"` (`"Midi0"` is a fixed placeholder for
    "this plugin's MIDI input," matching `GamepadDriver`'s own
    `"Gamepad0"` precedent -- JUCE/every plugin host delivers MIDI into
    one already-merged buffer per block, with no per-device identity
    available to a plugin). CC -> `Continuous`/`Unipolar`, value
    rescaled 0..127 -> 0..1. Note On/Off -> `Button`, value exactly
    1.0/0.0 -- velocity is deliberately NOT captured as a continuous
    value, staying consistent with `Kind::Button`'s own 0/1-only
    contract (a disclosed scope decision, not a silent gap). Pitch Bend
    -> `Continuous`/`Bipolar`, 0..16383 (center 8192) rescaled to -1..1.
    Threading: MIDI genuinely arrives on the audio thread via
    `processBlock()`; `MidiDriver::processMidiBuffer()` only does a
    cheap, bounded push of lightweight POD events into a
    `juce::CriticalSection`-locked queue there (no string/heap
    allocation on the audio thread), and `drainAndDispatch()` -- called
    once per control-rate tick from `KlangorbitProcessor`'s own timer,
    the same message-thread timer `GamepadDriver` already polls from --
    does the actual translation and `CanonicalInputHub::dispatch()`
    calls. Keeps `MappingEngine`'s own binding list (mutated by
    Learn-mode UI actions on the message thread) from ever being touched
    concurrently by two different threads.
  - **OSC**: listens on a UDP port (default 9000, the common TouchOSC-
    style default) via `juce::OSCReceiver`. `sourceId` is
    `"OSC." + addressPattern` verbatim (e.g. address `/orbit/x` ->
    `"OSC./orbit/x"`). A message with a float or int32 first argument
    dispatches as `Continuous`/`Unipolar`, that argument clamped to
    [0,1] and used AS the canonical value directly -- not rescaled from
    some other assumed range, since common OSC control-surface apps
    (TouchOSC, Lemur, etc.) already send normalized 0..1 values by
    convention. A message with no arguments dispatches as `Button`
    (value 1.0) -- the "bare trigger" convention some OSC senders use
    for a button press; OSC has no native "release" concept, so only a
    press is ever produced this way. A message whose first argument is
    non-numeric (string/blob) produces nothing. This interpretation
    logic is extracted into a pure, free function,
    `OscInterpretation::interpretMessage()`, specifically so it's
    unit-testable with hand-built `juce::OSCMessage`s and no real
    network socket. Threading: unlike MIDI, no queue is needed --
    `juce::OSCReceiver::Listener<MessageLoopCallback>` (the default)
    already marshals its callback onto the message thread internally, so
    `OscDriver` dispatches straight to `CanonicalInputHub` from
    `oscMessageReceived()`.
  - No UI yet for configuring the OSC port or displaying MIDI/OSC
    connection status (`KlangorbitProcessor::isOscConnected()`/
    `getOscPort()`/`setOscPort()` exist for a future indicator) -- same
    "no UI yet" scope gap already accepted for `GamepadDriver`'s own
    deadzone/curve/inertia-acceleration tuning, not an oversight.
  - New `Tools/verify_midi_driver` (CC/Note On/Note Off/Pitch Bend
    translation and value/polarity conversion, an empty-queue
    `drainAndDispatch()` no-op, multiple messages in one block, an
    untranslated message type producing nothing) and
    `Tools/verify_osc_driver` (float/int32 argument handling, the
    zero-argument bare-trigger convention, a non-numeric first argument
    correctly producing nothing, out-of-range value clamping) -- both
    exercise the translation logic directly against hand-built
    `juce::MidiMessage`/`juce::OSCMessage`s, no real hardware/network
    socket needed.
- **Controller mapping -- Learn mode, paging/banking, and saveable
  mapping profiles.** Closes the loop on the parameter registry and
  canonical input layer (see below): `MappingEngine` (`Source/
  MappingEngine.h/.cpp`) listens on the `CanonicalInputHub` and, in
  Learn mode, binds the next control that moves to a chosen
  `ParameterRegistry` target -- a real, protocol-neutral "MIDI-Learn,
  but for any input source" system, not gamepad-specific (now confirmed
  to work identically for `MidiDriver`/`OscDriver` above, exactly as
  originally intended -- both post to the same hub as `GamepadDriver`).
  New "Mappings..." toolbar button opens a window
  (`Source/MappingWindow.h/.cpp` + `Source/MappingPanel.h/.cpp`): pick a
  target parameter, press Learn, move a control -- done. Shows the
  current binding list (with per-binding Remove) and the active paging
  modifier/bank.
  - **Paging/banking**: a held modifier source (default
    `"Gamepad0.RightShoulder"`, configurable via a mapping profile's
    `modifierSourceId`) unlocks a second mapping layer (bank 1) -- the
    same physical control can drive a different parameter depending on
    whether the modifier is held. Exactly two banks, matching the
    project's own "a second layer" framing. Generalizes the same "a held
    modifier changes what a gesture means" principle the sling launch
    gesture's own Ctrl/Alt modifiers already established for mouse
    gestures (`SlingGesture.h`) into a protocol-neutral mapping
    mechanism, rather than being mouse/keyboard-specific code reused
    as-is (paging works against canonical input sources, which mouse
    modifier keys aren't).
  - **The target-parameter picker only lists `Scope::Global` and
    `Scope::SelectedObject` parameters**, not the per-object-slot
    `Scope::SpecificObject` variants (8 slots x every field) -- keeps
    the combo box to a manageable size for a workflow built around
    "whichever object is currently selected" anyway. A `SpecificObject`
    binding can still be authored by hand-editing a mapping profile
    JSON; `MappingEngine` itself isn't restricted to those two scopes.
  - **Cross-polarity conversion, handled explicitly**: binding a bipolar
    source (e.g. a stick axis, -1..1) to a unipolar target (e.g. Gain,
    0..1) rescales by RELATIVE POSITION across each side's own full
    range (so the stick's full travel reaches the target's full range),
    rather than clamping the source's negative half away, which would
    have silently made half the stick's travel do nothing. Verified by
    a dedicated test.
  - **The left stick's built-in gamepad rate-control movement (see
    below) is genuinely overridable now, not just described as such**:
    `GamepadDriver::setLeftStickOverrideQuery()` lets `MappingEngine`
    tell the driver "an explicit binding now claims the left stick," in
    which case the built-in movement behavior steps aside entirely for
    that tick rather than fighting the user's own mapping over the same
    object. Wired up in `KlangorbitProcessor`'s constructor. Movement
    itself stays a special-cased built-in behavior rather than an
    ordinary `MappingBinding` -- it needs behavior (Manual-mode
    switching, `manualVelocityActive` ownership/handoff, deadzone/curve
    shaping) that doesn't fit "write one normalized value into one
    registered parameter."
  - **Mapping profiles are their own file format and schema**, entirely
    separate from `Presets/schema/` -- own `schemaVersion` counter (see
    `MappingProfiles/schema/README.md` for the full field reference and
    the "why separate" reasoning, mirroring `Presets/schema/README.md`'s
    own reasoning for its independence from the code version).
    `MappingProfileManager` (`Source/MappingProfileManager.h/.cpp`)
    mirrors `PresetManager`'s own save/load/`juce::Result` conventions
    exactly. New `MappingProfiles/factory/` + `MappingProfiles/user/`
    directories (parallel to `Presets/factory/`/`Presets/user/`).
    Ships one factory default (`MappingProfiles/factory/default.json`)
    with an EMPTY binding list, by design: the left stick's rate-control
    movement already works out of the box via `GamepadDriver`'s own
    built-in behavior (see above), with no binding required -- an empty
    default is the honest starting point, not a placeholder that needed
    filling with invented example bindings.
  - Known minor gap: the Mapping panel's own "profile status" line
    doesn't reflect a mapping profile loaded automatically at plugin
    startup (`KlangorbitProcessor`'s constructor loads the factory
    default directly into `MappingEngine`, bypassing the panel, which
    isn't constructed yet at that point) -- shows "(no profile loaded)"
    even though the (empty, so behaviorally identical either way)
    default was in fact loaded. Cosmetic only; the actual binding state
    is correct.
  - New `Tools/verify_mapping_engine` (Learn-mode capture including the
    modifier source never being a bindable target, binding application
    and cross-polarity conversion, paging/banking, `addBinding()`'s
    replace-not-accumulate semantics, `removeBinding()`, mapping-profile
    save/parse/load round-trip, `schemaVersion` rejection, load-replaces
    -not-merges).
- **Gamepad control -- the selected object moves with a connected
  controller's left stick.** First concrete driver on the canonical
  input layer (see below): `GamepadBridge` (`Source/GamepadBridge.h/
  .mm`, a thin Objective-C++ bridge to Apple's GameController framework
  -- `GCController`/`GCExtendedGamepad`, macOS-only) + `GamepadDriver`
  (`Source/GamepadDriver.h/.cpp`), owned and polled by
  `KlangorbitProcessor` at control rate (~90Hz, the same timer driving
  `TrajectoryEngine::update()` -- NOT `processBlock()`/the audio thread,
  since GameController framework calls aren't real-time-safe). Exactly
  one gamepad at a time (whichever controller is first in
  `GCController.controllers` with an `extendedGamepad` profile); not
  architected to make multi-controller support impossible later, but not
  built here either.
  - **Movement is rate-control by default**, not an absolute target
    position and not an accumulating/persisting force: stick deflection
    continuously sets the selected object's CURRENT velocity while
    deflected; centering the stick (including via the stick's own
    spring-back) sets that velocity to exactly zero immediately -- the
    object stops exactly where it is, does not keep moving, and does not
    return to its starting point. Implemented via a new
    `SoundObject::manualVelocity`/`manualVelocityActive` pair:
    `TrajectoryEngine::integrate()`'s `Manual` mode case (previously a
    complete no-op -- "position set externally via `dragTo()`") now
    integrates position from `manualVelocity` whenever
    `manualVelocityActive` is true. Mouse dragging
    (`TrajectoryEngine::dragTo()`) never sets that flag, so it stays
    false throughout a pure mouse interaction and this addition is a
    verified no-op for that path -- important, because `dragTo()`
    already sets its own rough velocity estimate for Doppler purposes,
    which an earlier, unconditional version of this change would have
    silently overwritten every control-rate tick. Caught and fixed
    before landing; regression-guarded by a new
    `Tools/verify_orbit` test.
  - **Separate, off-by-default inertia mode**
    (`GamepadDriver::setInertiaModeEnabled()`): while held, the stick
    instead nudges the object's REAL `Impulse`-mode velocity
    (`obj.velocity += stickDirection * inertiaAcceleration * dt`); once
    released, `TrajectoryEngine::integrate()`'s existing `Impulse` case
    (damping, `dragCoefficient`, `maxVelocity` clamp, room-boundary
    bounce -- all already there) decelerates it naturally. Reuses real,
    existing physics rather than a second, scripted inertia model.
  - **Deadzone + exponential response curve**, extracted into their own
    small, reusable, protocol-neutral header (`Source/AxisShaping.h`,
    `shapeAxis (raw, deadzone, curveExponent)`) rather than a
    `GamepadDriver`-private method, specifically so it's testable without
    real hardware and reusable by a future MIDI/OSC driver wanting the
    same shaping for its own continuous controllers. Deadzone default
    8% (within the requested 5-10%), rescaled so there's no output jump
    right past the boundary; response curve `output = sign(x) *
    rescaled^exponent`, default exponent 2.0, giving fine control near
    center and reserving full speed for a more deliberate deflection.
    Both tunable (`GamepadDriver::setDeadzone()`/`setCurveExponent()`).
    Deliberately applied only when this driver interprets the stick for
    its own movement behavior above -- the raw, unshaped stick value is
    what gets dispatched to the canonical input hub (see below), so a
    future mapping consumer binding it to something else entirely isn't
    forced through movement-specific shaping.
  - Also dispatches a `CanonicalInputEvent` (see the canonical-input
    entry below) for every axis/button that changes between polls --
    both sticks, both triggers, the four face buttons, both shoulder
    buttons, and the D-pad (`sourceId`s like `"Gamepad0.LeftStick.X"`,
    `"Gamepad0.ButtonA"`) -- change-detected so an untouched gamepad
    doesn't flood the hub every tick. No consumer exists yet (that's the
    Learn-mode mapping branch); this is purely producer-side.
  - New `Tools/verify_axis_shaping` (deadzone clamping at/inside the
    boundary, no jump just past it, full-scale endpoints always +-1
    regardless of tuning, sign symmetry, exponent ordering, monotonicity)
    and two new `Tools/verify_orbit` cases (rate-control moves at exactly
    the set velocity and stops with zero drift when centered; the mouse-
    drag no-regression guard above). `GamepadBridge`'s actual hardware
    polling isn't testable headlessly (no gamepad connected in this
    environment) -- verified instead by full build/link (including the
    `GameController` framework) and a Standalone launch/stability check,
    consistent with how this project verifies that class of integration
    elsewhere.
  - Not verified against real gamepad hardware in this environment (none
    available) -- the world-space stick-to-movement mapping (stick "up"
    = object moves further away/+X, stick "right" = object moves right/
    -Y in this project's own y=left-positive convention) is a reasonable,
    documented, but UNVERIFIED-by-eye choice, same caveat this project's
    README already carries for the camera drag/zoom sign convention.
- **Canonical controller-input layer -- protocol-neutral abstraction for
  gamepad/MIDI/OSC, no driver or consumer yet.** New `CanonicalInputEvent`
  + `CanonicalInputHub` (`Source/CanonicalInput.h/.cpp`), owned by
  `KlangorbitProcessor` (`getCanonicalInputHub()`, parallel to how it
  already owns `ParameterRegistry`, see above). Builds directly on the
  parameter registry: a `CanonicalInputEvent` is "one control changed,
  in a protocol-neutral shape" -- a stable, driver-chosen `sourceId`
  (e.g. `"Gamepad0.LeftStick.X"`), a `Kind` (`Continuous` for a stick
  axis/analog trigger, `Button` for a discrete digital control), a
  normalized `value`, and a `Polarity` (reuses
  `ParameterRegistry::Polarity` rather than duplicating the same
  two-valued concept, since a mapping consumer needs both a canonical
  value and a target parameter's own polarity to convert correctly via
  `Descriptor::denormalize()`). `CanonicalInputHub` is a small,
  thread-safe (snapshot-before-call, so a listener reentrantly adding/
  removing another listener mid-dispatch can't deadlock) broadcast
  point: any driver posts events to it, any number of consumers
  (a future Learn-mode mapping engine, or a Learn-mode UI watching for
  "what did the user just move") can listen, without either side
  knowing about the other.
  - Deliberately NOT `juce::ListenerList` -- `dispatch()` needs to be
    safely callable from whichever thread a driver happens to poll/
    receive on (a gamepad driver polling from a timer, a hypothetical
    future MIDI driver's own callback thread), which isn't something
    `juce::ListenerList` specifically guarantees; a plain
    `juce::CriticalSection`-protected vector, snapshotted before
    calling out to listeners, is simple and sufficient at the expected
    listener-count scale here (a handful, not hundreds).
  - This is the whole point of the layer: adding MIDI or OSC later
    means writing a new, thin driver that translates ITS OWN
    protocol-specific state into the same `CanonicalInputEvent` shape
    and posts to the same hub -- this header and `CanonicalInputHub`
    itself shouldn't need to change. Not built or scheduled as part of
    this branch, only kept open for it.
  - New `Tools/verify_canonical_input` (basic dispatch and field
    round-tripping, multiple listeners all receiving the same event,
    `removeListener()` actually stopping delivery, `addListener()`
    idempotency, and a reentrant add/remove-during-dispatch scenario
    confirming the snapshot-before-call design is deadlock/corruption-
    safe).
- **Parameter registry -- foundation for controller mapping (gamepad/MIDI/
  OSC), no consumers yet.** New `ParameterRegistry` (`Source/
  ParameterRegistry.h/.cpp`): a central register of every controllable
  parameter, built once at construction
  (`KlangorbitProcessor::buildParameterRegistry()`) instead of a fixed
  list living in mapping-specific code, so a future controller-mapping
  layer (gamepad, later MIDI/OSC) can enumerate/bind against parameters
  generically. This branch adds ONLY the register and the migration of
  existing parameters into it -- no gamepad, MIDI, or OSC code at all.
  - Each registered `Descriptor` is self-contained: stable ID, display
    name, category (for a future mapping-UI parameter picker), REAL
    (not normalized) value range + polarity (`Unipolar` 0..1 or
    `Bipolar` -1..1), type-erased get/set, and a binding `Scope`:
    `Global` (one instance, e.g. `SceneSettings::roomSize`),
    `SpecificObject` (a fixed object slot, by id, regardless of current
    UI selection), or `SelectedObject` (dynamically resolves against
    whichever object is currently selected -- inert, not an error,
    while nothing is selected). `normalize()`/`denormalize()` convert
    between a parameter's real range and 0..1/-1..1 for a future mapping
    consumer.
  - **Deliberately NOT built on `juce::AudioProcessorParameter`.**
    Every parameter in this codebase today is a plain public struct
    field (`SoundObject`, `GrainCloudSettings`, `SceneSettings`),
    read/written directly by the GUI (`ParameterPanel`'s own
    pointer-to-member bindings) and the audio thread, with no
    abstraction layer at all -- `juce::AudioProcessorParameter` is
    built for host-automatable, identity-stable parameters declared
    once at construction, a poor fit for up to 8 objects' worth of
    fields (most inactive at any time) and, more fundamentally, for
    `Scope::SelectedObject`, which by design retargets which underlying
    field a single registered parameter resolves to from one call to
    the next -- something JUCE's parameter identity model isn't meant
    to express. This registry has no host-automation ambition; it
    exists purely for internal controller-mapping use. A lightweight
    descriptor generalizing `ParameterPanel`'s own existing
    pointer-to-member binding idiom behind a common, type-erased
    interface was the closer fit.
  - **Migrated every existing controllable parameter** that already had
    a `ParameterPanel` slider/toggle: `SoundObject` (physics, attraction,
    orbit -- including `orbitOrientation`, which has a real, well-defined
    range but no `ParameterPanel` slider yet -- Doppler, mute/solo),
    `GrainCloudSettings` (audio + movement side, all five movement
    modes' parameters), and `SceneSettings` (room/global field/time
    scale, acoustic medium properties). Registered per object slot
    (`object.<n>.<key>`, 0-based, one entry per possible slot whether
    active or not -- mirrors `TrajectoryEngine`'s own fixed-size object
    pool) plus once more as the dynamic `selectedObject.<key>` variant;
    `Vec3` fields (position/orientation-type fields) register as three
    separate `.x`/`.y`/`.z` float parameters, same split
    `Vec3RowComponent` already uses in the UI. Deliberately excluded:
    pure runtime physics STATE (position, velocity, `orbitPhase`,
    `attractionPulsePhase`, `orbitRadiusNoiseSmoothed`,
    `slingshotTargetId`/`Strength`) and every enum-valued field (`Mode`,
    `DirectivityPattern`, `GrainWindowShape`, `GrainMovementMode`,
    `GrainReadDepthDistribution`, `BoundaryBehavior`) -- a single float
    range doesn't naturally fit a fixed choice of N discrete options;
    left for a later, purpose-built discrete-parameter kind if ever
    needed, not forced into this one.
  - **Moved "which object is selected" from editor-only state into the
    processor** (`KlangorbitProcessor::getSelectedObjectIndex()`/
    `setSelectedObjectIndex()`) -- required for `Scope::SelectedObject`
    to mean anything without an editor window open, since this concept
    previously existed only as `KlangorbitEditor::selectedObjectIndex`.
    The editor still keeps its own copy for rendering (selection
    highlight, panel enablement) but now forwards every change to the
    processor too; the processor's copy is the actual source of truth.
  - Found while reading the existing architecture for this branch, NOT
    fixed here (out of scope, flagged for the upcoming gamepad-driver
    branch instead): `TrajectoryEngine::update()` -- the actual physics
    integration -- is currently only ever called from
    `PluginEditor::timerCallback()` at 90Hz, meaning the whole
    simulation freezes if the editor window closes. A gamepad driver
    that's meant to keep working with the window closed will need this
    resolved first (most likely: a processor-owned timer replacing the
    editor's, not folding physics into `processBlock()` itself).
  - New `Tools/verify_parameter_registry` (register/find, `normalize()`/
    `denormalize()` for both polarities including clamping and an
    asymmetric bipolar range, `SpecificObject` independence between
    slots, `SelectedObject`'s dynamic retargeting including the
    nothing-selected inert case, registration-order preservation) --
    tests `ParameterRegistry` itself in isolation with synthetic
    descriptors, since constructing a full `juce::AudioProcessor`
    outside a host/message-thread context isn't how any other
    `Tools/verify_*` in this project works; the actual
    `buildParameterRegistry()` registration is instead exercised by the
    full plugin build + a Debug-build standalone launch (catches an ID
    collision via `jassert`, compiled out in Release) as part of this
    change's own verification.
- **Octophonic + Circular Array output formats.** Two more decoder modes,
  added to the internal decoder below, both for regular circular
  loudspeaker arrays: `AmbisonicsDecoder::Mode::Octophonic` (fixed,
  8 channels, 45 degree spacing -- a named, directly selectable preset,
  equal in standing to Quad/5.1/7.1) and `Mode::CircularArray` (generic,
  `numSpeakers` 4..24, evenly spaced on 360/numSpeakers degrees -- for
  the many circular-array sizes that have no established naming
  convention at all). Deliberately kept as two separate, clearly
  distinguished entries in the Output Format list (not one control with
  a "how many speakers" afterthought) -- Octophonic is a real, named
  layout people expect to just pick, Circular Array exists for
  everything else.
  - **Angle convention (Octophonic):** +-22.5/+-67.5/+-112.5/+-157.5 deg
    -- a symmetric front L/R pair straddling 0 deg, not a single speaker
    at dead-front or dead-rear. No single canonical IEM/AllRAD-published
    octagon example was found during research (IEM's own
    AllRADecoder/configuration-file docs don't ship one, despite AllRAD
    itself originating there) -- this convention instead matches Blue
    Ripple Sound's "O3A Decoder - Octagon" (a dedicated, Ambisonics/
    B-format-native decoder product), and mirrors this project's own
    existing pattern for every other layout with a front pair (`Quad`,
    5.1, 7.1, Stereo above all straddle front symmetrically too).
    Channel order is this project's own choice (four L/R pairs sweeping
    front-to-back, matching `quad()`'s/`surround5point1()`'s own
    ordering idiom) -- Blue Ripple's specific channel numbering wasn't
    reproduced, since there's no JUCE-named bus here to match against
    anyway (`Octophonic` uses `discreteChannels(8)`, same reasoning as
    the raw Ambisonics modes). Circular Array instead starts its sweep
    at 0 deg/front (channel 0 = front) -- the simplest, most predictable
    default for an arbitrary N with no inherent "front stage" hierarchy
    to preserve.
  - **Decode method: mode-matching, NOT AllRAD.** A regular, evenly-spaced
    array has no irregularity for AllRAD's virtual-array-plus-VBAP-remap
    machinery to correct for -- that machinery exists specifically to
    avoid coloration on IRREGULAR arrays (see the existing AllRAD entry
    below). A plain SH-sampling decode straight to the real (already
    regular) speakers is standard practice here, and is what
    `AmbisonicsDecoder::buildCircularMatrix()` does (the same approach
    `buildStereoMatrix()` already used for the 2-speaker case,
    generalized to N) -- simpler, and at least as accurate, than routing
    through AllRAD's extra machinery for a case it wasn't designed to
    solve. Alias-free reconstruction for the fixed internal order-3
    encode needs >= 7 speakers (standard circular-harmonic-sampling
    result: an N-speaker ring exactly represents orders up to
    floor((N-1)/2)) -- Octophonic (8) clears that; Circular Array with
    fewer than 7 speakers still decodes correctly overall, just with
    more spatial blur than a wider array would give for the same
    order-3 source content, an inherent property of a small ring, not a
    decoder bug (documented in the UI hint and README).
  - **Horizontal-only**, like every other non-Atmos layout in this
    decoder: a circular array of speakers cannot reproduce
    elevation/height at all, regardless of decoder quality -- a property
    of the array TYPE, called out explicitly in the UI/README so it
    doesn't read as a bug.
  - **Bus layout:** both use `discreteChannels()` (no JUCE-named
    "circular array" layout exists). `CircularArray`'s channel count
    varies with `numSpeakers`, so `isBusesLayoutSupported()` now accepts
    any `discreteChannels(n)` for `n` in `[minCircularSpeakers,
    maxCircularSpeakers]` (4..24) when that mode is selected, not just
    one fixed layout per mode like every other entry -- the one real
    tradeoff of a genuinely variable-channel-count mode. This range
    includes 8, which is ALSO `Octophonic`'s own fixed channel count: an
    accepted, documented overlap (a host can't tell which of the two an
    8-channel bus "means" from the layout alone -- the plugin's own Mode
    state, set via the Output Format control, is what actually decides
    behavior). New "Circular Array: Speaker Count" control in the Output
    category (always visible, only has an effect while Circular Array is
    the active format -- same "control is a no-op outside the right
    mode, said so rather than hidden" pattern as the Orbit category's
    controls).
  - `Tools/verify_ambisonics_decoder` gained metadata coverage for both
    new modes (including `CircularArray`'s channel count/bus tracking
    `numSpeakers`, with clamping at both ends of the 4..24 range) and
    directional decode tests (Octophonic: a source at the front-left
    speaker's own direction comes out strongest on that single channel;
    CircularArray: same check at 6 speakers, plus a full 4..24 sweep
    confirming every supported count decodes to a reasonable overall
    level).
- **Internal Ambisonics decoder: Stereo/Quad/5.1/7.1/Atmos-bed output
  formats.** The plugin's output was previously always raw Ambisonics
  B-format only. New `AmbisonicsDecoder` module sits between the encoder
  and the output buffer, offering 11 mutually exclusive output formats
  (new "Output" parameter category, `Output Format` dropdown): raw
  Ambisonics (order 1/2/3, unchanged, zero added overhead -- see below),
  Stereo, Quad, 5.1, 7.1, and four Dolby-Atmos-bed layouts (5.1.2, 5.1.4,
  7.1.2, 7.1.4). Deliberately NOT a general "decode to any speaker array"
  system -- only these fixed target formats (`SpeakerLayouts.h`).
  Binaural/HRTF is intentionally NOT included yet (see below).
  - **Bus architecture: one distinct output bus layout per mode**, not a
    single fixed wide bus with unused channels padded silent. Each mode
    declares its own `juce::AudioChannelSet` (named layouts --
    `quadraphonic()`, `create5point1()`, `create5point1point4()`, etc. --
    so hosts that understand named layouts show a sensible label, not
    just a channel count) via `AmbisonicsDecoder::outputChannelSetFor()`.
    Raw modes keep the plugin's pre-existing `discreteChannels(N)`
    declaration exactly, so existing Reaper/Max-MSP routing and saved
    sessions built around the old raw-B-format bus are unaffected.
    Trade-off, chosen deliberately over the safer fixed-bus alternative:
    switching modes is host-dependent to take effect live.
    `KlangorbitProcessor::setDecoderMode()` calls `setBusesLayout()` and
    `updateHostDisplay()` best-effort, but this JUCE version's VST3
    wrapper has no dedicated "rescan my bus layout" restart flag a
    plugin can raise on its own -- confirmed working live in Reaper;
    other hosts may need the plugin removed and reinserted, or the
    project reloaded, to pick up a new mode's channel count.
  - **Decode method: AllRAD** (All-Round Ambisonic Decoding, Zotter &
    Frank 2012) for every speaker-layout mode -- decode to a large,
    densely/uniformly distributed VIRTUAL loudspeaker array (a Fibonacci
    sphere lattice, 50 points; plain SH sampling decode reusing
    `AmbisonicsEncoder::computeShCoefficients()`, not reimplemented),
    then remap that virtual array onto the real sparse/irregular target
    layout via VBAP (new `VBAP.h`/`.cpp` -- brute-force O(n^3)
    convex-hull triangulation for layouts with height speakers, a
    simpler 2D azimuth-pairwise fallback for horizontal-only layouts,
    since a true 3D hull of coplanar points is degenerate). Stereo uses
    a plain 2-point SH decode directly instead (two symmetric points
    don't need AllRAD's machinery, and it is NOT the same code path as
    Binaural -- no HRTF involved at all, see below).
    - Two correctness fixes found and fixed while building this,
      documented in `AmbisonicsDecoder.cpp` since they're not obvious
      from AllRAD's usual textbook description: (1) **max-rE weighting**
      (Daniel 2003) is required, not optional polish, on the
      virtual-array decode step -- without it, a raw/un-windowed
      order-limited SH decode's many small-but-numerous sidelobes swamp
      the true on-axis peak once summed through the VBAP remap (a
      straight-ahead 5.1 test source came out nearly EQUAL across all
      five channels without this weighting). (2) **per-speaker density
      compensation**: the ACN-0 (W/omni) coefficient is
      direction-independent, so its contribution is proportional to how
      much of the virtual array's surface each real speaker's VBAP
      region covers -- uneven for an irregular layout (5.1's C is
      flanked closely by L/R on both sides, so it structurally covers a
      much narrower azimuth span than L or R do), which otherwise biases
      every decode toward whichever speakers happen to have wider
      coverage, independent of the actual source direction. Fixed by
      normalizing each real speaker's decode row by its own omni
      response before the final loudness calibration.
    - Overall matrix loudness is calibrated pragmatically, not via a
      strict SAD-theory-derived normalization constant: test-encode
      plane waves from many directions, measure this matrix's average
      output energy, scale the whole matrix so that average is
      approximately unit energy (`calibrateDecodeMatrix()`) -- consistent
      with this project's existing "practical approximation, documented
      as such" philosophy (e.g. `PropagationProcessor`'s simplified air
      absorption model).
  - **LFE: silent by default, optional bass management.** Ambisonics
    carries no dedicated LFE signal, so a synthesized one is a real,
    audible addition to what was mixed -- opt-in
    (`AmbisonicsDecoder::setBassManagementEnabled()`, "Bass Management
    (LFE from W)" toggle in the Output category), not silently assumed.
    When enabled, the LFE channel (modes 5.1/7.1/Atmos variants) carries
    a one-pole low-pass (~120Hz, standard subwoofer crossover) of the W
    (omnidirectional) Ambisonics channel -- the closest available proxy
    for "the overall signal," since there is no real LFE bus to derive
    one from. Quad and Stereo have no LFE channel at all.
  - Angle sources, documented in `SpeakerLayouts.h`: ear-level speaker
    angles (L/R/C/surrounds) from ITU-R BS.775-4; height/top-channel
    angles for the Atmos-bed variants from ITU-R BS.2051-2, which
    specifies these as permitted ANGLE SECTORS rather than single fixed
    values -- the concrete angles chosen (top-front +-45 deg az/+45 deg
    el, top-rear +-135 deg az/+45 deg el) sit within those sectors and
    were cross-checked against Dolby's commonly published consumer
    height-speaker placement guidance.
  - Encoding stays at the fixed internal maximum (order 3) for every
    decoded mode regardless of target speaker count, for the best
    available spatial detail going into the decode; raw passthrough
    modes still encode at their own selected order (1/2/3) exactly as
    before. Raw modes bypass `AmbisonicsDecoder` entirely and encode
    straight into the output buffer -- not just an identity decode
    matrix, an actual skip -- so they have zero added overhead versus
    before this feature existed.
  - New `Tools/verify_ambisonics_decoder` (metadata consistency across
    all 11 modes; raw passthrough's `decode()` is a safe no-op; Stereo
    and AllRAD directional correctness for Quad/5.1/5.1.4; LFE
    silent-by-default and bass-management behavior; overall calibrated
    output level stays in a sane range across several source
    directions) and `Tools/verify_vbap` (triangulation/gain correctness
    for both the horizontal-pairwise and 3D-hull cases).
  - Binaural/HRTF deliberately deferred to a follow-up: chosen approach
    is bundling SADIE II's KU100 dummy-head SOFA measurement (Apache
    2.0 licensed, University of York) as the default dataset, plus
    user-supplied SOFA file import (same established pattern as IEM
    Plugin Suite's own BinauralDecoder) -- not implemented in this pass,
    which needs its own SOFA parsing and partitioned-convolution engine.
- **Orbit-mode usage hint.** The Orbit parameter category now shows an
  inline reminder ("Set the Object's Mode to \"Orbit\" ... these
  settings have no effect otherwise") whenever the edited object's Mode
  isn't actually Orbit -- every control in that category was already
  correctly inert until then (`TrajectoryEngine::integrate()` only
  reads them in its Orbit case), but nothing in the UI said so,
  reported as confusing. `ParameterPanel::orbitModeHintLabel`, kept in
  sync via `updateOrbitModeHintVisibility()` from category switches,
  selection changes, preset loads, and live Mode changes.
- **Orbit Radius Noise Smoothing** (`SoundObject::orbitRadiusNoiseSmoothing`,
  seconds, 0 = off/unchanged default behavior): low-pass-filters the
  raw per-tick Gaussian noise sample itself (one-pole, same
  `exp(-dt/tau)` idiom as `PropagationProcessor`'s `dopplerSmoothing`)
  before it's scaled into the mean-reverting orbit radius update,
  turning a jagged random walk into a smoother, more "breathing"
  motion. New runtime field `orbitRadiusNoiseSmoothed` holds the
  filter's own state (reset on preset load, like `orbitPhase`).
  `Tools/verify_orbit` gained a test comparing per-tick radius-delta
  std-dev with smoothing off vs. on (0.099 -> 0.004 in the test's
  numbers -- confirms the filter measurably smooths without freezing
  the radius entirely).
- **Grains: Orbit Around Parent, 2D -> 3D sphere spread**
  (`GrainCloudSettings::orbitSphereSpread`, 0..1). 0 (default)
  reproduces the original behavior exactly: every grain circles in the
  same flat horizontal plane. Raising it blends each grain's own orbit
  PLANE normal (picked once at spawn, `Grain::orbitPlaneNormal`) from
  `{0,0,1}` toward a uniformly random unit vector, so at 1.0 each
  grain's plane is essentially random -- over many grains and full
  rotations the swept shape approaches a sphere instead of a disc.
  `GrainCloud::orbitPlaneOffset()` builds an orthonormal basis for
  whatever plane a given normal defines; its seed vector was
  deliberately chosen so `normal={0,0,1}` reproduces the exact
  original `cos(phase), sin(phase), 0` formula (not just an
  equivalent-shape reparametrization) -- verified by
  `Tools/verify_grain_cloud`'s `testOrbitSphereSpread`, which checks
  spread=0 keeps every grain's `z` at exactly 0 and spread=1 produces
  real out-of-plane movement while the orbit's actual radius stays
  unchanged.
- **Voice stealing for grain spawning + per-parameter grain jitter.**
  Follow-up to the "grainRate/grainDuration independence" investigation
  above: that entry concluded the scheduling was already correct, but
  once `grainRate * grainDuration` genuinely exceeds
  `maxConcurrentGrains` (a real, unavoidable CPU ceiling -- see the
  earlier entry), the *original* design simply stalled new spawns until
  an old grain's full `grainDuration` elapsed, which is audibly
  stuttery for anyone deliberately using a long Duration at a high
  Rate, not just an edge case. Fixed by design change, not a bug in the
  old scheduling:
  - `GrainCloud::update()` now voice-steals once `maxConcurrentGrains`
    is hit: the OLDEST active grain gets a short (10ms) forced
    fade-out (`GrainCloud::beginVoiceSteal()`) and its slot is reused
    as soon as that completes, instead of waiting out the grain's full
    remaining `grainDuration`. Reuses the exact same
    guaranteed-exact-zero Hann envelope ending every grain's natural
    end already relies on (the fix from the earlier grain-click bug in
    this changelog) by simply shortening `lifetimeSeconds`/
    `grainLengthSamples` -- no new audio-thread code, no discontinuity
    risk. Trade-off, by design: `grainRate`'s spawn schedule is now
    honored continuously regardless of `grainDuration`, but individual
    grains can end up shorter than configured once oversubscribed
    (confirmed acceptable -- the alternative, a hard guarantee that
    every grain always plays its full configured length, cannot avoid
    stalling spawns at some rate*duration combination, since some
    concurrency ceiling is unavoidable for real-time CPU safety).
    The shared, scene-wide `globalGrainBudget` (across every object's
    cloud) is deliberately NOT covered by stealing -- only a cloud's
    own `maxConcurrentGrains` is -- since stealing within one cloud
    can't create more of that shared budget.
  - `maxConcurrentGrains`'s default raised 8 -> 32, so common
    `grainRate`/`grainDuration` combinations don't need voice stealing
    at all in practice.
  - **Per-parameter jitter**, replacing the old single, mutually-
    exclusive `jitterTarget`/`jitterRange` pair (only one field could
    be randomized at a time): five new dedicated `GrainCloudSettings`
    fields -- `grainRateJitter`, `grainDurationJitter`,
    `boundaryRadiusJitter`, `initialSpeedJitter`, `orbitRadiusJitter`
    -- each shown directly under its own parameter in the Grains panel
    (same `applyJitter()` +/- fractional shape as the pre-existing
    `pitchJitter`), all usable simultaneously. `grainRateJitter`
    specifically randomizes the spawn *interval* itself, recomputed
    fresh for every spawn attempt, not a per-grain property.
  - Preset compatibility: old presets that saved the legacy
    `jitterTarget`/`jitterRange` pair are migrated onto the matching
    new field on load (`PresetManager::grainCloudSettingsFromVar()`);
    saving (`grainCloudSettingsToVar()`) only ever writes the new
    fields from now on, so re-saving upgrades a preset automatically.
    No schemaVersion bump -- purely additive/optional, same policy as
    every other schemaVersion-2 `grainCloud` field.
  - `Tools/verify_grain_cloud` gained four new tests:
    `testVoiceStealingKeepsSpawningContinuous` (heavily oversubscribed
    rate*duration vs. cap, confirms ~continuous spawn cadence instead
    of stalling), `testVoiceStealingShortensNotCorrupts` (stolen
    grains' `grainLengthSamples` always stays positive/bounded, and
    measurably shorter than configured), `testGrainRateJitterVariesInterval`
    (spawn-interval stddev goes from exactly 0 to clearly nonzero with
    jitter on), and `testGrainDurationJitterVariesLifetime` (spawned
    grains' lengths spread across a range instead of one fixed value).
    One pre-existing test (`testRadialExplosionMovesOutward`) had to be
    adjusted -- its `grainRate=1000`/`maxConcurrentGrains=1` setup
    created a large same-call spawn backlog that voice stealing now
    correctly (if unhelpfully for that specific test) resolves by
    immediately stealing the grain it had just spawned; lowered to a
    rate that produces exactly one spawn per test tick instead, which
    is what the test actually needs to isolate.
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
- **Object list's add/remove buttons unified to bare "+"/"-", side by
  side in one row, with an explicit spacer before the object row list.**
  Follow-up to the toolbar-relocation entry below: "+ Object"/
  "- Remove Object" were two differently-phrased labels for what's
  really one symmetric pair of actions, and didn't fit side by side at
  their old lengths (hence the earlier stacked layout) -- bare symbols
  read as a matched pair and fit comfortably in one row in this panel's
  narrow width. `ObjectListPanel::addRemoveToListGap` (new, 14px) adds
  visible breathing room between that row and the object rows below it,
  instead of the list starting right under the buttons.
- **Toolbar simplified to a single row; "+ Object"/"- Remove Object" and
  the "Objects: N / M" count moved to the object list sidebar.**
  `KlangorbitEditor::toolbarHeight` reduced from 76 (two rows) to 38 (one)
  now that so much of the old second row moved out: `ObjectListPanel`
  (`Source/ObjectListPanel.h/.cpp`) now owns `addButton`/`removeButton`
  and shows the count in its own header label (repurposed from a static
  "Objects" title to "Objects: N / M", updated by `refresh()`) --
  `onAddClicked`/`onRemoveClicked` callbacks wire straight to
  `KlangorbitEditor`'s existing `addObjectClicked()`/
  `removeObjectClicked()`, same pattern as the panel's own
  `onObjectSelected`. `updateButtonStates()` (new, called from both
  `refresh()` and `setSelectedIndex()`) keeps both buttons' enabled state
  current; `KlangorbitEditor::updateObjectUiState()` is gone entirely --
  every one of its effects is now handled by `ObjectListPanel` itself at
  the same call sites that already existed. The now-single toolbar row
  holds Load/Save Preset on the left, and the CPU meter (moved here from
  the old second row) / Mappings.../Output.../"?" on the right.
- **Acoustics split back out into its own category, and Propagation Wind
  moved there from Scene.** Follow-up to both changes below: a single
  flat Scene page mixing room/force-field settings with acoustic-medium
  settings read as more cluttered than two separate tabs, so Acoustics
  (`Category::Acoustics`, speed of sound/temperature/humidity/pressure)
  is back as its own category button -- still grouped under "SCENE
  SETTINGS" alongside Scene (both are still `Scope::Global`, not
  per-object, which is why they stay in the same button GROUP even
  though they're separate TABS again). Propagation Wind
  (`SceneSettings::windVector`) moves from Scene into Acoustics
  specifically, alongside the rest of the acoustic-medium settings it
  conceptually belongs with -- completing the separation from Force
  Field (`SceneSettings::globalField`, stays in Scene) that the rename
  above started.
- **Parameter panel polish, following up on the reorganization below.**
  - `objectHeaderLabel` (which object is selected) now shares the
    "OBJECT SETTINGS" label's own row, right-aligned, instead of
    costing a row of its own below the button group -- less wasted
    vertical space.
  - "Mappings.../Output..." moved from the toolbar's second row to its
    first, next to "?" -- all three open their own OS-level window, so
    now they're grouped together for that reason.
  - Scene category: **Show Boundary** moved above **Boundary Size**
    (was "Room Size", see below) -- "do I even see this?" is the more
    natural first question before tuning the boundary itself.
  - **"Room Size" renamed to "Boundary Size"** -- it's a physics
    boundary (reflect/wrap/absorb), not an acoustic "room" in any
    sense (no reverb/reflection processing is tied to it). Underlying
    field name (`roomSize`) unchanged -- display-string rename only,
    no preset/schema impact. `ParameterRegistry`'s own display name for
    the same field (id `"roomSize"`) updated to match; the id itself
    (used by saved mapping profiles) is untouched.
  - **"Global Field (Wind/Gravity)" renamed to "Force Field
    (Wind/Gravity)", and "Wind (m/s)" renamed to "Propagation Wind
    (m/s)"** -- these are two functionally distinct scene-wide settings
    that happened to both read as "wind" in their old names: Force
    Field (`SceneSettings::globalField`) is a real physical force that
    pushes moving objects around (Impulse/Attracted modes); Propagation
    Wind (`SceneSettings::windVector`) only shifts the effective speed
    of sound for propagation delay/Doppler and never touches object
    motion at all. Considered moving Propagation Wind into the
    (per-object) Doppler category since it only affects sound -- not
    done, since both fields are scene-wide (`Scope::Global`, not tied
    to any object), and Doppler lives under "OBJECT SETTINGS" (disabled/
    hidden with nothing selected) -- moving a global setting there would
    misrepresent it as object-scoped. Both stayed under "SCENE SETTINGS"
    at this point, renamed only (see below for the follow-up that split
    Acoustics back out as its own category and moved Propagation Wind
    there specifically). `ParameterRegistry`'s own display names (ids
    `"globalField"`/`"windVector"`) updated to match; the ids themselves
    are untouched.
- **Parameter panel reorganized: Scene settings visually separated from
  Object settings, the Acoustics category folded into Scene, and Output
  moved out into its own window entirely.** (This entry describes the
  feature's current, final shape -- it went through an intermediate
  "Scene / Output" grouping first, revised again after trying it, before
  landing here; both changes were unreleased, so this entry replaces
  rather than layers onto the original.) The category-button row
  (`ParameterPanel`) is now a single "SCENE SETTINGS" group (just Scene
  -- Output no longer lives here, see below) above a small section
  label, with "OBJECT SETTINGS" (Object, Attraction, Orbit, Doppler,
  Grains) below it. The grouping reuses `categoryRequiresObject()`'s
  existing boolean partition (already used to disable/force-switch away
  from object-scoped categories with nothing selected) rather than
  tracking group membership as separate state that could drift out of
  sync with it. `objectHeaderLabel` (which object, if any, is selected)
  now sits just below the OBJECT SETTINGS buttons instead of above the
  whole panel -- smaller, regular weight, not the bold section-title
  treatment it used to have -- since selection is only relevant to that
  group's own pages.
  - **Acoustics is no longer its own top-level tab** -- speed of sound,
    temperature, relative humidity, atmospheric pressure, and wind are
    now part of the Scene page itself, under a small "ACOUSTICS" section
    label, alongside room size/global field/time scale. This was purely
    a `ParameterPanel`-side split to begin with: `ParameterRegistry`'s
    own category string for both groups was already the same
    (`"Global"`, see `PluginProcessor.cpp`'s `buildParameterRegistry()`)
    -- both are scene-wide settings, not one object's own property, and
    the panel's own tab boundary is now consistent with that instead of
    drawing a distinction the rest of the architecture never made.
    `ParameterPanel::Category::Acoustics` removed (its rows now register
    under `Category::Scene`); no other code referenced it.
  - **Output Format/Bass Management/Circular Array speaker count are no
    longer a `ParameterPanel` category at all** -- moved to their own
    `OutputWindow` (see the Added entry above): `ParameterPanel::
    Category::Output` removed, along with `decoderModeRow`/
    `circularSpeakerCountRow`/`bassManagementRow`/`setProcessor()`/
    `decoderProcessor` (all now live in `OutputPanel` instead). Plugin-
    wide settings tied to neither the scene nor any object don't fit
    naturally as a tab sharing space with either, the same reasoning
    that already applies to Help/Mappings each having their own window
    rather than being folded into this panel.
  - Docs (`README.md`, `Docs/UserGuide.md`, `Source/HelpContent.h`)
    updated everywhere they pointed at "the Acoustics category" (now
    "the Scene category's Acoustics section") or "the Output category"
    (now "the toolbar's Output... window").
- **Objects are now numbered from 1 instead of 0 everywhere they're
  displayed** -- the object list sidebar, the parameter panel's object
  header, the Orbit Reference Object dropdown, and the sling gesture's
  target label. Purely a display change: internal 0-based indices/ids
  (array indices, `SoundObject::id`, `inputChannel`, preset JSON `id`
  fields, combo-box item IDs) are completely unaffected, only the text
  shown to the user adds 1.
- **"Doppler (uses object's Doppler Factor)" simplified to just
  "Doppler"** and **"Grains Only (Mute Original Audio)" simplified to
  just "Isolate Grains"** in the Grains parameter category -- both
  parenthetical clarifications moved into the Help window/UserGuide
  instead of staying in the on-screen label. No functional change.
- **`Max Concurrent Grains`'s default and range raised again, 32 -> 256**
  (`GrainCloudSettings::maxConcurrentGrains`), matching a corresponding
  raise of the global cap shared across every object's cloud
  (`KlangorbitProcessor::maxConcurrentGrainsGlobal`, 128 -> 256, which
  also raises the per-cloud pool size since it's derived from the same
  constant) -- still the same "rough operation-count estimate, not
  profiled on real hardware" caveat as the earlier 32->128 raise; the
  toolbar's CPU meter remains the way to verify it on your own machine.
- **"Grain Rate" renamed to "Grain Rate (Spawn Rate)" in the Grains
  parameter category** (`README.md`, `Docs/UserGuide.md`, the in-app
  Help window) -- went through two iterations: first renamed outright
  to "Spawn Rate" (reported as unclear/confusable with Grain Duration),
  then revisited once the underlying scheduling was actually redesigned
  (see voice stealing above) and settled on keeping "Grain Rate" as the
  primary name with "(Spawn Rate)" alongside it, rather than dropping
  the original name entirely. The internal field name
  (`GrainCloudSettings::grainRate`, the `grainRate` preset JSON key) is
  deliberately unchanged throughout, same reasoning as the "Grains"
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
- **Reaper (and potentially other hosts) could permanently negotiate the
  plugin's output down to plain Stereo (2ch) regardless of the selected
  Output Format, reported as "no audio" and "only 2 channels" in both
  Reaper and the Standalone app.** `isBusesLayoutSupported()` used to
  accept ALL 13 of `AmbisonicsDecoder::Mode`'s output layouts as valid
  (deliberately, so a host could switch modes via its own native bus-
  negotiation UI, not only this plugin's own "Output..." picker) --
  the real-world consequence: some hosts probe several candidate output
  layouts when a plugin is first inserted and simply settle on whichever
  one gets accepted first (often plain Stereo, a common host-side
  default probe) -- permanently stuck there regardless of which mode the
  decoder itself, and the "Output..." window's own dropdown, actually
  show as selected. The plugin's original single-format (Ambisonics-only)
  version never exhibited this, for a simple reason: it only ever had
  ONE valid layout to negotiate to, so there was no ambiguity for a host
  to resolve incorrectly. Fixed by restricting `isBusesLayoutSupported()`
  to accept only the CURRENTLY ACTIVE decoder mode's own layout (plus
  the full adjustable range specifically while `CircularArray` is
  active, since that one mode's channel count is independently
  adjustable via `setCircularArraySpeakerCount()`) -- restores the
  original "exactly one valid layout at a time" guarantee.
  `setDecoderMode()`'s own in-plugin mode switching still works
  unaffected: it calls `decoder.setMode(newMode)` BEFORE calling
  `setBusesLayout()`, so by the time that call's internal validation
  reaches `isBusesLayoutSupported()`, `decoder.getMode()` already equals
  the new mode being requested -- the layout being requested and the
  "currently active" mode this function now checks against agree. Not
  independently unit-tested (host bus-negotiation behavior isn't
  something a headless test can exercise) -- confirmed fixed by the
  reporting user directly in Reaper (fresh plugin instance after
  rebuilding correctly showed and used all 16 Ambisonics channels).
- **The Mappings window (and, latently, Help) could open behind the
  host's own window when hosted as a VST3, with no way to reach it**
  (reported in Reaper). A newly created `juce::DocumentWindow` from
  inside a plugin process isn't guaranteed to be at the same OS window
  level as the host's own window, so `toFront()` alone -- which only
  reorders within this app's own window stack -- doesn't always
  guarantee it actually surfaces above the host. Fixed two ways,
  belt-and-suspenders: `MappingWindow`/`HelpWindow`/the new
  `OutputWindow` now all call `setAlwaysOnTop (true)` before
  `setVisible (true)` in their constructors (a small reference/settings
  window floating above the host is an accepted tradeoff for exactly
  this class of bug, not just a workaround); and `KlangorbitEditor`'s
  `showMappingClicked()`/`showHelpClicked()`/`showOutputClicked()` each
  now also re-assert `toFront (true)` one message-loop iteration later
  via `juce::MessageManager::callAsync()` (using a `juce::Component::
  SafePointer`, not a raw pointer, since the callback fires on a LATER
  iteration by which point the editor -- and the window it owns -- could
  conceivably have already closed). Not independently verified against a
  real Reaper install in this environment (not available here); the fix
  itself is a well-established pattern for this exact class of JUCE
  plugin bug, applied consistently to all three toolbar windows.
- **The whole simulation froze when the editor window closed --
  physics, panning, grain spawning, all of it.** `TrajectoryEngine::
  update()` and every `GrainCloud::update()` were only ever called from
  `KlangorbitEditor::timerCallback()` (a 90Hz `juce::Timer` owned by the
  editor). JUCE keeps calling `processBlock()` regardless of whether an
  editor exists, so audio never actually stopped -- but with the editor
  closed, objects stopped moving, Doppler/panning stopped updating, and
  no new grains ever spawned, since nothing was left to advance any of
  that state. Found while building the parameter registry (see above)
  and its `Scope::SelectedObject`, which needed a durable, non-editor
  source of truth for "which object is selected" anyway.
  - Fixed by moving timer ownership from `KlangorbitEditor` to
    `KlangorbitProcessor` itself (`private juce::Timer`, started
    unconditionally in the constructor, stopped in the destructor) --
    the exact same two calls (`trajectoryEngine.update(dt)`, then every
    active object's `GrainCloud::update(...)`), at the same 90Hz rate,
    just moved verbatim rather than reimplemented.
    `KlangorbitProcessor::timerCallback()` now runs regardless of
    whether an editor is open, closed, or was ever created at all --
    Standalone minimized, VST3 window closed in a host, or the plugin
    just sitting loaded on a track. `grainRandom` (the spawn-jitter RNG)
    moved from the editor to the processor along with the loop that
    uses it.
  - The editor keeps its own, separate 90Hz timer for view-only
    concerns that were bundled into the same callback before: trail
    capture, the sling gesture's Ctrl/Alt modifier polling, the
    CPU-load readout, and `repaint()`. It no longer touches
    `TrajectoryEngine`/`GrainCloud` at all, so there is no double-update
    while an editor happens to be open at the same time.
  - No new cross-thread synchronization concern: both timers still run
    on the one, process-wide JUCE message thread (there's only ever
    one), so this only changes which long-lived object schedules the
    callback, not which thread runs it or how
    `SoundObject`/`GrainCloudSettings` fields get read from the audio
    thread (already unsynchronized-by-convention, exactly as before --
    see `PluginProcessor::processBlock()`'s own established pattern).
  - Not independently verified against a real DAW host with the editor
    closed (no interactive host-automation available in this
    environment) -- verified instead by code inspection (confirmed no
    `TrajectoryEngine`/`GrainCloud` update calls remain in
    `PluginEditor.cpp`) plus the full existing test suite and a
    Standalone build/launch stability check, consistent with how this
    class of change is verified elsewhere in this project.
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
