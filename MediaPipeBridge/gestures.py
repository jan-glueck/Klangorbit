"""
Rule-based hand-gesture recognition for the Klangorbit MediaPipe bridge.

Pure Python -- no OpenCV/MediaPipe imports -- so it can be unit-tested with
synthetic trajectories (test_gestures.py) without a camera. No trained ML
classifier: thresholds on distances, speeds and path curvature only.

Coordinates: everything here is in "height units" with y pointing UP (the
bridge converts from image coordinates, x*aspect and -y, before calling
in), so the maths is ordinary counter-clockwise-positive geometry.

Gestures per hand (each hand is recognized independently, tagged with its
slot, so two hands never interfere with each other):

- Pinch (thumb tip <-> index tip, relative to hand size) = "holding",
  with hysteresis so it doesn't flicker at the threshold. Analogous to a
  pressed mouse button.
- Free Throw: pinch, swing fast, open the pinch while still moving. Aim =
  the hand's velocity at release.
- Slingshot: pinch, pull the hand back and release it (slowly). Aim = the
  pull vector, anchor - release point -- the same "launch opposite to the
  drag" semantics as the mouse sling gesture.
- Orbit: while pinched, draw a circle (>= ~270 degrees of consistent
  turning). Radius = the circle's size, direction = its turning direction.
  Fires once per pinch; the release afterwards does not also throw.

Events carry values in the "stick-like" convention the plugin's
GestureDriver expects (aim_x right-positive, aim_y up-positive, magnitude
<= 1) -- the plugin owns the world-space mapping, not this module, so the
gamepad and hand gestures stay consistent with each other.

The threshold values below are informed guesses, NOT tuned against real
hands/cameras -- they are the first thing to adjust after trying this with
a webcam.
"""

import math
from collections import deque
from dataclasses import dataclass, field
from typing import List, Optional, Tuple


@dataclass
class GestureConfig:
    # thumb-tip/index-tip distance divided by wrist->middle-MCP distance
    pinch_on_ratio: float = 0.25
    pinch_off_ratio: float = 0.50

    history_seconds: float = 1.6
    velocity_window_seconds: float = 0.12

    # Free Throw: speed range (height units / second) mapped to aim 0..1.
    throw_min_speed: float = 0.9
    throw_max_speed: float = 3.0

    # Slingshot: pull distance range (height units) mapped to aim 0..1.
    sling_min_pull: float = 0.08
    sling_max_pull: float = 0.35

    # Orbit: a circle of this radius range (height units), turning at
    # least orbit_min_turn radians with this share of steps in one direction.
    orbit_min_radius: float = 0.06
    orbit_max_radius: float = 0.40
    orbit_radius_full_scale: float = 0.30  # radius mapped to radius01 == 1
    orbit_min_turn: float = 1.5 * math.pi
    orbit_consistency: float = 0.75
    orbit_min_step: float = 0.008  # ignore sub-jitter movements
    orbit_window_seconds: float = 1.5


@dataclass
class GestureEvent:
    kind: str  # "throw" | "slingshot" | "orbit"
    slot: int
    a: float  # throw/slingshot: aim_x ; orbit: radius01
    b: float  # throw/slingshot: aim_y ; orbit: direction (+1 counter-clockwise, -1 clockwise)


def pinch_ratio(wrist, thumb_tip, index_tip, middle_mcp) -> float:
    """Each argument is an (x, y) pair in the same (aspect-corrected) units."""
    hand_size = math.dist(wrist, middle_mcp)
    if hand_size < 1e-6:
        return 1.0
    return math.dist(thumb_tip, index_tip) / hand_size


def hand_spread(pos_a: Tuple[float, float], pos_b: Tuple[float, float], full_scale: float = 1.2) -> float:
    """Distance between two hands, mapped to 0..1 (full_scale = height units for 1.0)."""
    return max(0.0, min(1.0, math.dist(pos_a, pos_b) / full_scale))


def _clamp_magnitude(x: float, y: float) -> Tuple[float, float]:
    m = math.hypot(x, y)
    if m > 1.0:
        return x / m, y / m
    return x, y


