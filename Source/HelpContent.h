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
position by hand. Output format is selectable (toolbar -> Output...,
section 11): an internally decoded Stereo (the default), Binaural (HRTF
headphone), Quad, Octophonic, Circular Array, 5.1, 7.1, or one of four
Dolby-Atmos-bed layouts, no external decoder needed -- or raw Ambisonics
B-format (1st through 5th order, ACN/SN3D, AmbiX-compatible) to feed a
decoder such as SPARTA AmbiDEC/AmbiBIN or the IEM Plugin Suite instead.

A fuller version of this guide lives in Docs/UserGuide.md in the
project folder.


== 1. SCENE VIEW ==

The 3D view looks down at the origin (the listener) by default. Orbiting
or zooming this camera is just your viewpoint -- it never affects the
actual spatial audio.

  Drag empty space           orbit the camera
  Scroll wheel                zoom the camera
  Click an object              select it (parameters -> right panel)
  Click + drag an object       move it live by hand (ground plane, X/Y)
  Alt + drag an object         move it in height (Z) instead of X/Y
  Release while dragging       keeps the implied velocity, throws it
                                 (disable per object via Object ->
                                 "Momentum" for plain pick-and-place panning)
  Double-click an object       quick demo: start/stop a circular orbit
  Click empty space (no drag)  clear selection

Small pale dots drifting around an object are its optional grains
(section 10).

A small red/green/blue axis gizmo, bottom-left corner, shows the
current camera orientation (X/Y/Z) -- a fixed-size on-screen indicator,
not a 3D object in the scene.

Double-clicking any slider in the parameter panel resets it to its
default value (every slider, every category). "Mappings..."/"Output..."
each open their own small window (top-right, next to "?") rather than
living in the parameter panel.


== 2. OBJECT LIST (left sidebar) ==

Top: "Objects: N / M" count, then "+ Object" / "- Remove Object" to
change how many objects are active. Removing one resets its slot to
defaults.

Below: one row per active object: "Object N" (numbered from 1) plus M
(Mute) and S (Solo).

  - Click a row to select that object, same as clicking it in 3D.
  - Mute always wins: a muted object stays silent even if soloed.
  - Solo is non-exclusive: with any object soloed, every NON-soloed
    object goes silent, but multiple objects can be soloed at once.


== 3. MOTION MODES (parameter panel -> Object -> Mode) ==

  Static      sits still
  Manual      being actively dragged by the mouse (automatic)
  Orbit       circles/ellipses around a point or another object
  Impulse     moves freely under velocity + forces -- thrown objects,
              or objects reacting to another object's attraction


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

EVERY control below only has an effect once the object's own Mode
(Object category) is actually set to "Orbit" -- otherwise it's shown
right in the panel as a reminder instead of silently doing nothing.

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
  Radius Noise Smoothing (s)    0 = raw/jagged wander (default); higher
                                  low-pass-filters the noise itself for
                                  a smoother, more "breathing" motion


== 6. OBJECT PHYSICS (panel -> Object) ==

  Momentum              default ON -- turn off for plain pick-and-place
                         panning: releasing a mouse drag then always
                         leaves THIS object exactly where the mouse was,
                         never throws it. Only affects plain drag, not
                         the sling gesture or gamepad throws.
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
other Impulse-mode object in the scene:

  Strength                pull strength; negative = repel
  Force Exponent           2 = classic inverse-square gravity
  Min. Distance             softening floor, avoids force spikes up close
  Max. Range                beyond this distance, no effect (0 = unlimited)
  Pulse Rate / Pulse Depth  makes the strength swell/shrink periodically

This is the same gravity system the Slingshot sling-gesture mode uses
for its one-off pull -- a real, always-on Strength here creates a
permanent gravity well, not just a single thrown object's temporary one.


