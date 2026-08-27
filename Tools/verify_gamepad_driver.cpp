#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/GamepadDriver.h"
#include "../Source/SlingGesture.h"

// Exercises GamepadDriver's fixed default control scheme (object
// cycle/add/remove, Free Throw/Orbit Shot/Slingshot) directly, via
// hand-built GamepadState previous/current pairs against a real
// TrajectoryEngine -- entirely without a connected controller.
// GamepadBridge (the actual GCController polling) is the only part of
// GamepadDriver that genuinely can't be exercised headlessly, and neither
// driveObjectManagement() nor driveThrowGesture() ever touches it -- see
// GamepadDriver.h's own comment on why both are public specifically for
// this. Movement (driveSelectedObjectMovement()) and camera control
// (KlangorbitEditor::updateGamepadCamera()) are NOT covered here, same
// "verified by full build + Standalone launch stability, not headlessly"
// precedent already accepted for GamepadBridge/Camera3D interaction
// elsewhere in this project.

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    bool approxEqual (float a, float b, float tolerance = 1.0e-3f)
    {
        return std::abs (a - b) <= tolerance;
    }

    bool approxEqual (Vec3 a, Vec3 b, float tolerance = 1.0e-3f)
    {
        return approxEqual (a.x, b.x, tolerance) && approxEqual (a.y, b.y, tolerance) && approxEqual (a.z, b.z, tolerance);
    }
}

