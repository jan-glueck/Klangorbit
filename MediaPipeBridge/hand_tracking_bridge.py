#!/usr/bin/env python3
"""
Klangorbit MediaPipe hand-tracking bridge.

Runs entirely OUTSIDE the Klangorbit plugin process (see
MediaPipeBridge/README.md for why: MediaPipe's C++ SDK has no official
Windows support and no maintained CMake integration path, while its
Python distribution is first-class and pip-installable on every target
platform with zero Bazel involvement). This script owns the webcam
(OpenCV, cross-platform -- no juce::CameraDevice or any other in-plugin
capture code needed) and MediaPipe's Hand Landmarker (Tasks API), and
sends the results to Klangorbit as plain OSC messages over UDP, consumed
by the plugin's EXISTING OscDriver (Source/OscDriver.h/.cpp) -- no new
parallel mapping system, per the project's own architecture rule.

OSC message convention sent by this script
--------------------------------------------------------------------------
Continuous hand position, per hand slot (0 = Left, 1 = Right, by
MediaPipe's own handedness classification -- stable across frames even if
detection order changes, so "left hand" never suddenly becomes slot 1):

    /klangorbit/hand/<slot>/x          float, 0..1 (already matches
    /klangorbit/hand/<slot>/y          float, 0..1  OscDriver's existing
                                        Unipolar convention -- whatever
                                        parameter this gets Learn-mapped
                                        to is denormalized into ITS OWN
                                        range automatically, exactly like
                                        an existing MIDI CC mapping)
    /klangorbit/hand/<slot>/z_estimate float, 0..1, HEAVILY smoothed --
                                        see the class comment on
                                        HandTracker.Z_SMOOTHING below for
                                        why this is deliberately treated
                                        as a low-confidence hint, not a
                                        real depth measurement.
    /klangorbit/hand/<slot>/visible    float, 1.0 while this hand is
                                        currently detected, 0.0 the
                                        instant it drops out of frame (so
                                        a future UI indicator -- or a
                                        Learn-mapped parameter -- can
                                        distinguish "hand not currently
                                        visible" from "hand sitting still
                                        at its last known position").

Status heartbeat (so a future in-plugin UI indicator can show whether
this bridge is even running, per the project's requirement that camera
activity must be visible in the UI, not silently active/inactive):

    /klangorbit/mediapipe/status       float, 1.0, sent once per
                                        processed frame while the camera
                                        is open and the tracking loop is
                                        alive. Its ABSENCE (no message
                                        for longer than a UI-side timeout)
                                        is how "not running" is detected
                                        -- OSC has no native disconnect
                                        notification, same reasoning
                                        OscDriver.h's own class comment
                                        already gives for buttons.

Gestures (recognized by gestures.py, rule-based, no ML):

    /klangorbit/hand/<slot>/pinch      float, 1.0 while thumb and index are
                                        pinched ("holding"), else 0.0 --
                                        ordinary continuous value, Learn-
                                        mappable like any other.
    /klangorbit/hands/spread           float, 0..1, distance between the
                                        two hands' wrists; only sent while
                                        BOTH hands are visible. Ordinary
                                        continuous value (e.g. map it to
                                        orbit radius or room size).

Discrete launch gestures are multi-argument messages (a slot number plus
values -- they don't fit OscDriver's one-scalar model), consumed by the
plugin's GestureDriver, which runs them on the selected object through the
same code as the gamepad's throw buttons. Arguments after the int slot are
in the gamepad-stick-like convention: right/up positive, magnitude <= 1.

    /klangorbit/gesture/throw          slot aimX aimY   (fast swipe + release)
    /klangorbit/gesture/slingshot      slot aimX aimY   (pull back + release;
                                                         aim = launch direction)
    /klangorbit/gesture/orbit          slot radius01 direction
                                                        (circle while pinched;
                                                         direction +1 = counter-
                                                         clockwise as seen on
                                                         screen, -1 = clockwise)

Privacy / data handling
--------------------------------------------------------------------------
No camera frame, image, or raw per-landmark data is ever written to disk
or sent anywhere -- each frame is decoded, run through the landmarker,
reduced to the few smoothed scalars above, and discarded. --preview (off
by default) opens a local, on-screen-only OpenCV window for the person
running this script to see what's being tracked; it never writes
anything to disk either.
"""

import argparse
import math
import sys
import time
import urllib.request
from pathlib import Path

import cv2
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision as mp_vision
from pythonosc import udp_client

from gestures import HandGestureRecognizer, hand_spread, pinch_ratio

MODEL_URL = "https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task"
MODEL_CACHE_DIR = Path(__file__).parent / "models"
MODEL_CACHE_PATH = MODEL_CACHE_DIR / "hand_landmarker.task"

# MediaPipe's own handedness label ("Left"/"Right") assumes the image is
# already mirrored the way a person looks at their own hands in a mirror
# (this is also just the intuitive webcam-app convention: your right hand
# moves things right on screen). Capture is flipped horizontally to match
# -- see main()'s cv2.flip() call.
HAND_SLOTS = {"Left": 0, "Right": 1}


