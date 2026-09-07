# Klangorbit

Object-based Ambisonics encoder with a trajectory/physics engine, and an
internal decoder to a selectable output format (toolbar -> "Output..." ->
"Output Format", 14 formats, in dropdown order): Stereo (the default --
audible immediately, no external decoder needed), Binaural (HRTF-based
headphone output, with a choice of bundled KEMAR/SADIE II datasets or a
custom SOFA file -- see "Binaural (HRTF) output" below), Quad, Octophonic
(a fixed, named 8-speaker circular array), Circular Array (a generic
circular array, adjustable `numSpeakers` 4-24, for array sizes that have
no established naming convention), 5.1, 7.1, one of four Dolby-Atmos-bed
layouts (5.1.2/5.1.4/7.1.2/7.1.4), or raw Ambisonics B-format at one of
three orders -- FOA/SOA/TOA (1st/2nd/3rd Order Ambisonics, 4/9/16ch,
ACN/SN3D, AmbiX-compatible), for further processing in SPARTA
(AmbiBIN/AmbiDEC) or the IEM Plugin Suite. Not a general "decode to any
speaker array" tool -- only these fixed target formats. See
`AmbisonicsDecoder.h` and the CHANGELOG entry for the decode method
(AllRAD for irregular layouts, plain mode-matching for the regular
circular ones, HRTF convolution for Binaural), and "Output formats:
channel layouts and standards" below for the exact speaker angles/channel
order and which layouts follow ITU-R BS.775-4/BS.2051-2. Octophonic and
Circular Array are horizontal-only -- a circular array of speakers cannot
reproduce elevation/height at all, a property of the array type, not a
decoder limitation.

The whole simulation -- physics, panning, grain spawning, everything at
control rate -- runs from a timer owned by `KlangorbitProcessor` itself,
independent of whether an editor window is open. Closing the editor
(Standalone minimized, VST3 window closed in a host, or the plugin just
loaded on a track with no editor ever opened) does not pause anything;
only audio callbacks pausing (a host bypassing/disabling the track) would.

A connected gamepad's left stick rate-controls the selected object's
movement (see "Gamepad control" below), and any other control -- gamepad,
MIDI CC/Note/Pitch Bend, or OSC (see "Controller mapping" below) -- can be
bound to any parameter via the same Learn mode, built on a
protocol-neutral controller-mapping architecture (`ParameterRegistry` +
`CanonicalInputHub` + `MappingEngine`, see the CHANGELOG): the registry,
the canonical layer, Learn mode, and mapping profiles are all
protocol-neutral, and `MidiDriver`/`OscDriver` (`Source/MidiDriver.h/.cpp`,
`Source/OscDriver.h/.cpp`) needed zero changes to any of them to plug in
as two more thin drivers alongside `GamepadDriver`.

## Signal flow