class HandGestureRecognizer:
    def __init__(self, slot: int, config: Optional[GestureConfig] = None):
        self.slot = slot
        self.cfg = config or GestureConfig()
        self.pinched = False
        self._history: deque = deque()  # (t, x, y)
        self._anchor: Optional[Tuple[float, float]] = None
        self._orbit_fired = False

    def lost(self):
        """Hand left the frame: cancel any gesture in progress, fire nothing."""
        self.pinched = False
        self._history.clear()
        self._anchor = None
        self._orbit_fired = False

    def update(self, t: float, x: float, y: float, pinch: float) -> List[GestureEvent]:
        cfg = self.cfg
        self._history.append((t, x, y))
        while self._history and t - self._history[0][0] > cfg.history_seconds:
            self._history.popleft()

        events: List[GestureEvent] = []

        if not self.pinched and pinch < cfg.pinch_on_ratio:
            self.pinched = True
            self._anchor = (x, y)
            self._orbit_fired = False
        elif self.pinched and pinch > cfg.pinch_off_ratio:
            self.pinched = False
            if not self._orbit_fired:
                events.extend(self._on_release(t, x, y))
            self._anchor = None
            self._orbit_fired = False
        elif self.pinched and not self._orbit_fired:
            orbit = self._detect_orbit(t)
            if orbit is not None:
                events.append(orbit)
                self._orbit_fired = True
                self._history.clear()
                self._history.append((t, x, y))

        return events

    def _velocity(self, t: float) -> Tuple[float, float]:
        window_start = None
        for ht, hx, hy in self._history:
            if t - ht <= self.cfg.velocity_window_seconds:
                window_start = (ht, hx, hy)
                break
        if window_start is None or t - window_start[0] < 1e-3:
            return 0.0, 0.0
        _, x, y = self._history[-1]
        dt = t - window_start[0]
        return (x - window_start[1]) / dt, (y - window_start[2]) / dt

    def _on_release(self, t: float, x: float, y: float) -> List[GestureEvent]:
        cfg = self.cfg
        vx, vy = self._velocity(t)
        speed = math.hypot(vx, vy)
        if speed >= cfg.throw_min_speed:
            ax, ay = _clamp_magnitude(vx / cfg.throw_max_speed, vy / cfg.throw_max_speed)
            return [GestureEvent("throw", self.slot, ax, ay)]

        if self._anchor is not None:
            px, py = self._anchor[0] - x, self._anchor[1] - y
            pull = math.hypot(px, py)
            if pull >= cfg.sling_min_pull:
                ax, ay = _clamp_magnitude(px / cfg.sling_max_pull, py / cfg.sling_max_pull)
                return [GestureEvent("slingshot", self.slot, ax, ay)]
        return []

    def _detect_orbit(self, t: float) -> Optional[GestureEvent]:
        cfg = self.cfg
        pts = [(hx, hy) for ht, hx, hy in self._history if t - ht <= cfg.orbit_window_seconds]
        if len(pts) < 8:
            return None

        # Drop sub-jitter steps so a resting hand's noise can't accumulate turning.
        path = [pts[0]]
        for p in pts[1:]:
            if math.dist(p, path[-1]) >= cfg.orbit_min_step:
                path.append(p)
        if len(path) < 8:
            return None

        total_turn = 0.0
        same_sign = {1: 0, -1: 0}
        steps = 0
        for i in range(2, len(path)):
            v1 = (path[i - 1][0] - path[i - 2][0], path[i - 1][1] - path[i - 2][1])
            v2 = (path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1])
            cross = v1[0] * v2[1] - v1[1] * v2[0]
            dot = v1[0] * v2[0] + v1[1] * v2[1]
            ang = math.atan2(cross, dot)
            total_turn += ang
            if abs(ang) > 1e-3:
                same_sign[1 if ang > 0 else -1] += 1
                steps += 1

        if steps == 0 or abs(total_turn) < cfg.orbit_min_turn:
            return None
        direction = 1 if total_turn > 0 else -1
        if same_sign[direction] / steps < cfg.orbit_consistency:
            return None

        xs = [p[0] for p in path]
        ys = [p[1] for p in path]
        radius = 0.25 * ((max(xs) - min(xs)) + (max(ys) - min(ys)))  # mean half-extent
        if not (cfg.orbit_min_radius <= radius <= cfg.orbit_max_radius):
            return None

        radius01 = max(0.0, min(1.0, radius / cfg.orbit_radius_full_scale))
        return GestureEvent("orbit", self.slot, radius01, float(direction))