== 8. SCENE-WIDE PARAMETERS (panel -> Scene) ==

  Show Boundary          purely visual toggle -- sits above Boundary
                         Size/Behavior below (see first)
  Boundary Size           radius (m) of an invisible spherical boundary;
   (was "Room Size")       0 disables it -- physics boundary, not an
                          acoustic "room" (no reverb tied to it)
  Boundary Behavior      Reflect (bounce, via Restitution) / Wrap
                         (reappear opposite side) / Absorb (stop+silence)
  Force Field             constant force/mass on every Impulse-mode
   (Wind/Gravity,          object, like directional wind or gravity --
   was "Global Field")     genuinely moves objects (unlike Propagation
                          Wind below, which only affects sound)
  Time Scale              slow-motion (< 1) or fast-forward (> 1)


== 9. ACOUSTIC SIMULATION (panel -> Acoustics, + per-object Doppler) ==

Distance, speed, and direction genuinely affect the sound, not just
its position. Acoustics is its own category, next to Scene (both under
the "SCENE SETTINGS" group -- both scene-wide, just different concerns:
physical boundary/force vs. the acoustic medium):

  Speed of Sound (m/s)   propagation delay + Doppler strength; going
                          far from the physical ~343 is a valid
                          creative tool for exaggerated effects
  Temperature/Humidity/   feed a simplified air-absorption lowpass
   Atmospheric Pressure    (not ISO-9613-1 accurate, but directional)
  Propagation Wind (m/s)  shifts effective speed of sound directionally
   (was "Wind", now in     (tailwind speeds up, headwind slows/
   Acoustics not Scene)    attenuates) -- sound only, never affects how
                          objects actually move (see Force Field, Scene
                          category, above)
  Doppler Enabled          on by default; quick on/off switch that
   (Doppler category)      doesn't touch the Doppler Factor dial below --
                          re-enabling restores whatever factor was set
  Doppler Factor           0 = off, 1 = physical, > 1 = exaggerated;
   (Doppler category)      delay itself always stays physically anchored
  Doppler Smoothing (s)    smooths sudden direction/speed changes
  Directivity Pattern +    Omni/Cardioid/Figure8 "facing" direction,
   Source Orientation      independent of movement direction


== 10. GRAINS (panel -> Grains) ==

Optional granular layer per object: spawns many small, independently
moving "grains", short windowed bursts read from a rolling buffer of
that object's live input. Turn on with Enabled.

  Isolate Grains                 silences just this object's OWN dry
                                  signal, leaving its grains untouched --
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
  Pitch Jitter                   how far each grain's pitch may deviate
                                  from the source material's own pitch --
                                  the REACH, shared by both Pitch Jitter
                                  Mode settings below (0 = no deviation).
  Pitch Jitter Mode              Random (default): a continuous, uniformly
                                  random +/- playback-rate deviation, the
                                  original behavior.
                                  Scale: quantizes instead -- each grain's
                                  pitch snaps exactly onto a degree of the
                                  Pitch Quantize Scale below, picked at
                                  random from every degree within Pitch
                                  Jitter's reach (up to one octave up/down
                                  at Pitch Jitter = 1.0). Use this to keep
                                  a jittered grain cloud musically "in
                                  tune" with its own source instead of
                                  detuning arbitrarily.
  Pitch Quantize Scale           Only shown while Pitch Jitter Mode ==
                                  Scale. The grain's own natural pitch is
                                  always the scale's root (no separate
                                  key/note picker -- these are intervals
                                  relative to whatever the source material
                                  already sounds like, not absolute
                                  pitches). Octaves/Fifths are bare
                                  intervals rather than full scales
                                  (Octaves alone removes all pitch
                                  variation except octave jumps). Acoustic
                                  Scale is the "overtone scale" (Lydian
                                  Dominant) -- the closest standard
                                  12-tone-equal-tempered scale to the
                                  natural overtone series (the real,
                                  inharmonic harmonic series has no exact
                                  equal-tempered equivalent, so this is a
                                  practical approximation, tune grain
                                  pitch jitter toward it with this option).
  Position Jitter / Read Depth   how far back into recent audio a grain
   Min/Max/Distribution           reads from, and the depth distribution
  Max Concurrent Grains          hard cap on simultaneously alive grains
                                  (there's also a global cap across all
                                  objects; default 256). If Grain Rate x
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
                                  The panel only shows the parameters
                                  that apply to whichever mode is
                                  currently selected -- switching modes
                                  swaps the visible rows below instantly.
                                  Bounce, Radial Explosion, and Orbit
                                  Around Parent each have their own
                                  Jitter slider under their size/speed
                                  parameter (Boundary Radius, Initial
                                  Speed, Orbit Radius) -- all usable at
                                  once, not mutually exclusive. Orbit
                                  Around Parent also has Orbit Sphere
                                  Spread (0..1): 0 = flat, every grain
                                  circles in the same horizontal plane
                                  (default); 1 = each grain's own orbit
                                  plane is essentially random, so over
                                  many grains/rotations the shape
                                  approaches a sphere instead of a disc.
  Doppler                        optional per-grain Doppler, still scaled
                                  by the parent object's own Doppler
                                  Factor -- but each grain has its own
                                  position/velocity, so the actual pitch
                                  shift genuinely differs per grain.