```
Live input (up to 8 mono channels)
        |
        +-------------------------------------------------+
        v                                                   v
[SoundObject 0..7]  <-- position/motion            [ring buffer per object]
   from TrajectoryEngine                             (continuously filled,
   (control rate, ~90 Hz, driven by                   see GrainCloud below)
    KlangorbitProcessor's own timer,
    not the editor's -- see below)
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
                                       recursion). Order 1-3 when the output
                                       format IS raw Ambisonics; fixed at
                                       order 3 (16ch) for every OTHER format,
                                       regardless of its own final channel
                                       count. Each SoundObject and each
                                       active grain is encoded as its own
                                       mono source with its own ramped gains.
                                            |
                                            v
                              Ambisonics B-format (4/9/16 channels)
                                            |
                                            v
                          Output Format == raw Ambisonics?
                             |                        |
                            yes                        no
                             |                        v
                             |            [AmbisonicsDecoder]
                             |               AllRAD (virtual dense array +
                             |               VBAP remap) for IRREGULAR
                             |               speaker layouts (Quad/5.1/7.1/
                             |               Atmos), plain mode-matching SH
                             |               decode straight to the real
                             |               speakers for Stereo AND for
                             |               REGULAR circular arrays
                             |               (Octophonic/Circular Array --
                             |               no irregularity for AllRAD to
                             |               correct for there). Built once
                             |               per mode switch, not per block.
                             |                        |
                             v                        v
                     plugin output bus, channel count/layout per the
                     selected Output Format (see AmbisonicsDecoder::
                     outputChannelSetFor()) -- raw Ambisonics modes bypass
                     AmbisonicsDecoder entirely (zero added overhead)
                                            |
                                            v
        DAW / Max/MSP / SPARTA / IEM Suite, or straight to speakers/headphones
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

Result: `Klangorbit.vst3`, `Klangorbit.component` (AU, macOS only), and the
standalone app in the build directory (`Klangorbit_artefacts/`). Both the
VST3 and the AU component are automatically copied to their system-wide
plugin folders (`COPY_PLUGIN_AFTER_BUILD TRUE`, `VST3_COPY_DIR`/
`AU_COPY_DIR` overridden to `/Library/Audio/Plug-Ins/VST3`/
`/Library/Audio/Plug-Ins/Components` -- all users, root of the boot
volume, instead of JUCE's own per-user defaults) -- but unlike the VST3
folder, the system-wide Components folder is root-owned and NOT
world-writable by default on a fresh macOS install, so it needs a
one-time manual step before the AU install (the copy step below) can
succeed without sudo:
```bash
sudo chmod 777 "/Library/Audio/Plug-Ins/Components"
```
(Same reasoning as the VST3 folder already being world-writable on this
particular Mac -- a one-time local setup step, not something the CMake
build does on its own.) Logic Pro/GarageBand/other AU hosts also scan the
per-user `~/Library/Audio/Plug-Ins/Components/`, but this project
installs system-wide only, for consistency with VST3.

AU format: classic AU v2 (Component Manager, `.component` bundle) --
deliberately NOT AUv3 (a different, app-extension-based packaging model,
unneeded for a Mac-only, non-sandboxed plugin like this one). Registers as
a plain "Effect" (`kAudioUnitType_Effect`, 4-char type `aufx`) -- NOT
"Music Effect" (`kAudioUnitType_MusicEffect`/`aumf`), which is what
`NEEDS_MIDI_INPUT TRUE` + `IS_SYNTH FALSE` makes JUCE infer by default
(an audio effect that also accepts MIDI, matching the Mappings/MIDI-CC
control surface -- see "Controller mapping"/"MIDI / OSC control" below).
Overridden to `aufx` deliberately, in `CMakeLists.txt` (`AU_MAIN_TYPE`),
after empirically confirming with a throwaway diagnostic build that Logic
Pro's Plugin Manager listed the AU as installed/compatible while `aumf`,
but never actually offered it as insertable on ANY track/bus type --
switching to `aufx` (otherwise identical) was the one change that made it
appear, confirmed directly by the user. Real, currently-open tradeoff:
`auval` flags `aufx` + implementing MIDI handling as a warning (our
MIDI-handling code is still built in, from `NEEDS_MIDI_INPUT`, but the
type tells a host this isn't a MIDI-interested unit) -- whether Logic
actually still routes MIDI (Mappings/Learn's CC input) to an `aufx`-typed
AU has not yet been confirmed either way by an in-Logic test. Gamepad/OSC
control are unaffected regardless of this choice. Validated with Apple's
own `auval` tool after every build touching the AU target:
```bash
auval -v aufx Klor Jgck
```
(`Klor`/`Jgck` are this plugin's `PLUGIN_CODE`/`PLUGIN_MANUFACTURER_CODE`
from `CMakeLists.txt`.) `auval` isn't run automatically as part of the
CMake build -- run it manually after building/reinstalling the AU, or
whenever `Klangorbit_AU` changes.

AU bus flexibility (input AND output): unlike VST3/Standalone (still a
fixed 8-channel discrete "Live Inputs" bus, matching the existing Reaper
workflow), the AU build's input bus accepts Mono, Stereo, Quad, or 7.1
(1/2/4/8 channels) -- whichever matches the Logic track/bus you insert
Klangorbit on, negotiated automatically by Logic itself, no in-plugin
control. This exists because Logic filters which tracks a plugin can even
be inserted on by channel format, and the plugin's original fixed
8-discrete-channel bus matched none of Logic's standard track types -- see
the CHANGELOG entry for the full diagnosis (via JUCE's own AU wrapper
source). Objects beyond the currently negotiated input channel count
simply have no live audio (same mechanism already used for any inactive
object). One practical consequence for Logic specifically: gravity/
attraction between objects in DIFFERENT Klangorbit instances (e.g. one
instance per Logic track) doesn't work -- each instance's own physics
simulation is completely independent, with no cross-instance
communication -- so a multi-object scene with real inter-object
interaction still needs a single instance fed by a wide enough live-input
bus, same as the existing Reaper workflow.

The OUTPUT side is flexible too, but works differently: Logic fixes the
output channel COUNT once, at insertion (based on which track/bus type
was chosen), and Klangorbit never asks to change it afterward while
running as AU. Of the 14 Output Formats, only the 9 with both a NAMED
channel layout and `<= 12` channels (Logic's own ceiling, 7.1.4) are ever
usable in Logic: Stereo, Binaural (HRTF), Quad, 5.1, 7.1, and the four
Atmos-bed formats. The Output window's "Output Format" dropdown greys out
(`ComboBox::setItemEnabled`) anything needing more channels than what
Logic actually negotiated -- switching among the remaining, AVAILABLE
formats works live, using fewer channels internally rather than
requesting a different bus. FOA/SOA/TOA (raw Ambisonics), Octophonic, and
Circular Array are never available in AU at all (unnamed channel sets
Logic's own layout-tag matching can't recognize, and TOA alone already
exceeds the 12-channel ceiling) -- see `KlangorbitProcessor::
isOutputModeAvailable()` and the CHANGELOG entry for the full reasoning,
including why this design was chosen specifically to avoid repeating the
Reaper bus-negotiation regression noted above.

Every format -- AU, VST3, and Standalone alike -- now starts up in Stereo
output rather than raw Ambisonics B-format (see the CHANGELOG entry): a
real behavior change for existing Reaper/Standalone sessions built around
the old raw-Ambisonics-by-default startup state. Switch Output Format
back to an Ambisonics order manually if that's what you need; it's still
fully supported, just no longer the default.

App/plugin icon and vendor name: `Assets/AppIcon.png` (1024x1024, source
vector at `Assets/AppIcon.svg`) is baked into a proper `.icns` for the
Standalone `.app`, VST3, and AU bundles at build time by JUCE's own icon
tooling (`ICON_BIG`/`ICON_SMALL` in `CMakeLists.txt`) -- the `.icns` itself
isn't checked in, it's regenerated every build. `COMPANY_NAME "Jan Glueck"`
in the same `juce_add_plugin()` call is what a host like Reaper/Logic shows
as the plugin's vendor/manufacturer (in the VST3's `moduleinfo.json`/the
AU's "Manufacturer String").

## Testing with Reaper + SPARTA/IEM

1. Start the plugin/standalone app, connect a live input (microphone or
   audio interface channel) to Input 0.
2. The default Output Format is Stereo (audible immediately) -- switch to
   TOA (3rd Order Ambisonics, 16ch, toolbar -> "Output...") to route the
   output to a bus with AmbiBIN (SPARTA) or the IEM BinauralDecoder
   instead. Every other format (Binaural/Quad/Octophonic/Circular
   Array/5.1/7.1/an Atmos-bed variant) sends already-decoded audio
   straight to that many channels, no external decoder plugin needed --
   Binaural (HRTF) specifically does its own HRTF convolution in-plugin
   (see "Binaural (HRTF) output" below), no external
   AmbiBIN/BinauralDecoder needed for that path -- but changing modes live
   may need the plugin removed and reinserted in some hosts, see the
   CHANGELOG.
3. Drag object 0 in the scene view with the mouse -> the position change
   should show up as a change in direction in the binaural playback.
4. Double-clicking an object starts an orbit motion around the origin
   (demo for the trajectory mode).
5. Drag an object quickly and release -> throw gesture, the object keeps
   moving freely afterward and is slowed down by `damping`/`dragCoefficient`,
   and reflected/wrapped/absorbed at the room boundary (`roomSize`).
6. Clicking an object (without dragging) selects it -- its parameters
   appear in the panel on the right. "+ Object" (object list, left side)
   activates the next free object (only object 0 is active at startup),
   "- Remove Object" deactivates the selected one.
7. Shift+drag an object -> "sling" launch gesture: pull it away from its
   position like a catapult (a bow line follows the cursor, plus a dashed
   preview showing the actual upcoming result -- see below) and release
   to fire it in the opposite direction. Try it a few times to compare
   with the plain throw gesture (4) -- the sling's launch speed is
   proportional to how far you pulled, not to how fast you moved the
   mouse. While pulling, tap Ctrl (repeatedly) to cycle through three
   launch modes: Free Throw (cyan bow line, default), Orbit Shot (violet
   -- a scripted ellipse/circle), and Slingshot (yellow-green -- a REAL
   gravity-based deflection around another object, not scripted). With
   Orbit Shot or Slingshot selected, tap Alt to step through
   circular/elliptical orbit shapes (Orbit Shot only) and tap Tab
   (repeatedly) to cycle which other active object the shot targets and
   back to the world origin ("Center") -- see "Sling launch gesture"
   below for what each mode's target selection actually does. The dashed
   preview updates live to match. Releasing far enough from the object
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

## Output formats: channel layouts and standards

Full speaker angles, channel order, and standards compliance for every
Output Format, in dropdown order. Verified directly against
`Source/SpeakerLayouts.h` (the source of truth for every layout below,
including its own citations) -- not summarized from memory.

- **Stereo** -- L/R at +-30 deg. ITU-R BS.775-4 (the same standard
  defining 5.1/7.1 below).
- **Binaural (HRTF)** -- not a fixed loudspeaker layout; see "Binaural
  (HRTF) output" below for the full technique.
- **Quad** -- L/R +-45 deg, Ls/Rs +-135 deg. The conventional consumer
  quadraphonic layout -- not an ITU standard.
- **Octophonic** -- fixed 8-speaker circular array: front L/R +-22.5 deg,
  front-side L/R +-67.5 deg, rear-side L/R +-112.5 deg, rear L/R
  +-157.5 deg. No ITU/IEM/AllRAD standard was found for this exact
  layout during research -- matches Blue Ripple Sound's "O3A Decoder --
  Octagon" (the one concrete Ambisonics-ecosystem reference product
  found); the channel order itself is this project's own choice (no
  external ordering convention to preserve, unlike the named JUCE
  layouts below).
- **Circular Array** -- generic N-speaker ring (4-24, adjustable), evenly
  spaced starting at front, channel index == sweep order. No
  standard -- pure geometry, for array sizes with no established naming
  convention.
- **5.1** -- ITU-R BS.775-4: L/R +-30 deg, C 0 deg, Ls/Rs +-110 deg.
  Channel order (matches `juce::AudioChannelSet::create5point1()`
  exactly, verified against JUCE source): L R C LFE Ls Rs.
- **7.1** -- ITU-R BS.775-4: L/R +-30 deg, C 0 deg, side Lss/Rss +-90 deg,
  rear Lrs/Rrs +-135 deg -- both within BS.775-4's own permitted sectors
  (side 90-110 deg, rear 135-150 deg), chosen as the commonly-used
  nominal defaults. Channel order (matches `create7point1()`): L R C LFE
  Lss Rss Lrs Rrs.
- **5.1.2 / 5.1.4 / 7.1.2 / 7.1.4** (the four Dolby-Atmos-bed layouts) --
  ear-level angles as the matching 5.1/7.1 bed above, PLUS height
  channels: top-front +-45 deg azimuth / +45 deg elevation, top-rear
  +-135 deg azimuth / +45 deg elevation, top-side +-90 deg azimuth /
  +45 deg elevation. ITU-R BS.2051-2 ("Advanced sound system for
  programme production") only defines permitted ANGLE SECTORS for these
  (e.g. top-front anywhere in azimuth +-30..45 deg / elevation
  +30..55 deg), not single fixed values -- the angles used here are
  round numbers within those permitted sectors, cross-checked against
  Dolby's own commonly published consumer height-speaker placement
  guidance (45 deg front / 135 deg rear, 45 deg elevation cited as
  "ideal") -- a defensible, documented choice within the standard's
  tolerance, not an invented number, but also not a literal quote of one
  single "the" official angle (the standard doesn't specify one).
  Channel counts/order: 5.1.2 = 5.1 bed + top-side L/R (8ch, matches
  `create5point1point2()`); 5.1.4 = 5.1 bed + top-front L/R + top-rear
  L/R (10ch, `create5point1point4()`); 7.1.2 = 7.1 bed + top-side L/R
  (10ch, `create7point1point2()`); 7.1.4 = 7.1 bed + top-front L/R +
  top-rear L/R (12ch, `create7point1point4()`) -- the largest
  non-Ambisonics format, at Logic Pro's own channel ceiling (see the AU
  section above).
- **FOA / SOA / TOA** (1st/2nd/3rd Order Ambisonics, 4/9/16ch) -- raw
  B-format, no decoding at all: ACN channel ordering, SN3D normalization
  (AmbiX-compatible). Not a loudspeaker layout -- feed an external
  decoder (SPARTA AmbiDEC/AmbiBIN, IEM Plugin Suite) or one of the
  formats above instead.

All named layouts above (every format except Octophonic and Circular
Array, which are deliberately generic/unnamed `discreteChannels()` buses
-- see the CHANGELOG's AU entries for why that distinction matters for
host recognition) use JUCE's own named `AudioChannelSet`s, so the channel
order a host/DAW sees matches what it already expects for that format
name -- not just the angles, the actual channel STREAM order too.

## Binaural (HRTF) output

Selecting "Binaural (HRTF)" as the Output Format (toolbar -> "Output...") decodes
straight to 2-channel headphone audio inside the plugin -- an HRTF
convolution, not the same thing as the plain 2-speaker `Stereo` format
(no HRTF/head-related processing at all). Technique: the summed Ambisonics
bus is first decoded to a dense, 50-point virtual loudspeaker array (the
same `SphericalHarmonicsUtils::fibonacciSphere` point distribution and
max-rE-weighted decode AllRAD already uses for the real-speaker formats
above), then each virtual speaker's signal is convolved through that
direction's own measured left/right head-related impulse response
(`juce::dsp::Convolution`, one L/R pair per virtual speaker) and summed to
the output. See `BinauralDecoder.h` for the full two-stage breakdown and
its own disclosed limitations (CPU cost not measured on real hardware,
interaural delay not applied -- see "Known limitations" below).

**Loudness calibration.** Reported bug: Binaural came out substantially
louder than every other Output Format, above 0dB often enough to matter,
and inconsistently so between the two bundled datasets (SADIE II louder
than KEMAR) -- because summing 50 un-normalized HRIR convolutions (each
one deliberately left un-normalized, `Normalise::no`, since a direction's
own level -- e.g. head-shadow attenuation behind the listener -- is real,
physically meaningful content) has no reason to land anywhere near the
same overall level as, say, a 2-channel VBAP pan. Fixed the same way
`AmbisonicsDecoder::calibrateDecodeMatrix()` already calibrates every
other decode mode: `BinauralDecoder::prepare()` now also builds, per
virtual speaker, each speaker's own combined impulse response toward 32
test source directions (weighted by that direction's own SH decode
gain), measures the resulting two-ear output ENERGY across all of them,
and scales the final L/R sum by one constant so the average comes out to
roughly unit energy -- `BinauralDecoder::calibrateOutputGain()`, applied
in `decode()`. A practical measurement, not an analytical derivation
(same "practical, disclosed approximation" precedent as
`calibrateDecodeMatrix()` itself), but it now compensates for a given
HRTF dataset's own absolute measurement level along with everything
else. See `Tools/verify_binaural_decoder.cpp`'s loudness-calibration
section, which checks both that the calibrated RMS lands in a sane
absolute range and that all bundled datasets (KEMAR/SADIE II/KU100)
land within 2x of each other.

A dataset picker appears in the Output window whenever Binaural is
selected ("HRTF Dataset"), reading HRIRs via `HrtfDataset` (a thin wrapper
around [libmysofa](https://github.com/hoene/libmysofa), BSD-3-Clause):

- **KEMAR** (default) -- Gardner & Martin, MIT Media Lab. Freely usable
  with citation. Bundled.
- **SADIE II -- D1 (KU100)** -- University of York, Apache License 2.0.
  An alternative measured head (a dummy-head mannequin rather than KEMAR's
  own). Bundled.
- **KU100 -- 2deg Grid (TH Koeln / Bernschuetz)** -- Benjamin Bernschütz,
  TH Köln, CC BY 3.0 (Zenodo DOI 10.5281/zenodo.3928297). The densest of
  the three bundled datasets: a full-sphere 2-degree Gauss-Legendre grid,
  16020 measurement points (`HRIR_FULL2DEG.sofa` from the compilation),
  natively 48kHz. A different KU100 measurement from SADIE II D1 above
  (same dummy head model, different lab/grid). Bundled.
- **Custom SOFA file...** -- import any AES69/SOFA-format HRTF measurement
  of your own via "Browse...". Lets you use a personally-measured or
  third-party HRTF instead of any bundled default.

All three bundled datasets require attribution when used or
redistributed -- see `THIRD_PARTY_LICENSES.md` for the exact required
text. Switching datasets rebuilds `binauralDecoder`'s decode matrix and
all 100 convolution engines immediately (message thread, not
real-time-safe) -- expect a brief pause, longer for SADIE II/KU100
(larger files, ~35MB/~19MB respectively vs. KEMAR's ~1MB). The choice of
dataset is plugin-instance state, like Output Format/Bass Management --
not saved in a preset, see "Presets" and the Known Limitations note on
`Presets/schema/README.md`'s own reasoning for why.

## Objects, motion physics, and the parameter panel

- **Object count is dynamic.** Only object 0 is active at startup (no
  longer all of `SAPOC_MAX_LIVE_INPUTS`). "+ Object"/"- Remove Object"
  (object list, left side, alongside its "Objects: N / M" count) activate/
  deactivate individual slots out of the max. 8 object
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
- **Force field & time scale:** `SceneSettings::globalField` (shown in
  the panel as "Force Field" -- constant force/mass, like wind/gravity,
  affects Impulse/Attracted objects; not the same thing as
  `windVector`/"Propagation Wind" below, which only affects sound
  propagation, never actual object movement) and `timeScale`
  (fast-forward/slow-motion for the whole simulation).
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
  per-grain selection exists, see the Grains parameter category).
  Each row also has its own **M**(ute)/**S**(olo) buttons
  (`SoundObject::muted`/`soloed`) -- this is the ONLY place these two are
  exposed in the GUI, deliberately not duplicated as checkboxes in the
  parameter panel's Object category. Own `muted` always wins over
  `soloed`; otherwise, soloing any object silences every object that
  isn't itself soloed (classic non-exclusive DAW solo -- several objects
  can be soloed together and all stay audible, it's not a single-object
  radio button). Applies to an object's `GrainCloud` too, not just its
  own signal. Toggling fades over ~20ms rather than cutting instantly, so
  it never clicks; once an object has actually reached silence, its
  propagation/encoding work (and its grains') is skipped entirely for
  performance, not just gained down to zero every block.

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
- **Room boundary** renders as a shaded, translucent sphere
  (`SceneSettings.roomSize` was already conceptually spherical -- see
  `PluginEditor.cpp`'s `drawShadedBoundarySphere()`) instead of a flat
  reference circle. No 3D mesh/lighting model (still no OpenGL, see
  above) -- a cheap "fake sphere" trick instead: since this camera always
  looks directly at the world origin, an origin-centered sphere's
  silhouette is always an *exact* circle centered on the viewport middle,
  for any camera angle or zoom
  (`Camera3D::projectSphereSilhouetteRadius()`, geometrically exact, not
  an approximation). A radial gradient (transparent center -> semi-opaque
  rim) reads as a translucent shell that clearly marks the boundary
  without hiding objects/grains inside it -- deliberately no specular
  highlight (an earlier version had one, lit from a fixed world-space
  direction; removed again, it didn't read well visually). A flat ground
  grid (1m/2m/3m circles) and a small "Front" marker/label at
  the origin remain as orientation aids. Purely cosmetic
  `SceneSettings::showRoomBoundary` toggle (Scene category, "Show
  Boundary") hides the sphere without disabling the boundary itself --
  `reflect`/`wrap`/`absorb` keep applying while it's hidden, only the
  drawing is skipped.
- **Gesture reminder**, a small always-on text overlay at the bottom of
  the scene view: "Double-click object: Orbit | Shift+Drag object: Sling
  launch" plus a dimmer second line for the Ctrl/Alt modifiers and the
  camera-rotate/zoom gestures. Both Orbit and Sling launch are pure
  mouse+modifier-key gestures with no button or menu entry anywhere else
  in the GUI, so without this there is no way to discover them at all.
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
- **Three launch modes, cycled mid-gesture with Ctrl.** While still
  holding Shift and dragging, tapping Ctrl steps through Free Throw ->
  Orbit Shot -> Slingshot -> back to Free Throw (`SlingLaunchMode` in
  `PluginEditor.h`). Every gesture starts on Free Throw, even if Ctrl
  happens to already be held down when the drag begins -- only an actual
  fresh press advances the mode, the same edge-detection rule Alt and Tab
  already follow (see below), so briefly holding Shift while Ctrl is
  already down for an unrelated reason can't silently skip a mode.
  - **Free Throw** (default): reuses the existing throw/`Impulse` physics
    (`TrajectoryEngine::throwObject()`), just aimed by the pull instead of
    by release velocity.
  - **Orbit Shot** (tap Alt to step through circular/elliptical shapes):
    launches the object directly onto a *scripted* orbit instead of a
    free trajectory (`TrajectoryEngine::startOrbit()`) -- a mathematically
    exact ellipse/circle, not a physics simulation. Pull distance sets the
    orbit's size (semi-major axis), the pull line's own direction sets the
    ellipse's orientation (`SoundObject::orbitOrientation`), and the spin
    direction (CW/CCW) is derived from the gesture's geometry rather than
    a separate control -- pulling to one side of the object versus the
    other naturally produces the opposite spin
    (`SlingGesture::computeOrbitDirectionSign()`, a signed 2D cross
    product of the anchor's position relative to the orbit center and the
    launch direction). Centers on the world origin ("Center") by default,
    consistent with the double-click orbit gesture -- or tap **Tab**
    (repeatedly) to cycle the center itself through every other active
    object in the scene and back to "Center" first (see "Reference
    targeting" below); the orbit then tracks that object's LIVE,
    possibly-moving position every tick
    (`TrajectoryEngine::startOrbit()`'s `referenceObjectId` parameter ->
    `SoundObject::orbitReferenceObjectId`, which `integrate()` already
    re-reads every tick for exactly this reason).
  - **Slingshot** -- a REAL, physics-based gravity-assist, not a scripted
    path. Pick a target object with Tab (see below; "Center" has no
    gravity-well meaning here, so it just degrades to a plain Free Throw),
    and the thrown object is continuously pulled toward that object's live
    position via the exact same inverse-square n-body force law the
    Attraction system already uses
    (`TrajectoryEngine::computeAttractionForce()`, extended with a small,
    self-contained addition for this -- see
    `SoundObject::slingshotTargetId`/`slingshotStrength`). Crucially, this
    does NOT touch the target object's own Attraction settings at all --
    firing a slingshot never mutates anything about the object you aimed
    at, the pull is private to the thrown object itself, with a fixed
    strength (`SlingGesture::slingshotGravityStrength`) scaled by the
    target's real `Mass` (already a per-object field). Depending on
    approach speed, distance, and that mass, the outcome is a genuine
    physics result, not a scripted one: a deflected flyby that continues
    on a new course (a classic "gravity assist"), or a capture into a
    bound, looping trajectory around the target -- try both by throwing
    fast-and-wide versus slow-and-close past a heavy object.
- **Reference targeting (Tab).** While pulling, tap **Tab** repeatedly to
  cycle a shared target selection through every other currently active
  object in the scene and back to "Center"
  (`SlingGesture::cycleSlingReference()`, `PluginEditor::slingReferenceObjectId`).
  Used by both Orbit Shot (which point the scripted ellipse centers on)
  and Slingshot (which object's gravity pulls on the thrown one) --
  tracked independently of the current launch mode, so a target can be
  picked before or after Ctrl-cycling into a mode that uses it. Switching
  into Slingshot while still on "Center" auto-selects the first available
  object instead of silently doing nothing, if one exists. Only
  meaningful while the gesture is active -- Tab does nothing otherwise,
  and every gesture starts back on "Center".
- **`SoundObject::orbitOrientation`** (new field): rotates an elliptical
  orbit's major axis within its orbit plane, radians, irrelevant at
  `orbitEccentricity=0`. `TrajectoryEngine::startOrbit()` gained matching
  optional `eccentricity`/`orientation` parameters (default 0 = unchanged,
  circular behavior, so the existing double-click gesture and any preset
  written before this feature keep working exactly as before).
- **All pull-to-launch math lives in `SlingGesture.h`**, deliberately
  separate from the JUCE mouse-handling code in `PluginEditor`, so the
  exact same functions run in the editor and in
  `Tools/verify_orbit.cpp` (checks the direction-sign/orientation math,
  the ellipse-rotation formula in `TrajectoryEngine`, the reference-cycle
  logic, and the Slingshot gravity physics -- including that a pulled
  throw is measurably deflected compared to an otherwise-identical plain
  one) -- not a test reimplementation.
- **Live movement preview while aiming.** In addition to the bow line
  (which shows the pull/aim, i.e. the *opposite* of where the object will
  actually go), a second, dashed element previews the actual result and
  updates live as you drag, one per mode:
  - **Free Throw:** a straight dashed line from the anchor in the launch
    direction, with a small dot at its tip, length proportional to pull
    distance -- deliberately not a full trajectory simulation (no
    `globalField`/damping curvature), just a clear directional hint, per
    the design brief.
  - **Orbit Shot:** the actual resulting ellipse/circle outline, sampled
    with the same `OrbitMath.h` formula `TrajectoryEngine` itself uses to
    move an orbiting object -- reflects the current pull distance,
    direction, eccentricity step, and target live, exactly as it will
    look the instant the shot fires.
  - **Slingshot:** the actual resulting curved path, forward-simulated a
    couple of seconds ahead with the exact same force law/constant
    `computeAttractionForce()` uses
    (`SlingGesture::simulateSlingshotPreview()`, simple Euler integration)
    -- a real, physically accurate preview of the deflection, not an
    approximation, though it does treat the target as momentarily fixed
    for the (short) preview horizon rather than also simulating its own
    motion, the same kind of simplification the Free Throw preview above
    already makes.
  - All three are dashed specifically so they can never be confused with
    the real, already-happened movement trail or a confirmed orbit path
    (both solid) -- and all disappear completely the moment the mouse is
    released, nothing lingers once the object actually starts moving.
  - Grains are unaffected by any of this: a `GrainCloud` on the slung
    object (e.g. in `AttractRepelSiblings` mode) keeps moving independently
    of the parent object's own preview/throw, exactly as already
    established -- the preview only ever describes the parent object's
    own upcoming motion, never the grains'.

## Gamepad control

Connect a controller (macOS's GameController framework -- this covers
most modern game controllers, e.g. Xbox/PlayStation controllers paired
over Bluetooth or USB). Every control has a sensible fixed default
binding out of the box, no setup required -- all still individually
overridable via Learn mode (see "Controller mapping" below) exactly like
any other bindable control:

| Control | Default behavior |
|---|---|
| Left stick | Rate-controls the selected object's position on the ground plane: deflection sets its current velocity continuously, in whichever direction you push; centering the stick (its own spring-back is enough) stops the object exactly where it is, immediately, with no drift and no snapping back to where it started. Touching the stick switches the selected object into Manual mode automatically, the same mode a mouse drag uses. |
| Right stick | Orbits the camera view (azimuth/elevation) -- the gamepad equivalent of dragging empty space with the mouse. Editor-only (there's nothing to look at with the window closed). |
| D-pad Up/Down | Zooms the camera in/out. Editor-only, same reasoning as the right stick above. |
| D-pad Left/Right | Cycles the selection to the next/previous active object, wrapping around (bidirectional). |
| Button A | Activates the next inactive object slot and selects it -- the gamepad equivalent of the "+ Object" button in the object list. |
| Button B | Deactivates the currently selected object and clears the selection -- the gamepad equivalent of "- Remove Object". |
| Button Y (hold) | Aims a **Free Throw**: while held, the left stick's direction and magnitude set the launch direction/strength (push the stick the way you want the object to fly); releasing fires it. |
| Left Shoulder (hold) | Aims an **Orbit Shot** the same way -- releases into a scripted circular orbit around the world origin, sized/oriented by how far/which way the stick was pushed. |
| Right Shoulder (hold) | Aims a **Slingshot** the same way -- a real, physics-based throw pulled toward the first other active object (or a plain throw if none exists), same gravity-assist mechanic as the mouse gesture's Slingshot mode. |

Free Throw/Orbit Shot/Slingshot are the gamepad equivalent of the mouse's
Shift+drag sling gesture (see "The sling launch gesture" above) --
deliberately a direct analog mapping (push the stick the way you want it
to launch) rather than that gesture's pull-back-then-release feel, which
only makes sense with a visible cursor to pull away from. There's no
gamepad equivalent of that gesture's Ctrl/Alt/Tab modifiers: Orbit Shot
always centers on the world origin at a fixed circular shape, and
Slingshot always auto-targets the first other active object -- pick a
specific center/target by using the mouse gesture instead. Holding a
throw button suspends the left stick's own movement control for that
object until you release.

Button X has no built-in behavior -- free for a Learn-mode binding of
your own, same as any other raw control (see "Controller mapping"
below).

Camera control aside (which needs the editor open to mean anything),
everything above runs independent of the editor window (see "Runs whole
simulation in the background" note near the top) and independent of any
DAW automation -- it's a live, always-on control path, not a recordable
parameter. No detection UI yet: there's currently no on-screen indicator
for whether a controller is connected (`KlangorbitProcessor::
isGamepadConnected()` exists for a future indicator to read).

- Only one controller is read at a time (whichever the OS reports
  first).
- **Optional inertia mode** (off by default,
  `KlangorbitProcessor::setGamepadInertiaModeEnabled()`, no UI toggle
  yet): instead of stopping instantly, the object keeps moving after
  the stick is released and decelerates under the same physics an
  ordinary thrown object uses (damping/drag), rather than snapping to a
  stop.
- **Deadzone and response curve** are tuned to reasonable defaults (8%
  deadzone, exponent-2 curve -- fine control near center, full speed
  needs a deliberate push) but have no UI to adjust yet
  (`KlangorbitProcessor::setGamepadDeadzone()`/
  `setGamepadCurveExponent()`). The throw gestures' own launch-strength
  scale is similarly adjustable only in code
  (`KlangorbitProcessor::setGamepadThrowMaxPullDistance()`).
- The stick-to-movement, camera-look, and camera-zoom direction
  conventions haven't been confirmed against real hardware in this
  environment -- see "Known limitations" below if any of them feel
  inverted.

## Controller mapping

Open **Mappings...** in the toolbar to bind any gamepad, MIDI, or OSC
control to any parameter: pick a **Target parameter** from the dropdown,
press **Learn**, then move the stick/trigger/button/MIDI knob/OSC
control you want to drive it -- the panel captures whichever control
changed next and creates the binding automatically, the same way
regardless of which of the three sources it came from. The binding list
shows every current mapping (with a **Remove** button each); **Load
Profile.../Save Profile...** save the whole binding set (plus the paging
modifier below) as its own file, independent of scene presets
(`MappingProfiles/schema/README.md`).

The target-parameter dropdown only lists scene-wide parameters and
"whichever object is currently selected" parameters (`mass`, `gain`,
`orbitRadius`, and so on) -- not a specific object regardless of
selection. That covers the great majority of real use; binding a
control to one specific object permanently (object 3's mass, say,
whatever is selected) is possible but only via hand-editing a saved
mapping profile's JSON for now, not through this picker.

**Paging**: hold the modifier control shown under "Paging modifier"
(default the left trigger -- deliberately not a shoulder button, since
Left/Right Shoulder already have a built-in meaning of their own, see
"Gamepad control" above) to unlock a second layer of bindings -- the
same stick can drive one parameter normally and a different one while
the modifier is held. Exactly two layers. Learn a binding while holding
the modifier to place it in the second layer; Learn without holding it
to place it in the first (default) layer.

The left stick's own built-in rate-control movement (see "Gamepad
control" above) is a real default, not a fixed one: binding either of
its axes to something else via Learn mode takes over that axis pair
completely, and the object it was moving just stays put.

## MIDI / OSC control

Two more input sources on the same canonical layer as the gamepad
(`Source/MidiDriver.h/.cpp`, `Source/OscDriver.h/.cpp`) -- bind either
one to a parameter through the exact same **Mappings...** / Learn-mode
workflow described above, no separate MIDI-Learn or OSC-Learn step.

- **MIDI**: the plugin already declares MIDI input (`NEEDS_MIDI_INPUT`),
  so any MIDI routed to it by the host (or, in Standalone, by macOS's
  own MIDI input selection) works automatically -- no extra setup in
  Klangorbit itself. Control Change, Note On/Off, and Pitch Bend are all
  bindable; a knob/fader sends CC, a pad/key sends Note On/Off (as a
  button, not velocity-sensitive), a pitch strip sends Pitch Bend
  (bipolar, centered).
- **OSC**: listens on UDP port **9000** by default (matches TouchOSC's
  own default) -- point any OSC control-surface app (TouchOSC, Lemur,
  etc.) at this machine's IP on that port and its controls become
  bindable the same way. A control sending a normalized 0..1 float or
  int value binds as a continuous control; a control with no arguments
  (a bare trigger/button message) binds as a button.
- **No UI yet** for changing the OSC port or showing MIDI/OSC connection
  status (`KlangorbitProcessor::isOscConnected()`/`getOscPort()`/
  `setOscPort()` exist for a future indicator/setting to use) -- the
  same kind of gap already noted above for gamepad deadzone/curve/
  inertia tuning, not an oversight.

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
  `SoundObject::dopplerEnabled` (default true, "Doppler Enabled" at the
  top of the Doppler category) is a separate on/off gate that short-
  circuits the AC contribution to 0 without touching the stored
  `dopplerFactor` value itself -- re-enabling restores whatever factor
  was actually dialed in, rather than needing to remember and re-type a
  value that was overwritten to 0.
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
- **Propagation Wind** (`SceneSettings::windVector`, m/s -- shown in the
  panel as "Propagation Wind", not just "Wind", to keep it from reading
  as a duplicate of "Force Field"/`globalField` above: this one only
  affects sound, never object movement) shifts the effective speed of
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
- **Pitch Jitter Mode.** `pitchJitter` (0..1) is the REACH, shared by two
  modes (`GrainCloudSettings::pitchJitterMode`): `Random` (default) applies
  it as the original continuous, uniformly random +/- playback-rate
  deviation; `Scale` instead quantizes -- each grain's pitch snaps onto a
  random degree of `pitchQuantizeScale` (standard interval sets: `Octaves`,
  `Fifths`, `MajorTriad`, `MinorTriad`, `MajorScale`, `Dorian`, `Lydian`,
  `Mixolydian`, `Aeolian`, `WholeTone`, `Octatonic`, `Hexatonic`,
  `Acoustic` -- the "overtone scale"/Lydian Dominant, the closest standard
  12-TET scale to the real, inharmonic overtone series) within `pitchJitter`
  * one octave of reach, root = the grain's own natural pitch (no separate
  key/note picker -- see `Source/Grain.h`'s own comment on both enums and
  `GrainCloud.cpp`'s `pickQuantizedSemitoneOffset()`). Reach is capped at
  one octave in both modes so a grain's `playbackRate` never exceeds the
  same `GrainLimits::maxPitchJitterPlaybackRate` (2.0) the ring buffer is
  sized against.
- **Read-depth range.** `grainReadDepthRangeMin`/`grainReadDepthRangeMax`
  independently control how far into the ring buffer's *past* a grain's
  start point may be drawn from, additive with `positionJitterInBuffer`
  above (that one stays a small de-clicking offset near the current write
  head; this is a deliberate, much larger reach into history -- up to 10s).
  0/0 (default) disables it, same as before this existed.
  `grainReadDepthDistribution` picks how the depth is sampled within the
  range: `Uniform`, `WeightedTowardRecent`, or `WeightedTowardOld`. Bounded
  by `GrainLimits::maxGrainReadDepthRange` (`Source/Grain.h`), the same
  constant the ring buffer is sized from, so the UI simply cannot request a
  depth beyond what's actually allocated.
- **Single-shot grain model.** `grainDuration` is both the audio envelope
  length and the movement lifetime -- a grain is spawned, moves for that
  duration while fading, and is done. `grainRate` ("Grain Rate (Spawn
  Rate)" in the UI) controls how often new grains spawn, deliberately
  independent of `grainDuration` at the scheduling level
  (`GrainCloud::update()`'s spawn-interval timer only ever reads
  `grainRate`).
- **Voice stealing.** `maxConcurrentGrains` is a hard per-cloud ceiling on
  simultaneously-alive grains (real CPU cost -- every active grain is a
  full Ambisonics encode pass). Sustaining `grainRate * grainDuration`
  overlapping grains needs that cap raised to match; once it's hit,
  `GrainCloud::update()` no longer stalls the spawn schedule waiting for
  an old grain to expire (that was the original design, and the reported
  bug: raising `grainDuration` without also raising the cap audibly
  throttled/stuttered the spawn rate). Instead it voice-steals: the
  OLDEST active grain gets a short (10ms) forced fade-out
  (`GrainCloud::beginVoiceSteal()`, reusing the exact same
  guaranteed-exact-zero Hann envelope ending every grain's natural end
  already relies on -- no new audio-thread code, just a shortened
  `lifetimeSeconds`/`grainLengthSamples`) and its slot is reused as soon
  as that fade completes. Trade-off: `grainRate`'s schedule is honored
  continuously regardless of `grainDuration`, but individual grains can
  end up shorter than configured once oversubscribed. See
  `Source/Grain.h`'s `GrainCloudSettings` class comment for the full
  explanation, and `Tools/verify_grain_cloud.cpp`'s
  `testVoiceStealingKeepsSpawningContinuous`/
  `testVoiceStealingShortensNotCorrupts`.
- **Movement modes** (`Source/Grain.h`, `GrainMovementMode`): `RandomWalk`,
  `Bounce` (elastic reflection within `boundaryRadius` around the spawn
  position, `restitution`), `RadialExplosion` (`initialSpeed` +
  `acceleration`, outward), `OrbitAroundParent` (relative to the parent
  object's *current* position, which can itself be moving),
  `AttractRepelSiblings` (n-body force *within the same cloud only*,
  reusing `TrajectoryEngine::computeAttractionForce`'s softened
  inverse-square model; negative `attractionStrength` repels).
  - **`orbitSphereSpread`** (`OrbitAroundParent` only, 0..1): blends each
    grain's own orbit-plane normal from `{0,0,1}` (flat, the original
    behavior -- every grain circles in the same horizontal plane) toward
    a uniformly random unit vector (1 -- each grain's own plane is
    essentially random), chosen once per grain at spawn
    (`GrainCloud::spawnGrain()`, stored in `Grain::orbitPlaneNormal`) and
    reused every tick after. `GrainCloud::orbitPlaneOffset()` builds an
    orthonormal basis for whatever plane that normal defines (seeded
    deliberately so `normal={0,0,1}` reproduces the exact original
    `cos(phase), sin(phase), 0` formula, not just an equivalent
    reparametrization) -- a strict generalization, zero behavior change
    at the default. Over many grains and full rotations, higher spread
    values sweep out a shape approaching a sphere instead of a flat
    disc. See `Tools/verify_grain_cloud.cpp`'s `testOrbitSphereSpread`.
- **Per-parameter jitter.** Each of `grainRate`, `grainDuration`,
  `boundaryRadius`, `initialSpeed`, and `orbitRadius` has its own
  dedicated `*Jitter` field (0..1, fractional +/- randomization applied
  at spawn time, same shape as the pre-existing `pitchJitter`) -- all
  usable simultaneously, unlike the earlier single shared
  `jitterTarget`/`jitterRange` pair (mutually exclusive, only one field
  jitterable at a time), which these superseded. `grainRateJitter`
  specifically randomizes the spawn *interval* itself (see
  `GrainCloud::update()`), not a per-grain property. Old presets that
  saved the legacy pair are migrated onto the matching new field on load
  (`PresetManager::grainCloudSettingsFromVar()`), never written on save.
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
  per-grain delay/air-absorption/directivity pass would be too expensive
  with dozens of concurrent grains.
- **Optional per-grain Doppler** (`Source/GrainDoppler.h`, "Doppler" toggle
  in the Grains parameter category, **off by default**): a much
  cheaper approximation than `PropagationProcessor`'s delay-line-based
  Doppler -- a single classic-Doppler-formula pitch ratio computed once
  per grain per audio block from its control-rate position/velocity
  snapshot, multiplied into the existing pitch-jitter-based playback
  rate, no delay line and no per-sample cost. Uses the parent object's own
  `dopplerFactor` (Doppler parameter category) to scale strength, so it's
  one familiar knob, not a second one. Off by default because it's a real
  (if small) added cost per grain per block, and at up to 256 concurrent
  grains that adds up -- opt-in rather than silently changing existing
  grain-cloud sound.
  - **Read-position continuity fix.** Reported bug: enabling Doppler
    produced audible clicks. Root cause: `GrainRenderer.h`'s
    `renderGrainBlock()` used to compute each sample's ring-buffer read
    position as `bufferReadStartSample + samplesPlayed * playbackRate` --
    correct only if `playbackRate` never changes for a grain's whole life
    (true of `pitchJitter`, which is fixed once at spawn, but NOT of
    Doppler's own ratio, recomputed fresh every block from the grain's
    live position/velocity). Whenever the rate changed, that formula
    recomputed the ENTIRE elapsed read position at the new rate, jumping
    the read pointer discontinuously every block the ratio changed --
    audible as a click. Fixed by making the read position a persistent,
    caller-owned accumulator (`PluginProcessor::GrainAudioState::
    readPosition`, advanced by `playbackRate` per rendered sample rather
    than recomputed from scratch each call) -- only the RATE of advance
    changes at a block boundary now, not the position itself. See the
    mid-grain-rate-change regression check added to
    `Tools/verify_grain_cloud.cpp`'s `testRenderGrainBlock()`.
- **Global spawn budget.** `maxConcurrentGrains` caps each cloud
  individually (up to 256, default raised 8 -> 32 -> 256 over time --
  comfortably covers common `grainRate * grainDuration` combinations
  without needing voice stealing at all); a further system-wide cap
  (`KlangorbitProcessor::maxConcurrentGrainsGlobal`, currently 256,
  raised from an initial 32) is shared across all clouds each control-rate
  tick, since every active grain costs a full Ambisonics encoding pass
  regardless of cloud. `grainDuration` (up to 5s) and `grainRate` (up to
  500/sec) were extended alongside it. 256 is a rough operation-count
  estimate for real-time safety, not a number profiled on real hardware in
  this environment -- the toolbar's **CPU meter** (top-right, next
  to Mappings...) shows the actual measured fraction of each audio
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
  above); a "Grains" category in the
  parameter panel exposes all cloud-level parameters (one parameter set per
  cloud, not per individual grain).
- **Verification:** `Tools/verify_grain_cloud.cpp` exercises the exact
  grain-rendering function the plugin uses (`GrainRenderer.h`, shared, not
  reimplemented) plus all five movement modes and the spawn-budget logic,
  no GUI/audio device needed.

## GUI design

Dark, cyan-accented, reduced "sci-fi HUD" look, applied uniformly rather
than styled ad hoc per widget:

- **`Source/UiTheme.h`** -- the single source of truth for every colour and
  spacing value used across the GUI (backgrounds, borders, the cyan accent,
  text tones, Mute/Solo's red/amber, a 4/8/12/16/24px spacing scale).
  `PluginEditor`, `ParameterPanel`, and `ObjectListPanel` all read from it
  instead of each picking their own shade of grey.
- **`Source/SciFiLookAndFeel.h/.cpp`** -- a `juce::LookAndFeel_V4` subclass
  applied once to the top-level editor (`setLookAndFeel()` in its
  constructor, cleared in its destructor), so every child component
  inherits it automatically. Mostly just recolours JUCE's own stock V4
  widget shapes via `setColour()` (safe, well-tested colour cascading) --
  plus three deliberately simple, hand-drawn overrides where the stock
  shapes didn't fit the brief: flat buttons with an accent underline for
  the active state (used by both the parameter panel's category tabs and
  any other toggle-driven button, incl. the object list's Mute/Solo,
  entirely through `Button::getToggleState()` + its own `buttonOnColourId`
  -- the same mechanism handles cyan, red, or amber "on" states without
  hardcoding any of them into the drawing code itself), a thin-track
  slider instead of a filled pill + circular knob, and a pill-style toggle
  switch instead of a checkbox tick.
- Each of the three panels (toolbar, object list, parameter panel) paints
  its own solid background plus a hairline border where it meets the 3D
  viewport or another panel, so the layout reads as distinct regions
  instead of floating controls over a shared black canvas.
- Per-object colours in the 3D view are restricted to a blue -> violet ->
  magenta hue band (`PluginEditor.cpp`'s `objectColour()`) rather than the
  full hue wheel, specifically so no object colour can ever coincide with
  the cyan selection-ring accent or the red/amber Mute/Solo indicators.
- Not independently visually verified (see "Known limitations / next
  steps" below) -- built and reasoned through carefully, checked via
  successful compilation and the app launching/staying stable, but never
  seen on an actual screen in this environment.

## Project structure

```
Klangorbit/
  CMakeLists.txt
  CHANGELOG.md          <- code versioning (SemVer)
  Assets/
    AppIcon.png           <- 1024x1024 master, baked into a .icns at build time (see "Build" above)
    AppIcon.svg           <- editable vector source for AppIcon.png
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
- **Mapping-panel "profile status" doesn't reflect the profile loaded
  automatically at startup.** `KlangorbitProcessor`'s constructor loads
  `MappingProfiles/factory/default.json` directly into `MappingEngine`
  before any UI exists to show that it happened -- the panel's own
  status line says "(no profile loaded)" even though the (currently
  empty, so behaviorally identical either way) default was in fact
  loaded. Cosmetic only.
