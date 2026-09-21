"""Unit tests for gestures.py -- synthetic 30fps trajectories, no camera needed.

Run: python3 -m unittest test_gestures   (stdlib only)
"""

import math
import unittest

from gestures import (GestureConfig, HandGestureRecognizer, hand_spread, pinch_ratio)

DT = 1.0 / 30.0
OPEN, CLOSED = 1.0, 0.1


def run(rec, frames, t0=0.0):
    """frames: list of (x, y, pinch). Returns (all events, end time)."""
    events = []
    t = t0
    for x, y, p in frames:
        events += rec.update(t, x, y, p)
        t += DT
    return events, t


def still(x, y, pinch, n):
    return [(x, y, pinch)] * n


def line(p0, p1, pinch, n):
    return [(p0[0] + (p1[0] - p0[0]) * i / (n - 1), p0[1] + (p1[1] - p0[1]) * i / (n - 1), pinch) for i in range(n)]


def circle(center, radius, turns, pinch, n, ccw=True):
    sgn = 1 if ccw else -1
    return [(center[0] + radius * math.cos(sgn * 2 * math.pi * turns * i / n),
             center[1] + radius * math.sin(sgn * 2 * math.pi * turns * i / n), pinch) for i in range(n)]


class PinchTests(unittest.TestCase):
    def test_hysteresis_no_flicker_between_thresholds(self):
        rec = HandGestureRecognizer(0)
        run(rec, still(0, 0, CLOSED, 3))
        self.assertTrue(rec.pinched)
        run(rec, still(0, 0, 0.4, 5))  # between on (0.25) and off (0.5): stays pinched
        self.assertTrue(rec.pinched)
        run(rec, still(0, 0, OPEN, 2))
        self.assertFalse(rec.pinched)

    def test_pinch_ratio_open_vs_pinched(self):
        wrist, mcp = (0, 0), (0, 0.2)
        self.assertLess(pinch_ratio(wrist, (0.01, 0.3), (0.02, 0.3), mcp), 0.25)
        self.assertGreater(pinch_ratio(wrist, (-0.1, 0.3), (0.1, 0.35), mcp), 0.5)


class ThrowTests(unittest.TestCase):
    def test_fast_swipe_right_then_open_throws_right(self):
        rec = HandGestureRecognizer(1)
        ev, _ = run(rec, still(0, 0, CLOSED, 3) + line((0, 0), (0.6, 0), CLOSED, 10) + still(0.6, 0, OPEN, 1)[:0]
                    + [(0.62, 0, OPEN)])
        self.assertEqual([e.kind for e in ev], ["throw"])
        self.assertEqual(ev[0].slot, 1)
        self.assertGreater(ev[0].a, 0.5)
        self.assertAlmostEqual(ev[0].b, 0.0, places=1)
        self.assertLessEqual(math.hypot(ev[0].a, ev[0].b), 1.0 + 1e-9)

    def test_upward_swipe_has_positive_aim_y(self):
        rec = HandGestureRecognizer(0)
        ev, _ = run(rec, still(0, 0, CLOSED, 3) + line((0, 0), (0, 0.5), CLOSED, 8) + [(0, 0.52, OPEN)])
        self.assertEqual([e.kind for e in ev], ["throw"])
        self.assertGreater(ev[0].b, 0.4)

    def test_hold_still_then_release_fires_nothing(self):
        rec = HandGestureRecognizer(0)
        ev, _ = run(rec, still(0.5, 0.5, CLOSED, 20) + still(0.5, 0.5, OPEN, 3))
        self.assertEqual(ev, [])

    def test_open_hand_movement_alone_fires_nothing(self):
        rec = HandGestureRecognizer(0)
        ev, _ = run(rec, line((0, 0), (1.0, 0), OPEN, 10))
        self.assertEqual(ev, [])


