# Klangorbit -- User Guide

Klangorbit is an object-based Ambisonics spatializer built around a live
physics engine: instead of automating position with envelopes or curves,
you give sound objects mass, velocity, gravity, and momentum, and let
their motion emerge from that. Its output format is selectable (Output
category, section 12): an internally decoded Stereo (the default --
audible immediately, no external decoder needed), Binaural (HRTF-based
headphone output, see section 12), Quad, 5.1, 7.1, Dolby-Atmos-bed
(5.1.2/5.1.4/7.1.2/7.1.4), Octophonic (a fixed 8-speaker circular array),
or Circular Array (any speaker count 4-24) output -- or raw Ambisonics
B-format (ACN/SN3D, AmbiX-compatible) to feed an external decoder such as
SPARTA AmbiBIN/AmbiDEC or the IEM Plugin Suite instead.

This guide covers how to use the plugin. For build instructions and
internal architecture notes, see `README.md` in the project root.

---

## 1. Getting started

1. Insert Klangorbit on a track that has as many mono inputs routed to it
   as you want moving sound sources (each active object consumes one
   input channel).
2. Feed a decoder (SPARTA AmbiDEC/AmbiBIN, IEM BinauralDecoder, etc.)
   from Klangorbit's Ambisonics output -- or switch Output Format to
   Binaural/Stereo/Quad/5.1/7.1/an Atmos-bed variant instead to skip an
   external decoder entirely (see section 12).
3. Open the Klangorbit editor: you'll see a 3D scene view in the middle,
   an object list on the left, a toolbar across the top, and a parameter
   panel on the right.
4. Click **+ Object** to activate your first sound object -- it appears
   on a small reference circle around the origin.
5. Click and drag it around to hear it move in the decoded output.

