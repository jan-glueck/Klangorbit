#pragma once
#include "TrajectoryEngine.h"
#include "SlingGesture.h"

/**
    The three "launch" actions behind the gamepad's Y/shoulder buttons
    (GamepadDriver) -- Free Throw / Orbit Shot / Slingshot, extracted out of
    GamepadDriver::driveThrowGesture()'s own switch purely to keep that
    function focused on button-state handling (SlingGesture.h still
    supplies the underlying math, as before).
*/
namespace GestureActions
{
    enum class LaunchMode { FreeThrow, OrbitShot, Slingshot };

    // Fires Free Throw / Orbit Shot / Slingshot for objectIndex from pullVector.
    // Caller is responsible for the minimum-pull gate and for validating
    // objectIndex (same split as before the extraction).
    void fire (TrajectoryEngine& engine, LaunchMode mode, int objectIndex, Vec3 pullVector);
}
