# MediaPipe hand-tracking bridge

Webcam-based hand tracking for Klangorbit, via [MediaPipe](https://github.com/google-ai-edge/mediapipe)'s
Hand Landmarker -- chosen over Apple's Vision framework specifically
because this project also targets Windows (see the main README's "Windows
build" section), and Vision is macOS/iOS-exclusive while MediaPipe runs
identically on Windows, macOS, and Linux.

## Why this is a separate Python process, not C++ inside the plugin

This was a real architecture decision, not the default/obvious choice --
recorded here so it isn't silently revisited later without the reasoning
that led to it:

- **Google does not officially support MediaPipe's C++ Tasks API.** The
  current Hand Landmarker documentation
  ([developers.google.com](https://developers.google.com/edge/mediapipe/solutions/vision/hand_landmarker))
  lists Android, Python, and Web as the supported platforms -- C++ isn't
  one of them. The C++ layer exists (it's what those bindings are built
  on internally) but isn't a documented, stable, end-user API.
- **MediaPipe's build system is Bazel, not CMake**, and there's no
  official prebuilt C++ library to link against. Getting a working
  MediaPipe C++ build into this project's CMake/JUCE build would mean
  either building the whole framework via Bazel ourselves (no official
  guarantee this even works cleanly on Windows) or adopting a community
  C-API wrapper -- the most relevant one,
  [cpvrlab/libmediapipe](https://github.com/cpvrlab/libmediapipe), does
  have ready-made Windows and macOS build scripts and is conveniently
  GPL-3.0 (matching this project's own license), but its last real commit
  was April 2023 -- over two years unmaintained, no releases, 8 open
  issues. Adopting it would mean effectively becoming its maintainer.
- **MediaPipe's Python distribution, by contrast, is first-class**:
  `pip install mediapipe` gets you prebuilt wheels on Windows, macOS, and
  Linux, zero Bazel involved, officially supported and documented.

Given that, this bridge runs as an independent Python process that owns
the webcam and MediaPipe, and talks to Klangorbit over OSC -- reusing the
plugin's **existing** `OscDriver` (`Source/OscDriver.h/.cpp`) and its
canonical-controller-input pipeline (`CanonicalInput.h`, `ParameterRegistry`,
Learn mode) instead of adding a second, parallel mapping system. The
trade-off is explicit: this is no longer a single in-process plugin --
running hand tracking means also running this script alongside Klangorbit,
not something bundled inside the VST3/AU/Standalone binary.

Camera capture itself also happens entirely in this script, via OpenCV
(`cv2.VideoCapture`) -- cross-platform on its own, so `juce::CameraDevice`
(JUCE's own camera abstraction, confirmed suitable while this was still
believed to be an in-process C++ feature) ended up not needed for this
architecture at all.

## Setup

```bash
cd MediaPipeBridge
python3 -m venv venv
source venv/bin/activate   # Windows: venv\Scripts\activate
pip install -r requirements.txt
python3 hand_tracking_bridge.py
```

The Hand Landmarker model (`hand_landmarker.task`, ~7.5MB, Apache 2.0,
from Google's own model hosting) downloads automatically on first run into
`MediaPipeBridge/models/` (gitignored -- regenerated, not vendored, same
reasoning as build artifacts elsewhere in this project).

**Camera permission**: macOS will prompt for camera access the first time
this script runs (System Settings -> Privacy & Security -> Camera, if it
doesn't prompt automatically). Windows: Settings -> Privacy -> Camera.

**`mediapipe` is pinned to `>=0.10.30,<1.0` in `requirements.txt`**
deliberately -- 1.0.0/1.0.1 crash the whole process on startup on macOS
the moment any Tasks Vision object (HandLandmarker included) is created
("`Check failed: service_ Service is unavailable`", not a catchable Python
exception -- a hard process abort). Confirmed by reproducing it locally
during this feature's own development, and matches a
[known, open upstream issue](https://github.com/google-ai-edge/mediapipe/issues/6356).
0.10.35 (the last of the 0.10.x series) has none of this and is what this
bridge was actually built and tested against.

## What this script does (and doesn't) do

Tracks up to two hands (`--num-hands 1` to restrict to one), and sends
each hand's smoothed wrist position as OSC messages to Klangorbit
(default `127.0.0.1:9000`, matching `OscDriver`'s own default port):

```
/klangorbit/hand/<slot>/x           float, 0..1
/klangorbit/hand/<slot>/y           float, 0..1
/klangorbit/hand/<slot>/z_estimate  float, 0..1 (see "Depth (z) reliability" below)
/klangorbit/hand/<slot>/visible     1.0 while tracked, 0.0 the instant it isn't
/klangorbit/mediapipe/status        1.0, once per processed frame, while running
```

`<slot>` is 0 for the left hand, 1 for the right (MediaPipe's own
handedness classification, stable across frames -- not detection order,
so a hand never suddenly swaps slots). These map straight into Klangorbit
exactly like any other OSC controller: open Mappings..., enter Learn mode,
move your hand -- no MediaPipe-specific UI or mapping path exists or is
needed.

It also recognizes gestures (`gestures.py`, see below) and sends them as
the messages listed in the next section.

There is also **no in-plugin UI indicator yet** for "the bridge is
running" / "a hand is currently visible" -- the `/klangorbit/mediapipe/status`
and `/klangorbit/hand/<slot>/visible` messages above exist specifically so
a future toolbar indicator can show this (the project's own requirement:
camera activity must be visible in the UI, not silently active). Not
built in this branch; flagging it here so it isn't forgotten.

**Privacy**: no camera frame, image, or raw landmark data is ever written
to disk or sent anywhere. Each frame is decoded, run through the
landmarker, reduced to the few smoothed scalars above, and discarded.
`--preview` (off by default) opens a local, on-screen-only OpenCV window
so the person running the script can see what's being tracked -- it does
not write anything to disk either.

## Gestures

Rule-based (thresholds on distances, speeds and path curvature -- no
trained classifier), per hand, each hand independent. **Pinch** (thumb tip
to index tip, relative to hand size, with hysteresis) is the "holding"
state, like a pressed mouse button. While pinched:

| Gesture | Motion | Result on the selected object |
|---|---|---|
| **Free Throw** | swing quickly, open the pinch while still moving | thrown along the hand's velocity at release (Impulse) |
| **Slingshot** | pull the hand back slowly, release | launched opposite to the pull, with the gravity-assist toward another object -- same semantics as the mouse Shift+drag sling |
| **Orbit** | draw a circle (>= ~270 degrees, consistent turn) | orbit around the origin; radius = circle size, direction = turning direction; fires once per pinch, and the release after it does not also throw |

A fast release is a Throw, a slow one is a Slingshot -- that distinction is
this project's own interpretation of the brief's "pull back and release"
wording (the mouse sling releases from a held pull, not at speed); a
quick forward flick past that point is what the Free Throw is.

```
/klangorbit/hand/<slot>/pinch   1.0 / 0.0 (Learn-mappable like any control)
/klangorbit/hands/spread        0..1 distance between both hands (only while both are visible)
/klangorbit/gesture/throw       slot aimX aimY
/klangorbit/gesture/slingshot   slot aimX aimY
/klangorbit/gesture/orbit       slot radius01 direction   (+1 counter-clockwise on screen, -1 clockwise)
```

Launch gestures are multi-argument messages (a slot plus values), which
don't fit `OscDriver`'s one-scalar-per-message model, so they're consumed
by a small `GestureDriver` in the plugin (via `OscDriver`'s new optional
message interceptor) and executed through `GestureActions` -- the same code
the gamepad's throw buttons now share, not a second implementation. Aim
values use the gamepad stick's convention (right/up positive, magnitude
<= 1); the plugin owns the mapping to world space.

**Two-hand spread** (distance between both hands) is just another
continuous OSC value -- map it in Learn mode to e.g. orbit radius or room
size, no extra code. **One hand per object** works at the recognition
level (both hands are recognized independently and tagged with their slot);
assigning each hand to its own object is part of the object-selection work
in `feature/mediapipe-parameter-mapping`, not this branch -- until then all
gestures act on the currently selected object.

**Finger-count mode switching was deliberately not built.** Deciding "how
many fingers are extended" from 21 landmarks degrades quickly with hand
rotation, partial occlusion and camera angle, and a wrong mode switch is
worse than none. A pinch/motion-based vocabulary is much more robust.

**All thresholds are informed guesses, not tuned against real hands and a
real camera** (no camera was available during development) -- expect to
adjust `GestureConfig` in `gestures.py` first. The recognition itself is
covered by `test_gestures.py` (synthetic 30fps trajectories, stdlib only:
`python3 -m unittest test_gestures`), the plugin side by
`Tools/verify_gesture_driver.cpp`; the aim/direction sign conventions (like
the gamepad's) are unverified by eye.

## Depth (z) reliability -- don't treat it as a real 3rd axis

MediaPipe's per-landmark `z` is a **learned estimate, not a measurement**:
a normal RGB webcam has no depth sensor at all, and MediaPipe's own
training documentation states the relative-depth-from-wrist regression
is learned **only from synthetic rendered hand data**, never from real
depth-camera ground truth (real photos only supervise the x/y position).
A clinical hand-tracking validation study
([arXiv:2308.01088](https://arxiv.org/pdf/2308.01088)) found plain
MediaPipe's own z output insufficient for precise work and built a
"depth-enhanced" variant using an actual depth sensor specifically to fix
it.

This bridge reflects that: `z_estimate` (derived from MediaPipe's
"world landmarks", its metric-scale estimate) is smoothed far more
aggressively than x/y (`Z_SMOOTHING = 0.1` vs. `0.4`, see
`HandSlotState` in `hand_tracking_bridge.py`) specifically so it can't
drive anything jumpy, and its OSC address is deliberately named
`z_estimate`, not `z` or `position_z` -- a naming choice meant to keep
"this is a rough hint, not a position" visible at the mapping-selection
point too, not just in this doc. Recommended use: map it to at most a
single coarse, forgiving parameter if at all (e.g. a "leans toward"
tendency), never as one axis of a precise 3D position alongside x/y.

## Frame-rate mismatch

A webcam runs at roughly 30fps; Klangorbit's own physics/control-rate
timer runs at 60-120Hz (see the main README). Sending raw, unsmoothed
per-frame positions at that mismatch would produce visible/audible jumps.
`ExponentialSmoother` (one-pole low-pass, see `hand_tracking_bridge.py`)
smooths x/y/z before every OSC send specifically to avoid that -- no
change needed on the Klangorbit/C++ side for this.