def ensure_model_downloaded() -> Path:
    """Downloads the Hand Landmarker .task model bundle to a local cache
    on first run (~7.5MB, Apache 2.0, from Google's own model hosting) --
    not committed to the repo, same reasoning as build artifacts:
    regenerable, and every user should get Google's current canonical
    file rather than a copy that silently ages in version control."""
    if MODEL_CACHE_PATH.exists():
        return MODEL_CACHE_PATH

    MODEL_CACHE_DIR.mkdir (parents=True, exist_ok=True)
    print (f"Downloading Hand Landmarker model to {MODEL_CACHE_PATH} ...", file=sys.stderr)
    try:
        urllib.request.urlretrieve (MODEL_URL, MODEL_CACHE_PATH)
    except OSError as e:
        print (f"ERROR: could not download the hand-tracking model ({e}).\n"
               f"Check your internet connection, or download it manually from\n"
               f"{MODEL_URL}\nand place it at {MODEL_CACHE_PATH}", file=sys.stderr)
        sys.exit (1)
    print ("Download complete.", file=sys.stderr)
    return MODEL_CACHE_PATH


class ExponentialSmoother:
    """One-pole low-pass filter for a single scalar -- smooths the
    ~30fps camera/tracking rate up to something that doesn't produce
    visible/audible jumps once fed into TrajectoryEngine's much faster
    60-120Hz control-rate tick (see this project's README, "GrainCloud"/
    control-rate sections for the same rate mismatch elsewhere). alpha
    closer to 1.0 = less smoothing/more responsive, closer to 0.0 = more
    smoothing/more lag. Resets instantly (no smoothing) the first time a
    value arrives after being unset, so a hand re-entering frame doesn't
    slowly drift in from wherever it happened to be last."""

    def __init__ (self, alpha: float):
        self.alpha = alpha
        self.value = None

    def update (self, newValue: float) -> float:
        if self.value is None:
            self.value = newValue
        else:
            self.value = self.alpha * newValue + (1.0 - self.alpha) * self.value
        return self.value

    def reset (self):
        self.value = None


def send_gesture(osc: udp_client.SimpleUDPClient, event) -> None:
    osc.send_message (f"/klangorbit/gesture/{event.kind}", [int (event.slot), float (event.a), float (event.b)])


class HandSlotState:
    # z_estimate gets much heavier smoothing than x/y (alpha=0.1 vs 0.4):
    # MediaPipe's landmark z is a learned, SYNTHETIC-DATA-ONLY-trained
    # relative-depth regression (see MediaPipeBridge/README.md's own
    # "z reliability" section for the full reasoning/evidence), not a
    # measurement -- it is noticeably jitterier than x/y frame-to-frame
    # even for a still hand, so treating it as a coarse, slow-moving hint
    # rather than a responsive position is a deliberate choice here, not
    # an oversight.
    X_SMOOTHING = 0.4
    Y_SMOOTHING = 0.4
    Z_SMOOTHING = 0.1

    def __init__ (self):
        self.x = ExponentialSmoother (self.X_SMOOTHING)
        self.y = ExponentialSmoother (self.Y_SMOOTHING)
        self.z = ExponentialSmoother (self.Z_SMOOTHING)
        self.visible = False
        self.pinched = False


def send_hand_state (osc: udp_client.SimpleUDPClient, slot: int, state: HandSlotState):
    osc.send_message (f"/klangorbit/hand/{slot}/visible", 1.0 if state.visible else 0.0)
    osc.send_message (f"/klangorbit/hand/{slot}/pinch", 1.0 if state.pinched else 0.0)
    if state.visible:
        osc.send_message (f"/klangorbit/hand/{slot}/x", state.x.value)
        osc.send_message (f"/klangorbit/hand/{slot}/y", state.y.value)
        osc.send_message (f"/klangorbit/hand/{slot}/z_estimate", state.z.value)