== 11. OUTPUT FORMAT (toolbar -> Output...) ==

Plugin-wide, not tied to the scene or any object -- has its own window:

  Output Format          one of 16 mutually exclusive formats, listed in
                          dropdown order:

                           Stereo -- plain 2-speaker decode at +-30deg,
                            NOT binaural (no HRTF). The default.
                           Binaural (HRTF) -- HRTF-based headphone
                            output (a dense virtual speaker array, each
                            convolved through a measured head-related
                            impulse response). Own HRTF Dataset picker
                            appears below when selected -- see "Binaural
                            HRTF Dataset" further down.
                           Quad -- conventional quadraphonic: L/R
                            +-45deg, Ls/Rs +-135deg. Not an ITU
                            standard -- the conventional consumer
                            layout.
                           Octophonic -- fixed, named 8-speaker circular
                            array: front L/R +-22.5deg, front-side L/R
                            +-67.5deg, rear-side L/R +-112.5deg, rear
                            L/R +-157.5deg. No ITU/IEM/AllRAD standard
                            found for this exact layout -- matches Blue
                            Ripple Sound's "O3A Decoder -- Octagon" (the
                            one concrete Ambisonics-ecosystem reference
                            found); channel order is this project's own
                            choice.
                           Circular Array -- generic circular array, any
                            speaker count 4-24 (own slider below, only
                            shown while this format is selected),
                            evenly spaced starting at front. No
                            standard -- pure geometry.
                           5.1 -- ITU-R BS.775-4: L/R +-30deg, C 0deg,
                            Ls/Rs +-110deg. Channel order L R C LFE
                            Ls Rs.
                           7.1 -- ITU-R BS.775-4: L/R +-30deg, C 0deg,
                            side Lss/Rss +-90deg, rear Lrs/Rrs +-135deg
                            (both within BS.775-4's permitted sectors --
                            side 90-110deg, rear 135-150deg). Channel
                            order L R C LFE Lss Rss Lrs Rrs.
                           5.1.2 (Atmos) -- 5.1 bed (above) + top-side
                            L/R, 8ch total.
                           5.1.4 (Atmos) -- 5.1 bed + top-front L/R +
                            top-rear L/R, 10ch total.
                           7.1.2 (Atmos) -- 7.1 bed (above) + top-side
                            L/R, 10ch total.
                           7.1.4 (Atmos) -- 7.1 bed + top-front L/R +
                            top-rear L/R, 12ch total -- the largest
                            non-Ambisonics format.
                            Height angles for all four (top-front
                            +-45deg/+45deg elevation, top-rear
                            +-135deg/+45deg elevation, top-side
                            +-90deg/+45deg elevation): ITU-R BS.2051-2
                            only defines permitted ANGLE SECTORS for
                            height speakers (e.g. top-front anywhere in
                            azimuth +-30..45deg / elevation +30..55deg),
                            not single fixed values -- the angles used
                            are round numbers within those sectors,
                            cross-checked against Dolby's own published
                            consumer height-speaker guidance (45deg
                            front / 135deg rear, 45deg elevation cited
                            as "ideal").
                           1st/2nd/3rd Order Ambisonics (4ch/9ch/16ch)
                            -- raw B-format, no decoding, ACN/SN3D.
                           4th/5th Order Ambisonics (25ch/36ch) -- same,
                            raw B-format, no decoding, ACN/SN3D. Higher
                            precision for external decoding only --
                            nothing in this plugin's own decode paths
                            uses either.

                          Octophonic/Circular Array are HORIZONTAL ONLY
                          -- a flat speaker ring cannot reproduce
                          elevation, regardless of decoder quality.
                          Every non-Ambisonics format except Stereo now
                          pans each object DIRECTLY to the real speakers
                          (VBAP) instead of decoding a shared Ambisonics
                          bus -- sharper localization, no external
                          decoder plugin needed. Every Output Format is
                          always available (fixed 36-channel output bus)
                          in VST3/Standalone AND in AU hosted by anything
                          other than Logic; routing those channels
                          somewhere audible is the host's own job. In
                          Logic specifically, availability is still
                          bounded by which track type Klangorbit was
                          inserted on.
  Circular Array:         4-24 -- only shown while Output Format is
   Speaker Count           "Circular Array".
  Bass Management         only shown for formats with an LFE channel
   (LFE from W)            (5.1/7.1/5.1.2/5.1.4/7.1.2/7.1.4). Off by
                          default -- when on, sends a low-passed
                          (~120Hz) version of the omnidirectional (W)
                          channel to the LFE channel.
  Binaural HRTF Dataset   only shown while Output Format is "Binaural
                          (HRTF)":
                           KEMAR -- MIT Media Lab, bundled, default
                           SADIE II D1 (KU100) -- University of York,
                            bundled, an alternative measured head
                           KU100 2deg Grid (TH Koeln/Bernschuetz) --
                            bundled, the densest grid (16020 points,
                            full sphere); a different KU100 measurement
                            from SADIE II D1 above (same dummy head
                            model, different lab/grid)
                           Custom SOFA file... -- import your own
                            AES69/SOFA-format HRTF measurement via
                            "Browse..."
                          Switching datasets rebuilds the decoder --
                          expect a brief pause, longer for SADIE II/KU100.
                          See THIRD_PARTY_LICENSES.md for the required
                          attribution for both bundled datasets.