int main()
{
    // ==================================================================
    // driveObjectManagement: cycle (Button X), add (Button A), remove (Button B)
    // ==================================================================
    {
        TrajectoryEngine engine (3);
        engine.activateObject (0);
        engine.activateObject (2); // slot 1 left inactive, to verify cycling skips it

        GamepadDriver driver (engine);
        int selected = -1;

        GamepadState previous; // everything false/zero
        GamepadState pressX; pressX.buttonX = true;

        driver.driveObjectManagement (selected, previous, pressX);
        check (selected == 0, "cycle: from nothing selected, lands on the first active slot (0)");

        // Holding (no fresh edge) must not re-fire.
        driver.driveObjectManagement (selected, pressX, pressX);
        check (selected == 0, "cycle: holding Button X (no new press edge) does not cycle again");

        GamepadState released; // buttonX back to false
        driver.driveObjectManagement (selected, pressX, released);
        driver.driveObjectManagement (selected, released, pressX); // a fresh press
        check (selected == 2, "cycle: skips the inactive slot (1) and lands on the next active one (2)");

        driver.driveObjectManagement (selected, pressX, released);
        driver.driveObjectManagement (selected, released, pressX);
        check (selected == 0, "cycle: wraps back around to the first active slot after the last one");
    }

    {
        TrajectoryEngine engine (2);
        engine.activateObject (0); // slot 1 starts inactive

        GamepadDriver driver (engine);
        int selected = 0;

        GamepadState previous;
        GamepadState pressA; pressA.buttonA = true;

        driver.driveObjectManagement (selected, previous, pressA);
        check (engine.getObject (1).inputChannel >= 0, "add: activates the next inactive slot");
        check (selected == 1, "add: selects the newly activated slot right away");

        // All slots now active -- a further press must be a no-op, not a crash.
        const int selectedBefore = selected;
        driver.driveObjectManagement (selected, previous, pressA);
        check (selected == selectedBefore, "add: does nothing once every slot is already active");
    }

    {
        TrajectoryEngine engine (2);
        engine.activateObject (0);
        engine.activateObject (1);

        GamepadDriver driver (engine);
        int selected = 1;

        GamepadState previous;
        GamepadState pressB; pressB.buttonB = true;

        driver.driveObjectManagement (selected, previous, pressB);
        check (engine.getObject (1).inputChannel < 0, "remove: deactivates the selected slot");
        check (selected == -1, "remove: clears the selection");

        // Nothing selected -- a further press must be a no-op, not a crash.
        driver.driveObjectManagement (selected, previous, pressB);
        check (selected == -1, "remove: does nothing when nothing is selected");
    }

    // ==================================================================
    // driveThrowGesture: Free Throw (Button Y, held)
    // ==================================================================
    {
        TrajectoryEngine engine (2);
        engine.activateObject (0);
        engine.activateObject (1);
        GamepadDriver driver (engine);

        GamepadState idle;
        GamepadState held; held.buttonY = true;

        // Press: starts aiming, but must not touch the object at all yet.
        driver.driveThrowGesture (0, idle, held);
        check (engine.getObject (0).mode == SoundObject::Mode::Static, "FreeThrow: pressing the button alone doesn't move/launch the object yet");

        // Still held, stick wandering around -- must still do nothing.
        GamepadState aiming = held; aiming.leftStickY = 0.4f;
        driver.driveThrowGesture (0, held, aiming);
        check (engine.getObject (0).mode == SoundObject::Mode::Static, "FreeThrow: still-held ticks with the stick moving don't fire early");

        // Release with the stick pushed fully "up" (+X direction, see
        // driveThrowGesture()'s own world-space convention comment).
        GamepadState releaseAimed; releaseAimed.leftStickY = 1.0f;
        driver.driveThrowGesture (0, aiming, releaseAimed);

        const float expectedPull = driver.getThrowMaxPullDistance();
        const Vec3 expectedVelocity { expectedPull * SlingGesture::throwVelocityScale, 0.0f, 0.0f };
        check (engine.getObject (0).mode == SoundObject::Mode::Impulse, "FreeThrow: releasing fires the throw (mode -> Impulse)");
        check (approxEqual (engine.getObject (0).velocity, expectedVelocity, 0.05f),
               "FreeThrow: velocity direction/magnitude matches full-deflection pull distance * throwVelocityScale");
    }

    // --- Below the minimum pull distance: releasing fires nothing at all ---
    {
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        GamepadDriver driver (engine);

        GamepadState idle;
        GamepadState held; held.buttonY = true;
        driver.driveThrowGesture (0, idle, held);

        // A tiny deflection, inside the driver's own deadzone -> shapes to exactly 0.
        GamepadState releaseTiny; releaseTiny.leftStickY = 0.03f;
        driver.driveThrowGesture (0, held, releaseTiny);

        check (engine.getObject (0).mode == SoundObject::Mode::Static, "FreeThrow: releasing with a below-deadzone deflection fires nothing (mode unchanged)");
        check (approxEqual (engine.getObject (0).velocity, { 0.0f, 0.0f, 0.0f }), "FreeThrow: releasing with a below-deadzone deflection leaves velocity untouched");
    }

    // --- Nothing selected when the button is pressed: no aim ever starts ---
    {
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        GamepadDriver driver (engine);

        GamepadState idle;
        GamepadState held; held.buttonY = true;
        driver.driveThrowGesture (-1, idle, held); // nothing selected at press time

        GamepadState releaseAimed; releaseAimed.leftStickY = 1.0f;
        // Even though object 0 is passed as "selected" on release, no aim
        // was ever started (selection was -1 at press time), so this must
        // still do nothing.
        driver.driveThrowGesture (0, held, releaseAimed);

        check (engine.getObject (0).mode == SoundObject::Mode::Static, "FreeThrow: pressing with nothing selected never starts an aim, even if release later passes a valid index");
    }

    // ==================================================================
    // driveThrowGesture: Orbit Shot (Left Shoulder, held)
    // ==================================================================
    {
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        GamepadDriver driver (engine);

        GamepadState idle;
        GamepadState held; held.leftShoulder = true;
        driver.driveThrowGesture (0, idle, held);

        GamepadState releaseAimed; releaseAimed.leftStickY = 1.0f; // same +X aim as the FreeThrow case
        driver.driveThrowGesture (0, held, releaseAimed);

        auto& obj = engine.getObject (0);
        const float expectedPull = driver.getThrowMaxPullDistance();
        check (obj.mode == SoundObject::Mode::Orbit, "OrbitShot: releasing starts an orbit (mode -> Orbit)");
        check (approxEqual (obj.orbitCenter, { 0.0f, 0.0f, 0.0f }), "OrbitShot: always centers on the world origin (no reference-object cycling equivalent)");
        check (obj.orbitReferenceObjectId == -1, "OrbitShot: no live reference object (fixed-point orbit)");
        check (approxEqual (obj.orbitEccentricity, 0.0f), "OrbitShot: always circular (no eccentricity-cycling equivalent)");
        check (approxEqual (obj.orbitRadius, expectedPull * SlingGesture::orbitRadiusScale, 0.05f), "OrbitShot: orbit radius matches the pull distance * orbitRadiusScale");
        check (approxEqual (std::abs (obj.orbitAngularSpeed), SlingGesture::orbitAngularSpeedMagnitude, 0.01f), "OrbitShot: angular speed magnitude matches SlingGesture::orbitAngularSpeedMagnitude");
    }

    // ==================================================================
    // driveThrowGesture: Slingshot (Right Shoulder, held)
    // ==================================================================
    {
        // Two active objects -- object 1 is the only other candidate, so
        // it must be auto-targeted (same fallback the mouse gesture uses).
        TrajectoryEngine engine (2);
        engine.activateObject (0);
        engine.activateObject (1);
        GamepadDriver driver (engine);

        GamepadState idle;
        GamepadState held; held.rightShoulder = true;
        driver.driveThrowGesture (0, idle, held);

        GamepadState releaseAimed; releaseAimed.leftStickY = 1.0f;
        driver.driveThrowGesture (0, held, releaseAimed);

        auto& obj = engine.getObject (0);
        check (obj.mode == SoundObject::Mode::Impulse, "Slingshot: releasing fires the throw (mode -> Impulse)");
        check (obj.slingshotTargetId == 1, "Slingshot: auto-targets the only other active object");
        check (approxEqual (obj.slingshotStrength, SlingGesture::slingshotGravityStrength), "Slingshot: uses SlingGesture::slingshotGravityStrength");
    }

    {
        // Only one active object -- no candidate exists, so this must
        // degrade gracefully to a plain, unaffected throw (strength 0).
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        GamepadDriver driver (engine);

        GamepadState idle;
        GamepadState held; held.rightShoulder = true;
        driver.driveThrowGesture (0, idle, held);

        GamepadState releaseAimed; releaseAimed.leftStickY = 1.0f;
        driver.driveThrowGesture (0, held, releaseAimed);

        auto& obj = engine.getObject (0);
        check (obj.mode == SoundObject::Mode::Impulse, "Slingshot (no other object): still fires as a throw");
        check (obj.slingshotTargetId == -1, "Slingshot (no other object): no target to pull toward");
        check (approxEqual (obj.slingshotStrength, 0.0f), "Slingshot (no other object): strength degrades to 0 (plain Free Throw feel)");
    }

    // --- Priority when two throw buttons are pressed on the same tick ---
    {
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        GamepadDriver driver (engine);

        GamepadState idle;
        GamepadState bothHeld; bothHeld.buttonY = true; bothHeld.leftShoulder = true;
        driver.driveThrowGesture (0, idle, bothHeld);

        // Releasing Y alone (leftShoulder still held) must still fire --
        // Free Throw won the simultaneous press, so only ITS button's
        // release ends the gesture, not "any throw button now up".
        GamepadState yReleasedOnly; yReleasedOnly.leftShoulder = true; yReleasedOnly.leftStickY = 1.0f;
        driver.driveThrowGesture (0, bothHeld, yReleasedOnly);

        check (engine.getObject (0).mode == SoundObject::Mode::Impulse, "simultaneous press: Free Throw wins priority and fires on ITS OWN button's release");
        check (engine.getObject (0).slingshotTargetId == -1, "simultaneous press: fired as Free Throw, not Slingshot (no slingshot target set)");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