- **Binding a specific object regardless of selection needs hand-
  editing a mapping profile.** The Learn-mode picker only offers
  scene-wide and "whichever object is selected" targets (see
  "Controller mapping" above) -- a `Scope::SpecificObject` binding
  (e.g. always object 3's mass) works if authored directly in a mapping
  profile's JSON, but there's no UI path to create one.
- **Gamepad direction conventions are unverified against real
  hardware.** No gamepad was available to test with in this
  environment -- the world-space stick-to-movement convention (stick
  "up" moves the object further away, stick "right" moves it right),
  the throw-gesture stick-to-launch-direction convention (same mapping,
  see `GamepadDriver::driveThrowGesture()`), and the right-stick/D-pad
  camera-look/zoom convention (see `KlangorbitEditor::
  updateGamepadCamera()`) are all reasonable, documented choices, not
  confirmed-by-eye ones. Each is a one-line sign flip if it turns out
  inverted, same situation as the mouse camera drag/zoom convention
  above.
- **Output format switching is host-dependent to take effect live.**
  `KlangorbitProcessor::setDecoderMode()` (the "Output..." window's
  "Output Format" dropdown) DOES change the encoder order, rebuild the decode
  matrix, and request a host bus-layout renegotiation at runtime -- but
  VST3's mechanism for a PLUGIN-initiated bus change is weaker than a
  host-initiated one (this JUCE version's wrapper has no dedicated
  "rescan my bus layout" restart flag). Confirmed working live in Reaper;
  other hosts may only pick up a new mode's channel count after the
  plugin is removed and reinserted, or the project reloaded. See the
  CHANGELOG entry for why this bus-per-mode approach was chosen anyway
  over a fixed always-16-channel bus. The same applies to changing
  Circular Array's Speaker Count while that format is already active --
  it's a channel-count change too, `KlangorbitProcessor::
  setCircularArraySpeakerCount()` requests the same best-effort
  renegotiation.
- **Circular arrays (Octophonic, Circular Array) are horizontal-only.**
  Neither can reproduce elevation/height content at all -- a property of
  a flat ring of speakers, not something a better decoder could fix.
  Circular Array additionally loses some spatial precision (more blur,
  not incorrect direction) below 7 speakers, since the fixed internal
  order-3 encode needs at least 7 evenly-spaced speakers for alias-free
  circular-harmonic reconstruction -- see the CHANGELOG entry.
- **Binaural (HRTF-based) output's CPU cost is not measured on real
  hardware in this environment.** `BinauralDecoder` runs 50 (virtual
  speakers) x 2 `juce::dsp::Convolution` instances every block -- 50 was
  chosen to match AllRAD's own already-proven virtual-array density, not
  independently profiled. Check the toolbar's CPU meter after switching
  to Binaural; the constant is easy to tune down in `BinauralDecoder.h`
  if it proves too costly on real hardware.
- **Binaural interaural delay (ITD) is not applied -- a disclosed v1
  simplification.** `HrtfDataset::getFilter()`'s own per-ear delay
  outputs are measured but currently discarded; only the amplitude/
  spectral shape of each ear's HRIR is used. This mainly affects
  precision of front-back/elevation localization cues that depend on
  fine timing differences rather than level differences -- a future
  enhancement would apply them as a per-speaker fractional delay before
  convolution. See `BinauralDecoder.h`'s own comment.
- **The full AU experience in Logic Pro's actual insert UI has not been
  verified end-to-end.** Both the input-side (Mono/Stereo/Quad/7.1) and
  output-side (the 9 Logic-viable Output Formats, greyed appropriately)
  bus flexibility are confirmed correct at the AU-protocol level (`auval
  -v aumf Klor Jgck`, full PASS, `Reported Channel Capabilities` showing
  the complete 4x6 = 24-pair cross product) -- but whether Logic's own
  insert UI actually offers/accepts Klangorbit cleanly across every
  track/bus type, whether the Output Format dropdown's greying reads
  correctly against a real negotiated bus, and whether switching between
  AVAILABLE formats behaves as expected live in Logic, still needs
  hands-on testing in Logic itself; not something `auval` (or this
  environment, with no way to run Logic) can confirm.
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