== 12. PRESETS ==

"Load Preset..." / "Save Preset..." in the toolbar save/load the whole
scene (every object, mode, all parameters, scene settings, grain
clouds) as a .json file. Presets are schema-versioned and validated on
load -- an incompatible/corrupted file is rejected with a clear error
instead of silently loading wrong. (Output Format/Bass Management/
Binaural HRTF Dataset are plugin-instance state, not part of what a
preset saves.)


== 13. KEYBOARD SHORTCUTS ==

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


== 14. GAMEPAD CONTROL ==

Connect a game controller -- every control has a sensible built-in
default already, no setup needed (all still rebindable via Learn mode,
see the next section):

  Left stick                  Rate-controls the selected object's
                               position -- deflection sets velocity
                               continuously, centering the stick stops
                               it exactly where it is (no drift/snap-
                               back). Touching it switches that object
                               into Manual mode.
  Right stick                 Orbit the camera (look-around). Editor
                               only -- nothing to look at otherwise.
  D-pad Up/Down                Zoom the camera in/out. Editor only.
  D-pad Left/Right              Cycle the selection to the next/previous
                               active object (bidirectional).
  Button A                     Activate the next inactive object slot
                               and select it (same as "+ Object").
  Button B                     Deactivate the selected object (same as
                               "- Remove Object").
  Y / Left Shoulder /          Hold + push the left stick to aim a Free
  Right Shoulder (hold)        Throw / Orbit Shot / Slingshot; release
                               to fire. Gamepad equivalent of the
                               mouse's Shift+drag sling gesture, aimed
                               by pushing the stick instead of pulling
                               the mouse back. Orbit Shot always
                               centers on the world origin; Slingshot
                               always targets the first other active
                               object -- use the mouse gesture for a
                               specific center/target.