Two things worth knowing right away: **double-clicking any slider in the
parameter panel resets it to its default value** (every slider, in every
category -- JUCE's own standard double-click-to-reset behavior); and
**Mappings.../Output...** each open their own small window (top-right,
next to the **?** help button) rather than living in the parameter panel
itself, since neither is tied to the scene or any one object.

## 2. The scene view

The scene view is a live 3D view of your objects, looking down at the
origin by default (the listener's position). You can freely orbit and
zoom this camera without affecting the actual spatial audio -- it's just
your viewpoint, not the listener's.

- **Drag on empty space** -- orbit the camera around the origin.
- **Scroll wheel** -- zoom in/out.
- **Left-click an object** -- select it. Its parameters appear in the
  panel on the right.
- **Left-click + drag an object** -- move it live by hand (its motion
  mode becomes *Manual* while you hold the mouse).
- **Release while dragging** -- the object keeps whatever velocity your
  drag implied and continues moving under physics (*Impulse* mode) --
  a simple throw.
- **Double-click an object** -- start (or stop, if already orbiting) a
  circular orbit around the origin, as a quick demo/shortcut. For more
  control over an orbit's shape, use the parameter panel's Orbit
  category, or the sling gesture below.
- **Click empty space (no drag)** -- clear the current selection.

Small, paler dots drifting around an object are its **grains** (see
section 11) -- optional granular-synthesis "sound dust", if enabled for
that object.

A small red/green/blue axis gizmo in the bottom-left corner shows the
current camera orientation (X/Y/Z, matching this project's own front/
left/up convention) -- handy after rotating the view to tell which way
is which. It's a fixed-size on-screen indicator, not a 3D object in the
scene, so it doesn't move or scale with zoom.

## 3. The object list (left sidebar)

At the top: the **Objects: N / M** count, then **+ Object** / **- Remove
Object** to change how many objects are active (up to the number of
input channels the plugin was given) -- both act on this list, so both
live here rather than in the toolbar. Removing an object resets its slot
to defaults -- reactivating it later starts fresh, not from wherever it
was left.

Below that: every active object gets a row -- **Object N** (numbered
from 1), plus an **M** (Mute) and **S** (Solo) button.

- Click a row to select that object, same as clicking it in the 3D view
  -- useful when an object is small, fast, or off-screen.
- **Mute (M)**: silences that object. Muting always wins -- a muted
  object stays silent even if it's also soloed.
- **Solo (S)**: when any object is soloed, every object that is *not*
  soloed goes silent, regardless of its own Mute state. Multiple objects
  can be soloed at once (non-exclusive solo, like a DAW mixer).

## 4. Motion modes

Every object is always in exactly one mode, shown in the parameter
panel's **Object -> Mode** dropdown:

| Mode | Behavior |
|---|---|
| **Static** | Sits still at its current position. |
| **Manual** | Being actively moved by the mouse (set automatically while dragging). |
| **Orbit** | Circles/ellipses around a center point or another object, kinematically (see section 6). |
| **Impulse** | Moves freely under velocity + forces (gravity/attraction/drag) -- what a throw or the Slingshot gesture puts an object into. |
| **Attracted** | Same physics as Impulse, but meant for objects that mainly react to *other* objects' attraction rather than being thrown. |

You can also set a mode directly from the dropdown, e.g. to park an
object in Static after it's drifted somewhere interesting.

## 5. The sling launch gesture

**Shift + click-and-drag** an object, then release, to launch it like a
slingshot/catapult: the launch direction and speed come from how far and
which way you pulled it back, not from your release velocity. While
pulling, a dashed preview line shows what release will actually do.

While the gesture is active (mouse still held down), three modifier keys
change *how* it launches:

- **Ctrl** -- cycles through three launch modes (tap repeatedly):
  - **Free Throw** (cyan line) -- a plain, ordinary throw. Normal drag
    and gravity apply afterward, same as releasing a regular
    click-drag throw.
  - **Orbit Shot** (violet line) -- instead of a straight throw, the
    object is placed directly into a scripted elliptical orbit. The
    ellipse's orientation follows the direction you pulled; its size
    follows how far you pulled.
  - **Slingshot** (yellow-green line) -- a **real, physics-based
    gravity assist**, not a scripted path. The object is thrown
    normally, but is also pulled by real gravity toward another chosen
    object (see Tab below) for as long as that pull is active. Whether
    it ends up deflected past the target (a flyby) or captured into a
    genuine orbit depends entirely on throw speed, angle, and the
    target's mass -- exactly like a real spacecraft gravity-assist
    maneuver, not something this mode dictates in advance. The
    preview line is a real forward simulation of the same physics, so
    what you see while aiming is what you get.
- **Alt** -- (Orbit Shot only) cycles the orbit's eccentricity: Circular
  -> light ellipse -> medium ellipse -> extreme ellipse.
- **Tab** -- cycles which point the current mode uses:
  - For **Orbit Shot**: which point the scripted ellipse is centered
    on -- "Center" (the world origin) or another active object (whose
    live, possibly moving position it then tracks).
  - For **Slingshot**: which object's gravity is pulling the thrown
    object. "Center" has no gravity here, so it just falls back to a
    plain, undeflected throw. Switching into Slingshot mode
    auto-selects the first other object for you if none is chosen yet.

A label near the cursor shows the current mode and target while you aim.

## 6. Orbit parameters (parameter panel -> Orbit category)

**Every control in this category only has an effect once the object's own
Mode is actually set to "Orbit"** (Object category -> Mode dropdown, or
double-click the object in the scene for a quick demo orbit). If Mode is
anything else, the panel shows a reminder of this directly in the Orbit
category instead of silently doing nothing.

For any object in Orbit mode, the parameter panel exposes:

- **Orbit Center (fixed point)** -- the point it orbits, if no reference
  object is set (see below).
- **Orbit Reference Object** -- orbit another object's *live* position
  instead of a fixed point (e.g. a moon around a planet). "Fixed"
  reverts to the plain center above.
- **Orbit Radius**, **Orbit Angular Speed (rad/s)** -- size and speed
  (sign sets direction, CW vs CCW).
- **Orbit Plane Normal** -- tilts the orbit plane out of the default
  horizontal (x/y) plane.
- **Orbit Eccentricity** -- 0 = circle, up to ~0.95 = a flat ellipse.
- **Orbit Decay (m/s)** -- steady, one-directional radius drift (spiral
  in/out over time). 0 = stable radius.
- **Radius Baseline / Radius Reversion Rate / Radius Noise Amplitude** --
  an alternative to Decay: a "living, breathing" orbit whose radius
  randomly wanders but keeps drifting back toward Baseline, instead of
  drifting away forever. Both 0 by default (off).
- **Radius Noise Smoothing (s)** -- 0 (default) leaves the random
  wander raw/jagged, exactly as Radius Noise Amplitude alone produces
  it. Raising this low-pass-filters the noise itself, turning a jittery
  wander into a smoother, more organic "breathing" motion -- higher
  values feel slower and gentler.

## 7. Object physics parameters (parameter panel -> Object category)

Apply to Impulse/Attracted motion (thrown or attracted objects):

- **Mass** -- affects how strongly the object is pulled by gravity/
  attraction sources, and, when this object is itself a source, how
  strongly it pulls others.
- **Gain** -- manual per-object volume, on top of distance attenuation.
- **Damping (simple decay)** -- fraction of velocity removed every
  update tick; simple "friction". 0 = none, 1 = stops instantly.
- **Max Velocity** -- hard speed cap. 0 = unlimited.
- **Drag Coefficient** -- a second, more physically realistic braking
  force proportional to current speed (air-resistance-like).
- **Restitution (wall bounce)** -- how elastic a bounce off the room
  boundary is (see section 9), 0 = velocity absorbed, 1 = perfectly
  elastic.
- **Stop Threshold** -- velocities below this are snapped to exactly 0,
  so damped motion actually comes to rest instead of crawling forever.

## 8. Attraction / gravity parameters (parameter panel -> Attraction category)

These make an object act as a gravity/attraction *source* that pulls (or
pushes) every other Impulse/Attracted object in the scene:

- **Strength** -- how strongly it attracts others; negative = repels
  instead.
- **Force Exponent** -- 2 = classic inverse-square gravity (the
  physical default); other values give a softer or sharper falloff.
- **Min. Distance (softening)** -- a floor on how close the distance
  used in the force calculation can get, preventing the force from
  spiking to absurd values at very close range.
- **Max. Range** -- beyond this distance, this source has no effect at
  all. 0 = unlimited range.
- **Pulse Rate / Pulse Depth** -- makes the attraction strength
  periodically swell and shrink (a "pulsing gravity well") instead of
  staying constant. 0 rate = off.

This is the same underlying gravity system the Slingshot sling-gesture
mode (section 5) uses for its one-off pull -- setting a real,
always-on Strength here creates a permanent gravity well any nearby
object will react to, not just a single thrown object.

## 9. Scene-wide parameters (parameter panel -> Scene category)

Apply to the whole scene, not one object:

- **Show Boundary** -- purely visual; the boundary still applies
  physically even if hidden. Sits above Boundary Size/Behavior below --
  the natural first question ("do I even see this?") before tuning the
  boundary itself.
- **Boundary Size** (formerly "Room Size") -- radius (meters) of an
  invisible spherical boundary around the origin. 0 disables it (objects
  can drift unbounded). Renamed since it's a physics boundary
  (reflect/wrap/absorb), not an acoustic "room" -- no reverb or
  reflection processing is tied to it.
- **Boundary Behavior** -- what happens when an object reaches that
  boundary: **Reflect** (bounces back, strength set by that object's
  Restitution), **Wrap** (reappears on the opposite side), or
  **Absorb** (stops there and goes silent).
- **Force Field (Wind/Gravity)** (formerly "Global Field") -- a constant
  force/mass applied to every Impulse/Attracted object, like a
  directional wind or gravity -- genuinely pushes objects around. Not
  the same thing as Propagation Wind (Acoustics category, below), which
  only affects sound, never movement -- renamed, and moved to a separate
  category from it, specifically to stop the two from reading as
  duplicates.
- **Time Scale** -- slows down (< 1) or speeds up (> 1) the whole
  simulation.

## 10. Acoustic simulation (parameter panel -> Acoustics category, and per-object Doppler)

Klangorbit doesn't just move objects visually -- distance, speed, and
direction genuinely affect the sound. Acoustics is its own category
(alongside Scene, both under "SCENE SETTINGS" -- both are scene-wide,
not tied to any object, they just cover different concerns: physical
boundary/force vs. the acoustic medium itself):

- **Speed of Sound (m/s)** -- affects propagation delay and how strong
  Doppler pitch shift is for a given movement speed. Deliberately
  independent of Temperature below; setting it far from the physical
  ~343 m/s (e.g. much lower) is a valid creative tool for exaggerated,
  surreal Doppler and delay effects, not just a "realism" slider.
- **Temperature / Relative Humidity / Atmospheric Pressure** -- feed a
  simplified air-absorption model (a gentle lowpass that gets stronger
  with distance); not ISO-9613-1 accurate, but directionally correct.
- **Propagation Wind (m/s)** (formerly "Wind") -- a directional vector
  that shifts the effective speed of sound (tailwind speeds up arrival,
  headwind slows/attenuates it) -- both a physically real effect and a
  distinct creative tool. Renamed, and moved here from the Scene
  category, to make clear it only affects *sound propagation*, never how
  objects actually move (that's Force Field, Scene category, above) --
  not moved into the (per-object) Doppler category below despite also
  being sound-related, since it's scene-wide, not tied to any one
  object.
- **Doppler Enabled** (per object, top of the Doppler category) -- on by
  default. A quick on/off switch that doesn't touch the Doppler Factor
  dial below it -- turning it back on restores whatever factor was
  actually set, instead of needing to remember and re-type a value that
  was overwritten to 0.
- **Doppler Factor** (per object, Doppler category) -- 0 = no pitch
  shift, 1 = physically correct, > 1 = exaggerated. The propagation
  delay itself always stays physically anchored to real distance
  regardless of this value -- it only scales the audible pitch-shift
  side effect.
- **Doppler Smoothing (s)** -- time constant smoothing sudden
  direction/speed changes (e.g. a wall bounce), avoiding pitch clicks.
- **Directivity Pattern** (Omni / Cardioid / Figure8) + **Source
  Orientation** -- makes an object "face" a direction and lets that
  affect its gain toward the listener, independent of its movement
  direction.

## 11. Grains (parameter panel -> Grains category)

An optional granular-synthesis layer per object: instead of (or in
addition to) hearing the object's own signal, it spawns many small,
independently-moving "grains" -- short, windowed bursts read from a
rolling buffer of that object's live input, each with its own tiny
trajectory. Turn it on with **Enabled**.

- **Isolate Grains** -- silences just this object's own dry/unGranulated
  signal, leaving its grains completely untouched. Unlike the object
  list's Mute button (which silences the object entirely, grains
  included), this lets you isolate and listen to only the grains on
  their own, independent of the underlying sound they're generated
  from.
- **Grain Rate (Spawn Rate) / Grain Duration** -- two independent
  controls, easy to mix up but not the same thing: Grain Rate
  (grains/sec) is how often a *new* grain starts; Grain Duration (s) is
  how long *each* grain plays (also its movement lifetime) once it has
  started. Raising Duration does not, by itself, slow down Grain Rate --
  a high rate and a long duration together mean MORE overlapping grains
  at once, not fewer new ones. Grains audibly overlap into a denser,
  more continuous texture whenever `Grain Rate x Grain Duration > 1`.
  Each has its own **Jitter** slider directly below it (0..1, random +/-
  variation per spawn) -- Grain Rate Jitter varies the spacing between
  spawns instead of a perfectly metronomic beat; Grain Duration Jitter
  varies each grain's own length.
- **Pitch Jitter** -- random per-grain playback-rate deviation.
- **Position Jitter In Buffer / Read Depth Min / Max / Distribution** --
  control how far back into the object's recent audio each grain reads
  from, and whether that's evenly spread, biased shallow (recent), or
  biased deep (older).
- **Max Concurrent Grains** -- a hard cap on how many of this object's
  grains may be alive simultaneously (there's also a global cap shared
  by all objects' clouds, so total CPU cost stays bounded either way;
  default 256, comfortably covering common Rate/Duration combinations).
  If `Grain Rate x Grain Duration` exceeds this cap,
  Klangorbit doesn't stall new spawns waiting for room -- it fades out
  the OLDEST currently-playing grain a little early (a quick, click-free
  10ms fade) and reuses its slot immediately, so new grains keep
  starting exactly on schedule. The trade-off: under heavy overlap, some
  individual grains end up a bit shorter than the Duration you set,
  rather than new grains simply failing to start on time.
- **Movement Mode** -- how each grain moves during its short life. The
  panel only shows the parameters that actually do something for
  whichever mode is currently selected (switching modes swaps the
  visible rows below instantly) -- each with its own **Jitter** slider
  directly below the relevant parameter (0..1, random +/- variation per
  spawn, all independently usable at once):
  - *Random Walk* -- smoothed random drift.
  - *Bounce* -- elastic reflection inside a small sphere around its
    spawn point (**Boundary Radius Jitter** varies that sphere's size).
  - *Radial Explosion* -- flies outward from the parent at spawn time
    (**Initial Speed Jitter** varies the launch speed).
  - *Orbit Around Parent* -- circles the parent's current position
    (**Orbit Radius Jitter** varies the orbit's size). **Orbit Sphere
    Spread** (0..1) reshapes the orbit itself: 0 (default) keeps every
    grain circling in the same flat horizontal plane, exactly like a
    classic 2D orbit; raising it toward 1 tilts each grain's own orbit
    plane by an increasingly random amount (picked once per grain at
    spawn), so over many grains and full rotations the swept-out shape
    grows from a flat disc into something approaching a full sphere
    around the object. In between gives a partial tilt/wobble.
  - *Attract/Repel Siblings* -- grains in the same cloud pull or push
    each other (gravity-like, same force law as section 8, scoped to
    just that cloud).
- **Doppler** -- optional, separate per-grain Doppler shift based on
  each grain's own velocity; when on, it's still scaled by the parent
  object's own Doppler Factor (Doppler category). Each grain has its
  own position/velocity, so the actual pitch shift is genuinely
  different per grain even though they share that one strength knob.

## 12. Output format (toolbar -> Output...)

Chooses what the plugin's output bus actually carries -- a plugin-wide
setting, not tied to the scene or any object, so it lives in its own
window (toolbar -> **Output...**) rather than the parameter panel:

- **Output Format** -- one of 14 mutually exclusive formats:
  - **Ambisonics (Order 1/2/3)** -- the original raw B-format output
    (ACN/SN3D), no decoding at all; route it to an external decoder
    (SPARTA AmbiBIN/AmbiDEC, IEM BinauralDecoder) as before.
  - **Stereo** -- a plain 2-speaker decode (+-30 deg), not the same
    thing as binaural -- no HRTF/head-related processing, just two
    virtual loudspeakers. The default output format (for every plugin
    format, not just AU) -- audible immediately, no external decoder or
    manual switch needed.
  - **Binaural** -- HRTF-based headphone output, decoded internally (no
    external AmbiBIN/BinauralDecoder needed for this path). Technique: the
    Ambisonics bus is decoded to a dense 50-point virtual speaker array
    (same point distribution AllRAD uses below), then each virtual
    speaker's signal is convolved through that direction's own measured
    left/right head-related impulse response and summed to the output --
    see `BinauralDecoder.h` for the full breakdown. Selecting this format
    reveals an **HRTF Dataset** picker in the same window:
    - **KEMAR** (default) -- Gardner & Martin, MIT Media Lab. Bundled.
    - **SADIE II -- D1 (KU100)** -- University of York. Bundled.
    - **Custom SOFA file...** -- import your own AES69/SOFA HRTF
      measurement via **Browse...**.

    Both bundled datasets require attribution -- see
    `THIRD_PARTY_LICENSES.md` in the project root. Switching datasets
    rebuilds the decoder immediately; expect a brief pause, longer for
    SADIE II. CPU cost has not been measured on real hardware -- watch the
    toolbar's CPU meter after switching. Interaural delay (fine timing
    differences between the ears) is not applied in this version, only
    each ear's amplitude/spectral HRIR shape -- see the CHANGELOG and
    `BinauralDecoder.h`'s own comment.
  - **Quad / 5.1 / 7.1** -- standard loudspeaker layouts, ITU-R
    BS.775-4 angles.
  - **5.1.2 / 5.1.4 / 7.1.2 / 7.1.4** -- Dolby-Atmos-bed-style layouts
    with height/top speakers, ITU-R BS.2051-2 angles.
  - **Octophonic** -- a fixed, named 8-speaker circular array (45 deg
    spacing, +-22.5 deg convention -- see the CHANGELOG for why).
  - **Circular Array** -- a generic circular array for any speaker
    count without an established naming convention; set **Circular
    Array: Speaker Count** (4-24) to match your actual setup. That
    control only has an effect while this format is selected --
    harmless otherwise.

  Octophonic and Circular Array are horizontal-only: a flat ring of
  speakers cannot reproduce elevation/height at all, regardless of
  decoder quality -- this is a property of the array itself, not a bug.
  Circular Array also loses some spatial precision (more blur, not
  wrong direction) below 7 speakers -- see the CHANGELOG for why.

  Every non-Ambisonics format is decoded internally (AllRAD for the
  irregular Quad/5.1/7.1/Atmos layouts, a simpler direct decode for
  Stereo/Octophonic/Circular Array -- see the CHANGELOG for the method
  and why it differs); there is no need to route to an external decoder
  plugin for any of these. Switching formats (or changing Circular
  Array's Speaker Count while it's active) changes the plugin's output
  channel count -- most hosts (Reaper confirmed) pick this up live, some
  need the plugin removed and reinserted, or the project reloaded, to
  fully apply it.
- **Bass Management (LFE from W)** -- off by default. Ambisonics has no
  dedicated LFE signal, so this is a real, audible addition when
  enabled: a low-passed (~120Hz) version of the omnidirectional (W)
  channel is sent to the LFE channel, for the formats that have one
  (5.1/7.1/Atmos variants -- Quad, Stereo, Binaural, Octophonic, and
  Circular Array have no LFE). Has no effect on any other format.

## 13. Presets

**Load Preset...** / **Save Preset...** in the toolbar save/load the
entire scene (every active object, its mode and all parameters, scene
settings, grains) as a `.json` file. Presets carry a schema
version and are validated on load -- an incompatible or corrupted file
is rejected with a clear error message instead of silently loading
wrong. (Output Format/Bass Management/Binaural HRTF Dataset are
plugin-instance state, not part of the scene the preset saves -- see
Presets/schema/README.md.)

## 14. Keyboard shortcuts summary

| Action | Shortcut |
|---|---|
| Select an object | Left-click it |
| Manual move / plain throw | Left-click + drag, release |
| Quick demo orbit around origin | Double-click an object |
| Sling launch gesture | Shift + click-drag, release |
| ...cycle Free Throw / Orbit Shot / Slingshot | Ctrl (while pulling) |
| ...cycle orbit ellipse shape | Alt (while pulling, Orbit Shot only) |
| ...cycle orbit/slingshot reference object | Tab (while pulling) |
| Orbit the camera | Drag empty space |
| Zoom the camera | Scroll wheel |
| Clear selection | Click empty space |

## 15. Tips for use with Reaper + SPARTA/IEM

1. Route Klangorbit's Ambisonics output (ACN/SN3D) into a matching
   decoder (SPARTA AmbiDEC for speakers, AmbiBIN for headphones; IEM's
   BinauralDecoder/AllRADecoder are also compatible) -- or switch Output
   Format to Binaural (section 12) to skip the external decoder for
   headphone monitoring.
2. Automate nothing in Reaper for movement -- the physics engine is the
   automation. Instead, perform gestures live (drag, throw, sling) while
   recording, or design a scene ahead of time and save it as a preset.
3. Start with 1-2 objects while getting a feel for the physics
   (damping, gravity, orbit) before scaling up -- CPU cost grows with
   active objects and grains.
4. Watch the **CPU** readout (toolbar, top-right, next to Mappings...)
   if you enable grains on
   many objects at once -- it's a real measured load, not an estimate, so
   amber/red means audible dropouts are actually likely, not just
   theoretical.

## 16. Gamepad control

Connect a game controller (Xbox/PlayStation-style, wired or Bluetooth --
macOS's GameController framework covers most modern ones). Every control
has a sensible built-in default -- no setup needed -- and every one of
them can still be rebound to something else via Learn mode (section 17):

- **Left stick**: rate-controls the selected object's position on the
  ground plane -- deflection sets its current velocity continuously;
  centering the stick (its own spring-back is enough) stops the object
  exactly where it is, immediately, with no drift and no snap-back.
  Touching the stick switches the selected object into Manual mode
  automatically (the same mode a mouse drag uses).
- **Right stick**: orbits the camera (look-around) -- the gamepad
  equivalent of dragging empty space with the mouse. Only does anything
  with the editor window open (there's nothing to look at otherwise).
- **D-pad Up/Down**: zooms the camera in/out. Also editor-only.
- **Button X**: cycles the selection to the next active object.
- **Button A**: activates the next inactive object slot and selects it
  (same as the "+ Object" button in the object list).
- **Button B**: deactivates the currently selected object (same as
  "- Remove Object" there).
- **Button Y / Left Shoulder / Right Shoulder (hold)**: aims a Free
  Throw / Orbit Shot / Slingshot -- while held, push the left stick the
  direction and strength you want to launch; releasing fires it. The
  gamepad equivalent of the mouse's Shift+drag sling gesture (section 5),
  just aimed by pushing the stick rather than pulling the mouse back.
  Orbit Shot always centers on the world origin; Slingshot always
  targets the first other active object -- use the mouse gesture instead
  if you want to choose a specific center/target.

All of the above (except camera look/zoom, which needs the editor open)
keeps working with the editor window closed, since the whole simulation
runs independent of it. Only one controller is read at a time. An
optional inertia mode for the left stick's movement (movement continues
after release, decelerating like a thrown object) exists but has no UI
toggle yet.

## 17. Controller mapping (toolbar -> Mappings...)

Bind any gamepad, MIDI, or OSC control to any parameter: pick a **Target
parameter** from the dropdown, press **Learn**, then move the
stick/trigger/button/MIDI knob/OSC control you want -- the next control
that changes gets bound automatically, the same way regardless of which
of the three it came from (see section 18 for MIDI/OSC specifics). The
list below shows every current binding, each with its own **Remove**.
**Load Profile...**/**Save Profile...** save the whole binding set (plus
the paging modifier) as its own file, completely separate from scene
presets.

The target-parameter dropdown only offers scene-wide parameters and
"whichever object is currently selected" parameters -- not one specific
object regardless of selection (possible by hand-editing a saved mapping
profile's JSON, but not through this picker).

**Paging**: hold the control shown next to "Paging modifier" (right
shoulder button by default) to unlock a second layer of bindings -- the
same stick can drive one parameter normally and a different one while
the modifier is held. Learn a binding while holding the modifier to
place it in that second layer.

Binding either of the left stick's axes here takes over the built-in
rate-control movement (section 16) completely for that axis pair --
the object it was moving just stays where it is.

## 18. MIDI / OSC control

MIDI and OSC controls are bindable through the same **Mappings...**
window and Learn mode as section 17 -- there's no separate MIDI-Learn or
OSC-Learn step.

**MIDI**: the plugin already accepts MIDI input, so anything your host
(or, in Standalone, macOS's own MIDI input selection) routes to it works
without extra setup here. Turning a knob/fader sends Control Change,
pressing a pad/key sends Note On/Off (captured as a button, not
velocity-sensitive), and moving a pitch strip sends Pitch Bend
(centered, bipolar) -- all three are bindable via Learn.

**OSC**: listens on UDP port **9000** by default (TouchOSC's own
default). Point an OSC control-surface app at this machine's IP on that
port and its controls become bindable the same way -- a control sending
a normalized 0..1 value binds as a continuous control, a bare
trigger/button message (no arguments) binds as a button.

There's no on-screen indicator yet for MIDI/OSC connection status, and
no UI yet to change the OSC port from its default.
