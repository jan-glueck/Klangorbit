# Klangorbit -- User Guide

Klangorbit is an object-based Ambisonics spatializer built around a live
physics engine: instead of automating position with envelopes or curves,
you give sound objects mass, velocity, gravity, and momentum, and let
their motion emerge from that. It outputs raw Ambisonics B-format
(ACN/SN3D, AmbiX-compatible) -- it does **not** decode to speakers or
headphones itself; feed its output into a decoder such as SPARTA
(AmbiBIN/AmbiDEC) or the IEM Plugin Suite.

This guide covers how to use the plugin. For build instructions and
internal architecture notes, see `README.md` in the project root.

---

## 1. Getting started

1. Insert Klangorbit on a track that has as many mono inputs routed to it
   as you want moving sound sources (each active object consumes one
   input channel).
2. Feed a decoder (SPARTA AmbiDEC/AmbiBIN, IEM BinauralDecoder, etc.)
   from Klangorbit's Ambisonics output.
3. Open the Klangorbit editor: you'll see a 3D scene view in the middle,
   an object list on the left, a toolbar across the top, and a parameter
   panel on the right.
4. Click **+ Object** to activate your first sound object -- it appears
   on a small reference circle around the origin.
5. Click and drag it around to hear it move in the decoded output.

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

## 3. The object list (left sidebar)

Every active object gets a row: **Object N**, plus an **M** (Mute) and
**S** (Solo) button.

- Click a row to select that object, same as clicking it in the 3D view
  -- useful when an object is small, fast, or off-screen.
- **Mute (M)**: silences that object. Muting always wins -- a muted
  object stays silent even if it's also soloed.
- **Solo (S)**: when any object is soloed, every object that is *not*
  soloed goes silent, regardless of its own Mute state. Multiple objects
  can be soloed at once (non-exclusive solo, like a DAW mixer).

Use **+ Object** / **- Remove Object** in the toolbar to change how many
objects are active (up to the number of input channels the plugin was
given). Removing an object resets its slot to defaults -- reactivating it
later starts fresh, not from wherever it was left.

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

- **Room Size** -- radius (meters) of an invisible spherical boundary
  around the origin. 0 disables it (objects can drift unbounded).
- **Boundary Behavior** -- what happens when an object reaches that
  boundary: **Reflect** (bounces back, strength set by that object's
  Restitution), **Wrap** (reappears on the opposite side), or
  **Absorb** (stops there and goes silent).
- **Show Boundary** -- purely visual; the boundary still applies
  physically even if hidden.
- **Global Field (Wind/Gravity)** -- a constant force/mass applied to
  every Impulse/Attracted object, like a directional wind or gravity.
- **Time Scale** -- slows down (< 1) or speeds up (> 1) the whole
  simulation.

## 10. Acoustic simulation (parameter panel -> Acoustics category, and per-object Doppler)

Klangorbit doesn't just move objects visually -- distance, speed, and
direction genuinely affect the sound:

- **Speed of Sound (m/s)** -- affects propagation delay and how strong
  Doppler pitch shift is for a given movement speed. Deliberately
  independent of Temperature below; setting it far from the physical
  ~343 m/s (e.g. much lower) is a valid creative tool for exaggerated,
  surreal Doppler and delay effects, not just a "realism" slider.
- **Temperature / Relative Humidity / Atmospheric Pressure** -- feed a
  simplified air-absorption model (a gentle lowpass that gets stronger
  with distance); not ISO-9613-1 accurate, but directionally correct.
- **Wind (m/s)** -- a directional vector that shifts the effective
  speed of sound (tailwind speeds up arrival, headwind slows/attenuates
  it) -- both a physically real effect and a distinct creative tool.
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

- **Mute Original Audio** -- silences just this object's own dry/
  unGranulated signal, leaving its grains completely untouched. Unlike
  the object list's Mute button (which silences the object entirely,
  grains included), this lets you isolate and listen to only the
  grains on their own, independent of the underlying sound they're
  generated from.
- **Grain Rate / Grain Duration** -- how often grains spawn, and how
  long each one plays (also its movement lifetime).
- **Pitch Jitter** -- random per-grain playback-rate deviation.
- **Position Jitter In Buffer / Read Depth Min / Max / Distribution** --
  control how far back into the object's recent audio each grain reads
  from, and whether that's evenly spread, biased shallow (recent), or
  biased deep (older).
- **Max Concurrent Grains** -- per-cloud cap (there's also a global cap
  shared by all clouds, so total CPU cost stays bounded).
- **Movement Mode** -- how each grain moves during its short life:
  - *Random Walk* -- smoothed random drift.
  - *Bounce* -- elastic reflection inside a small sphere around its
    spawn point.
  - *Radial Explosion* -- flies outward from the parent at spawn time.
  - *Orbit Around Parent* -- circles the parent's current position.
  - *Attract/Repel Siblings* -- grains in the same cloud pull or push
    each other (gravity-like, same force law as section 8, scoped to
    just that cloud).
- **Jitter Target / Jitter Range** -- randomizes one movement parameter
  (relevant to the current Movement Mode) per spawned grain.
- **Doppler** (grains) -- optional, separate per-grain Doppler
  shift based on each grain's own velocity; when on, it's still scaled
  by the parent object's own Doppler Factor above.

## 12. Presets

**Load Preset...** / **Save Preset...** in the toolbar save/load the
entire scene (every active object, its mode and all parameters, scene
settings, grains) as a `.json` file. Presets carry a schema
version and are validated on load -- an incompatible or corrupted file
is rejected with a clear error message instead of silently loading
wrong.

## 13. Keyboard shortcuts summary

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

## 14. Tips for use with Reaper + SPARTA/IEM

1. Route Klangorbit's Ambisonics output (ACN/SN3D) into a matching
   decoder (SPARTA AmbiDEC for speakers, AmbiBIN for headphones; IEM's
   BinauralDecoder/AllRADecoder are also compatible).
2. Automate nothing in Reaper for movement -- the physics engine is the
   automation. Instead, perform gestures live (drag, throw, sling) while
   recording, or design a scene ahead of time and save it as a preset.
3. Start with 1-2 objects while getting a feel for the physics
   (damping, gravity, orbit) before scaling up -- CPU cost grows with
   active objects and grains.
4. Watch the **CPU** readout in the toolbar if you enable grains on
   many objects at once -- it's a real measured load, not an estimate, so
   amber/red means audible dropouts are actually likely, not just
   theoretical.