def main():
    parser = argparse.ArgumentParser (description="Klangorbit MediaPipe hand-tracking bridge")
    parser.add_argument ("--camera-index", type=int, default=0, help="OpenCV camera device index (default: 0)")
    parser.add_argument ("--host", default="127.0.0.1", help="OSC target host (default: 127.0.0.1)")
    parser.add_argument ("--port", type=int, default=9000, help="OSC target port -- matches OscDriver's own default (default: 9000)")
    parser.add_argument ("--num-hands", type=int, default=2, choices=[1, 2], help="Max simultaneous hands to track (default: 2)")
    parser.add_argument ("--preview", action="store_true", help="Show a local on-screen preview window with landmarks drawn (debugging aid, off by default; never written to disk)")
    parser.add_argument ("--model-path", type=Path, default=None, help="Use a specific .task model file instead of downloading/caching the default one")
    args = parser.parse_args()

    model_path = args.model_path if args.model_path is not None else ensure_model_downloaded()

    base_options = mp_python.BaseOptions (model_asset_path=str (model_path))
    options = mp_vision.HandLandmarkerOptions (
        base_options=base_options,
        num_hands=args.num_hands,
        running_mode=mp_vision.RunningMode.VIDEO,
    )
    landmarker = mp_vision.HandLandmarker.create_from_options (options)

    cap = cv2.VideoCapture (args.camera_index)
    if not cap.isOpened():
        print (f"ERROR: could not open camera index {args.camera_index}. "
               f"Check that a camera is connected and that this script has "
               f"camera permission (macOS: System Settings -> Privacy & "
               f"Security -> Camera).", file=sys.stderr)
        sys.exit (1)
    cap.set (cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set (cv2.CAP_PROP_FRAME_HEIGHT, 480)

    osc = udp_client.SimpleUDPClient (args.host, args.port)
    slots = {0: HandSlotState(), 1: HandSlotState()}
    recognizers = {0: HandGestureRecognizer (0), 1: HandGestureRecognizer (1)}

    print (f"Klangorbit hand-tracking bridge running -- camera {args.camera_index}, "
           f"sending OSC to {args.host}:{args.port}. Ctrl+C to stop.", file=sys.stderr)

    spread = ExponentialSmoother (0.4)
    start_time = time.monotonic()
    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                print ("WARNING: camera frame read failed -- camera may have "
                       "been disconnected. Retrying...", file=sys.stderr)
                time.sleep (0.1)
                continue

            # Mirror the frame -- see the module comment on HAND_SLOTS for why
            # (matches MediaPipe's own handedness convention and the
            # intuitive "move your hand right, it goes right on screen" feel).
            frame = cv2.flip (frame, 1)
            rgb = cv2.cvtColor (frame, cv2.COLOR_BGR2RGB)
            mp_image = mp.Image (image_format=mp.ImageFormat.SRGB, data=rgb)
            timestamp_ms = int ((time.monotonic() - start_time) * 1000)
            result = landmarker.detect_for_video (mp_image, timestamp_ms)

            seen_this_frame = set()
            aspect = frame.shape[1] / frame.shape[0]
            frame_time = timestamp_ms / 1000.0
            wrist_positions = {}
            for i, hand_landmarks in enumerate (result.hand_landmarks):
                handedness_label = result.handedness[i][0].category_name
                slot_index = HAND_SLOTS.get (handedness_label)
                if slot_index is None:
                    continue  # unexpected label, skip defensively rather than crash
                seen_this_frame.add (slot_index)

                # Landmark 0 is the wrist (MediaPipe's own fixed indexing) --
                # used as this hand's tracked position, the same reference
                # point z_estimate is already relative to.
                wrist = hand_landmarks[0]
                state = slots[slot_index]
                state.visible = True
                state.x.update (float (wrist.x))
                state.y.update (float (wrist.y))
                # World landmarks give a metric z (meters, hand-centered) --
                # rescaled into a rough 0..1-ish band around 0.5 purely so it
                # shares OscDriver's existing Unipolar [0,1] convention; NOT
                # a claim of metric accuracy, see the reliability comment
                # above on HandSlotState.Z_SMOOTHING.
                world_z = result.hand_world_landmarks[i][0].z
                state.z.update (max (0.0, min (1.0, 0.5 - world_z)))

                # Gesture recognition works in aspect-corrected "height units"
                # with y pointing UP (see gestures.py's module comment).
                def to_units (lm):
                    return (float (lm.x) * aspect, -float (lm.y))
                ratio = pinch_ratio (to_units (hand_landmarks[0]), to_units (hand_landmarks[4]),
                                     to_units (hand_landmarks[8]), to_units (hand_landmarks[9]))
                ux, uy = to_units (wrist)
                wrist_positions[slot_index] = (ux, uy)
                for event in recognizers[slot_index].update (frame_time, ux, uy, ratio):
                    send_gesture (osc, event)
                state.pinched = recognizers[slot_index].pinched

            for slot_index, state in slots.items():
                if slot_index not in seen_this_frame and state.visible:
                    state.visible = False
                    state.pinched = False
                    state.x.reset()
                    state.y.reset()
                    state.z.reset()
                    recognizers[slot_index].lost()
                send_hand_state (osc, slot_index, state)

            if 0 in wrist_positions and 1 in wrist_positions:
                spread.update (hand_spread (wrist_positions[0], wrist_positions[1]))
                osc.send_message ("/klangorbit/hands/spread", spread.value)
            else:
                spread.reset()

            osc.send_message ("/klangorbit/mediapipe/status", 1.0)

            if args.preview:
                for hand_landmarks in result.hand_landmarks:
                    for lm in hand_landmarks:
                        px, py = int (lm.x * frame.shape[1]), int (lm.y * frame.shape[0])
                        cv2.circle (frame, (px, py), 3, (0, 255, 0), -1)
                cv2.imshow ("Klangorbit hand-tracking bridge (preview -- not recorded)", frame)
                if cv2.waitKey (1) & 0xFF == ord ("q"):
                    break

    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
        if args.preview:
            cv2.destroyAllWindows()
        landmarker.close()
        print ("Bridge stopped.", file=sys.stderr)


if __name__ == "__main__":
    main()