Only one controller read at a time. Everything above except camera
look/zoom keeps working with the editor closed, since the whole
simulation runs independent of it. An optional inertia mode for the
left stick's movement exists (movement continues after release,
decelerating like a thrown object) but has no UI toggle yet. Button X
has no built-in behavior -- free for a Learn-mode binding of your own,
same as any other raw control. The Learn-mode "second bank" modifier
(hold to access an alternate set of bindings, see the next section)
defaults to Left Trigger, not a shoulder button, so it never competes
with Orbit Shot/Slingshot above.


== 15. CONTROLLER MAPPING (toolbar -> Mappings...) ==

Bind any gamepad, MIDI, or OSC control to any parameter: pick a Target
parameter, press Learn, then move the stick/trigger/button/MIDI knob/
OSC control you want -- the next control that changes gets bound
automatically, the same way for all three (see next section for MIDI/
OSC specifics). The list shows every current binding (each with its own
Remove). Load Profile.../Save Profile... save the whole binding set as
its own file, separate from scene presets.

Target picker only offers scene-wide and "whichever object is
currently selected" parameters, not one specific object regardless of
selection (possible by hand-editing a saved profile's JSON only).

Paging: hold the shown modifier control (left trigger by default) to
unlock a second layer of bindings -- same stick, different parameter
while held. Learn while holding it to place a binding in that layer.

Binding either left-stick axis here takes over the built-in rate-
control movement completely for that axis pair.


== 16. MIDI / OSC CONTROL ==

MIDI and OSC controls bind through the same Mappings window and Learn
mode as the previous section -- no separate MIDI-Learn or OSC-Learn
step.

MIDI: the plugin already accepts MIDI input, so anything your host (or,
in Standalone, macOS's own MIDI input selection) routes to it works
without extra setup. A knob/fader sends Control Change, a pad/key sends
Note On/Off (captured as a button, not velocity-sensitive), a pitch
strip sends Pitch Bend (centered, bipolar) -- all bindable via Learn.

OSC: listens on UDP port 9000 by default (TouchOSC's own default).
Point an OSC control-surface app at this machine's IP on that port and
its controls become bindable the same way -- a control sending a
normalized 0..1 value binds as a continuous control, a bare trigger/
button message (no arguments) binds as a button.

No on-screen indicator yet for MIDI/OSC connection status, and no UI
yet to change the OSC port from its default.


== 17. DAW AUTOMATION ==

Separate from -- and alongside -- the gamepad/MIDI/OSC Learn-mode
mapping above: every field exposed there is ALSO a real, host-
automatable parameter, in AU, VST3, and Standalone alike. Draw/record
automation for it directly in the host's own automation lanes -- 525
parameters total, grouped per object ("Object 1".."Object 8", each
subgrouped by category: Object Physics/Attraction/Orbit/Doppler/Grain
Cloud) plus one "Global" group for scene-wide parameters.

  What's automatable      exactly the fields Learn mode already exposes
                           for a SPECIFIC object slot or globally
  What's NOT automatable  "whichever object is selected" bindings (no
                           fixed target for a host lane), enum settings
                           (Movement Mode, Output Format, etc.), most
                           runtime physics state (velocity, orbit phase).
                           Position (X/Y/Z) IS automatable -- writing it
                           switches the object into Manual mode, same as
                           a mouse drag
  Manual tweaks            mouse/gamepad/MIDI/OSC changes are NOT
                           themselves recorded as host automation --
                           only automation the host plays back or
                           writes shows up
  Session save/reload      now actually preserves the whole scene
                           (previously did not)
)HELPTEXT";
}