class SlingshotTests(unittest.TestCase):
    def test_slow_pull_back_then_release_launches_opposite_direction(self):
        rec = HandGestureRecognizer(0)
        # anchor at origin, drag slowly down-left, then let go while (nearly) still
        ev, _ = run(rec, still(0, 0, CLOSED, 3) + line((0, 0), (-0.2, -0.2), CLOSED, 40) + still(-0.2, -0.2, CLOSED, 5)
                    + [(-0.2, -0.2, OPEN)])
        self.assertEqual([e.kind for e in ev], ["slingshot"])
        self.assertGreater(ev[0].a, 0.3)  # launch right
        self.assertGreater(ev[0].b, 0.3)  # launch up
        self.assertLessEqual(math.hypot(ev[0].a, ev[0].b), 1.0 + 1e-9)

    def test_tiny_pull_is_ignored(self):
        rec = HandGestureRecognizer(0)
        ev, _ = run(rec, still(0, 0, CLOSED, 3) + line((0, 0), (-0.03, 0), CLOSED, 20) + [(-0.03, 0, OPEN)])
        self.assertEqual(ev, [])


class OrbitTests(unittest.TestCase):
    def test_counter_clockwise_circle_while_pinched(self):
        rec = HandGestureRecognizer(0)
        ev, _ = run(rec, still(0.2, 0, CLOSED, 2) + circle((0, 0), 0.2, 1.2, CLOSED, 40, ccw=True))
        self.assertEqual([e.kind for e in ev], ["orbit"])
        self.assertEqual(ev[0].b, 1.0)
        self.assertTrue(0.3 < ev[0].a <= 1.0)

    def test_clockwise_circle_has_negative_direction(self):
        rec = HandGestureRecognizer(0)
        ev, _ = run(rec, still(0.2, 0, CLOSED, 2) + circle((0, 0), 0.2, 1.2, CLOSED, 40, ccw=False))
        self.assertEqual([e.kind for e in ev], ["orbit"])
        self.assertEqual(ev[0].b, -1.0)

    def test_release_after_orbit_does_not_also_throw(self):
        rec = HandGestureRecognizer(0)
        frames = still(0.2, 0, CLOSED, 2) + circle((0, 0), 0.2, 1.2, CLOSED, 40) + [(0.15, 0.1, OPEN)]
        ev, _ = run(rec, frames)
        self.assertEqual([e.kind for e in ev], ["orbit"])

    def test_orbit_fires_only_once_per_pinch(self):
        rec = HandGestureRecognizer(0)
        ev, _ = run(rec, still(0.2, 0, CLOSED, 2) + circle((0, 0), 0.2, 3.0, CLOSED, 100))
        self.assertEqual([e.kind for e in ev], ["orbit"])

    def test_circle_without_pinch_does_nothing(self):
        rec = HandGestureRecognizer(0)
        ev, _ = run(rec, circle((0, 0), 0.2, 2.0, OPEN, 60))
        self.assertEqual(ev, [])

    def test_tiny_jitter_circle_does_nothing(self):
        rec = HandGestureRecognizer(0)
        ev, _ = run(rec, still(0, 0, CLOSED, 2) + circle((0, 0), 0.004, 3.0, CLOSED, 90))
        self.assertEqual(ev, [])

    def test_straight_line_is_not_an_orbit(self):
        rec = HandGestureRecognizer(0)
        ev, _ = run(rec, still(0, 0, CLOSED, 2) + line((0, 0), (0.25, 0), CLOSED, 60))
        self.assertNotIn("orbit", [e.kind for e in ev])


class RobustnessTests(unittest.TestCase):
    def test_hand_lost_while_pinched_cancels_silently(self):
        rec = HandGestureRecognizer(0)
        run(rec, still(0, 0, CLOSED, 3) + line((0, 0), (0.6, 0), CLOSED, 10))
        rec.lost()
        self.assertFalse(rec.pinched)
        ev, _ = run(rec, [(0.7, 0, OPEN)])  # reappears open: must not fire a stale throw
        self.assertEqual(ev, [])

    def test_two_hands_are_independent(self):
        left, right = HandGestureRecognizer(0), HandGestureRecognizer(1)
        run(left, still(0, 0, CLOSED, 3) + line((0, 0), (0.6, 0), CLOSED, 10))
        ev_right, _ = run(right, still(0, 0, OPEN, 10))
        self.assertEqual(ev_right, [])
        self.assertTrue(left.pinched)

    def test_hand_spread_mapping(self):
        self.assertEqual(hand_spread((0, 0), (0, 0)), 0.0)
        self.assertAlmostEqual(hand_spread((0, 0), (0.6, 0)), 0.5)
        self.assertEqual(hand_spread((0, 0), (5, 0)), 1.0)


if __name__ == "__main__":
    unittest.main()
