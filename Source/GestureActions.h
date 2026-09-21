#pragma once
#include "TrajectoryEngine.h"
#include "SlingGesture.h"

/**
    The three "launch" actions shared by every gesture source that aims with
    a pull vector -- the gamepad's Y/shoulder buttons (GamepadDriver) and
    recognized hand gestures (GestureDriver). Extracted from what used to be
    GamepadDriver::driveThrowGesture()'s own switch so both sources call the
    exact same TrajectoryEngine sequences rather than each reimplementing
    them (SlingGesture.h supplies the underlying math, as before).
*/
namespace GestureActions
{
    enum class LaunchMode { FreeThrow, OrbitShot, Slingshot };

    // Meters of launch strength at full aim deflection -- see
    // GamepadDriver::setThrowMaxPullDistance()'s comment for the tuning
    // reasoning; hand gestures use the same default.
    constexpr float defaultMaxPullMeters = 2.0f;

    // "Stick-like" aim (aimX right-positive, aimY up-positive, each roughly
    // -1..1) -> world-space pull vector. Same convention GamepadDriver
    // has always used for its left stick: up -> +X (front), right -> -Y
    // (this project's y=left-positive convention).
    inline Vec3 aimToPullVector (float aimX, float aimY, float maxPullMeters = defaultMaxPullMeters)
    {
        return Vec3 { aimY, -aimX, 0.0f } * maxPullMeters;
    }

    // Fires Free Throw / Orbit Shot / Slingshot for objectIndex from pullVector.
    // Caller is responsible for the minimum-pull gate and for validating
    // objectIndex (same split as before the extraction).
    void fire (TrajectoryEngine& engine, LaunchMode mode, int objectIndex, Vec3 pullVector);

    // Orbit with an explicitly chosen turning direction (hand circle
    // gesture): the direction comes from the gesture itself, not derived
    // from pull geometry the way fire()'s OrbitShot does. directionSign:
    // +1 counter-clockwise (x/y plane viewed from +z), -1 clockwise.
    void fireOrbit (TrajectoryEngine& engine, int objectIndex, float radiusMeters, float directionSign);
}
