#pragma once

/**
    Plain-text help content shown by HelpWindow (opened from the toolbar's
    "?" button in KlangorbitEditor) -- a condensed, in-app copy of
    Docs/UserGuide.md, kept in its own header so it doesn't clutter
    PluginEditor.cpp. Plain text (no Markdown), since it's rendered in a
    plain juce::TextEditor, not a Markdown/HTML view -- see HelpWindow.h.

    Update this alongside Docs/UserGuide.md when functionality/parameters
    change; they intentionally cover the same material for two different
    audiences (this one is the quick, always-available in-app reference,
    the Markdown file is the fuller standalone manual).
*/
namespace HelpContent
{
    inline const char* text = R"HELPTEXT(KLANGORBIT -- HELP

Klangorbit is an object-based Ambisonics spatializer driven by a live
physics engine: give sound objects mass, velocity, gravity, and
momentum, and let their motion emerge from that instead of automating
position by hand. Output is raw Ambisonics B-format (ACN/SN3D,
AmbiX-compatible) -- feed it into a decoder such as SPARTA (AmbiDEC/
AmbiBIN) or the IEM Plugin Suite; Klangorbit itself does not decode to
speakers/headphones.

A fuller version of this guide lives in Docs/UserGuide.md in the
project folder.


== 1. SCENE VIEW ==

The 3D view looks down at the origin (the listener) by default. Orbiting
or zooming this camera is just your viewpoint -- it never affects the
actual spatial audio.

  Drag empty space           orbit the camera
  Scroll wheel                zoom the camera
  Click an object              select it (parameters -> right panel)
  Click + drag an object       move it live by hand
  Release while dragging       keeps the implied velocity, throws it
  Double-click an object       quick demo: start/stop a circular orbit
  Click empty space (no drag)  clear selection

Small pale dots drifting around an object are its optional grains
(section 10).


== 2. OBJECT LIST (left sidebar) ==

One row per active object: "Object N" plus M (Mute) and S (Solo).

  - Click a row to select that object, same as clicking it in 3D.
  - Mute always wins: a muted object stays silent even if soloed.
  - Solo is non-exclusive: with any object soloed, every NON-soloed
    object goes silent, but multiple objects can be soloed at once.

"+ Object" / "- Remove Object" in the toolbar change how many objects
are active. Removing one resets its slot to defaults.


== 3. MOTION MODES (parameter panel -> Object -> Mode) ==

  Static      sits still
  Manual      being actively dragged by the mouse (automatic)
  Orbit       circles/ellipses around a point or another object
  Impulse     moves freely under velocity + forces (thrown objects)
  Attracted   same physics as Impulse, for objects mainly reacting
              to other objects' attraction rather than being thrown


== 4. SLING LAUNCH GESTURE ==

Shift + click-drag an object, then release, to launch it like a
catapult: direction/speed come from how far and which way you pulled
it, not your release velocity. A dashed preview line shows what
release will actually do.

While still holding the mouse down:

  Ctrl  cycles the launch mode (tap repeatedly):
        - Free Throw (cyan)       a plain ordinary throw
        - Orbit Shot (violet)     placed directly into a SCRIPTED
                                   elliptical orbit; shape from Alt,
                                   orientation/size from the drag
        - Slingshot (yellow-green) a REAL, physics-based gravity
                                   assist -- thrown normally, then
                                   pulled by real gravity toward the
                                   Tab-selected object. Flyby or
                                   captured orbit is an emergent
                                   result of speed/angle/target mass,
                                   not a scripted outcome. The preview
                                   is a real forward simulation, so
                                   what you see is what you get.

  Alt   (Orbit Shot only) cycles eccentricity: Circular -> light ->
        medium -> extreme ellipse.

  Tab   cycles the reference point/object:
        - Orbit Shot: what the scripted ellipse centers on -- "Center"
          (world origin) or another object's LIVE position.
        - Slingshot: which object's gravity pulls the thrown object.
          "Center" means no pull (falls back to a plain throw).
          Switching into Slingshot auto-picks the first object if
          none is chosen yet.

A label near the cursor shows the current mode/target while aiming.


== 5. ORBIT PARAMETERS (panel -> Orbit) ==

  Orbit Center (fixed point)    point orbited if no reference object
  Orbit Reference Object        orbit another object's LIVE position
                                  instead ("Fixed" = use the point above)
  Orbit Radius / Angular Speed  size / speed (sign = direction)
  Orbit Plane Normal            tilts the orbit out of the x/y plane
  Orbit Eccentricity            0 = circle, up to ~0.95 = flat ellipse
  Orbit Decay (m/s)             steady one-directional radius drift
  Radius Baseline/Reversion     ALTERNATIVE to Decay: radius randomly
   Rate/Noise Amplitude          wanders but keeps drifting back to
                                  Baseline, instead of drifting away


== 6. OBJECT PHYSICS (panel -> Object) ==

  Mass                 how strongly gravity/attraction pulls it, and
                         (as a source) how strongly IT pulls others
  Gain                  manual per-object volume
  Damping                fraction of velocity removed per tick (friction)
  Max Velocity           hard speed cap (0 = unlimited)
  Drag Coefficient       speed-proportional braking force (air-like)
  Restitution            elasticity of a room-boundary bounce
  Stop Threshold          velocities below this snap to exactly 0


== 7. ATTRACTION / GRAVITY (panel -> Attraction) ==

Makes an object act as a gravity SOURCE pulling (or pushing) every
other Impulse/Attracted object in the scene:

  Strength                pull strength; negative = repel
  Force Exponent           2 = classic inverse-square gravity
  Min. Distance             softening floor, avoids force spikes up close
  Max. Range                beyond this distance, no effect (0 = unlimited)
  Pulse Rate / Pulse Depth  makes the strength swell/shrink periodically

This is the same gravity system the Slingshot sling-gesture mode uses
for its one-off pull -- a real, always-on Strength here creates a
permanent gravity well, not just a single thrown object's temporary one.


== 8. SCENE-WIDE PARAMETERS (panel -> Scene) ==

  Room Size            radius (m) of an invisible spherical boundary;
                         0 disables it
  Boundary Behavior      Reflect (bounce, via Restitution) / Wrap
                         (reappear opposite side) / Absorb (stop+silence)
  Show Boundary          purely visual toggle
  Global Field           constant force/mass on every Impulse/Attracted
   (Wind/Gravity)         object, like directional wind or gravity
  Time Scale              slow-motion (< 1) or fast-forward (> 1)


== 9. ACOUSTIC SIMULATION (panel -> Acoustics, + per-object Doppler) ==

Distance, speed, and direction genuinely affect the sound, not just
its position:

  Speed of Sound (m/s)   propagation delay + Doppler strength; going
                          far from the physical ~343 is a valid
                          creative tool for exaggerated effects
  Temperature/Humidity/   feed a simplified air-absorption lowpass
   Atmospheric Pressure    (not ISO-9613-1 accurate, but directional)
  Wind (m/s)               shifts effective speed of sound directionally
                          (tailwind speeds up, headwind slows/attenuates)
  Doppler Factor           0 = off, 1 = physical, > 1 = exaggerated;
   (Doppler category)      delay itself always stays physically anchored
  Doppler Smoothing (s)    smooths sudden direction/speed changes
  Directivity Pattern +    Omni/Cardioid/Figure8 "facing" direction,
   Source Orientation      independent of movement direction


== 10. GRAINS (panel -> Grains) ==

Optional granular layer per object: spawns many small, independently
moving "grains", short windowed bursts read from a rolling buffer of
that object's live input. Turn on with Enabled.

  Grains Only                    silences just this object's OWN dry
   (Mute Original Audio)          signal, leaving its grains untouched --
                                  isolates the grains from the source
                                  they're generated from. Independent of
                                  the object list's Mute (which silences
                                  both source and grains together).
  Grain Rate (Spawn Rate) /      two INDEPENDENT controls: Grain Rate is
   Grain Duration                 how often a NEW grain starts, Duration
                                  is how long EACH one then plays. Raising
                                  Duration does not slow down Grain Rate --
                                  both together just means more grains
                                  overlapping at once (audible whenever
                                  Grain Rate x Duration > 1). Each has its
                                  own Jitter slider right below it.
  Pitch Jitter                   random per-grain playback-rate deviation
  Position Jitter / Read Depth   how far back into recent audio a grain
   Min/Max/Distribution           reads from, and the depth distribution
  Max Concurrent Grains          hard cap on simultaneously alive grains
                                  (there's also a global cap across all
                                  objects; default 32). If Grain Rate x
                                  Duration exceeds this, new grains don't
                                  stall -- the OLDEST active grain gets a
                                  quick (10ms) fade-out and its slot is
                                  reused immediately, so new grains keep
                                  starting on schedule. Trade-off: some
                                  grains may then end up shorter than
                                  the configured Duration.
  Movement Mode                  Random Walk / Bounce / Radial Explosion /
                                  Orbit Around Parent / Attract-Repel
                                  Siblings (gravity within the cloud only).
                                  Bounce, Radial Explosion, and Orbit
                                  Around Parent each have their own
                                  Jitter slider under their size/speed
                                  parameter (Boundary Radius, Initial
                                  Speed, Orbit Radius) -- all usable at
                                  once, not mutually exclusive.
  Doppler (grains)                optional per-grain Doppler, still scaled
                                  by the parent object's own Doppler Factor


== 11. PRESETS ==

"Load Preset..." / "Save Preset..." in the toolbar save/load the whole
scene (every object, mode, all parameters, scene settings, grain
clouds) as a .json file. Presets are schema-versioned and validated on
load -- an incompatible/corrupted file is rejected with a clear error
instead of silently loading wrong.


== 12. KEYBOARD SHORTCUTS ==

  Select an object                    left-click it
  Manual move / plain throw            left-click + drag, release
  Quick demo orbit around origin        double-click an object
  Sling launch gesture                  Shift + click-drag, release
  ...cycle Free Throw/Orbit/Slingshot   Ctrl (while pulling)
  ...cycle orbit ellipse shape          Alt (while pulling, Orbit Shot)
  ...cycle orbit/slingshot target       Tab (while pulling)
  Orbit the camera                      drag empty space
  Zoom the camera                       scroll wheel
  Clear selection                       click empty space
)HELPTEXT";
}
