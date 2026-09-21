#include <cstdio>
#include "../Source/GestureDriver.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    bool approx (float a, float b, float tol = 1.0e-4f) { return std::abs (a - b) <= tol; }

    juce::OSCMessage msg (const char* address, float slot, float a, float b)
    {
        juce::OSCMessage m (address);
        m.addInt32 ((juce::int32) slot);
        m.addFloat32 (a);
        m.addFloat32 (b);
        return m;
    }
}

int main()
{
    // --- Parsing ------------------------------------------------------------
    {
        GestureEvent e;
        check (GestureInterpretation::parse (msg ("/klangorbit/gesture/throw", 1, 0.5f, -0.25f), e), "throw: parses (int slot + two floats)");
        check (e.kind == GestureEvent::Kind::Throw && e.handSlot == 1 && approx (e.a, 0.5f) && approx (e.b, -0.25f), "throw: fields carried through");

        juce::OSCMessage floatSlot ("/klangorbit/gesture/slingshot");
        floatSlot.addFloat32 (0.0f); floatSlot.addFloat32 (0.1f); floatSlot.addFloat32 (0.2f);
        check (GestureInterpretation::parse (floatSlot, e) && e.kind == GestureEvent::Kind::Slingshot, "slingshot: a float slot argument is accepted too");

        check (GestureInterpretation::parse (msg ("/klangorbit/gesture/throw", 0, 3.0f, 4.0f), e)
               && approx (std::hypot (e.a, e.b), 1.0f) && approx (e.a, 0.6f) && approx (e.b, 0.8f), "throw: over-long aim vector is normalized to magnitude 1, direction kept");

        check (GestureInterpretation::parse (msg ("/klangorbit/gesture/orbit", 0, 5.0f, -0.3f), e)
               && e.kind == GestureEvent::Kind::Orbit && approx (e.a, 1.0f) && approx (e.b, -1.0f), "orbit: radius clamped to 1, direction reduced to -1");
        check (GestureInterpretation::parse (msg ("/klangorbit/gesture/orbit", 0, -2.0f, 0.0f), e) && approx (e.a, 0.0f) && approx (e.b, 1.0f), "orbit: negative radius clamps to 0, direction 0 counts as +1");

        check (GestureInterpretation::parse (msg ("/klangorbit/gesture/throw", 9, 0.1f, 0.1f), e) && e.handSlot == 1, "hand slot is clamped into 0..1");

        juce::OSCMessage tooShort ("/klangorbit/gesture/throw");
        tooShort.addInt32 (0); tooShort.addFloat32 (0.5f);
        check (! GestureInterpretation::parse (tooShort, e), "missing argument: rejected");

        juce::OSCMessage stringArg ("/klangorbit/gesture/throw");
        stringArg.addInt32 (0); stringArg.addString ("x"); stringArg.addFloat32 (0.5f);
        check (! GestureInterpretation::parse (stringArg, e), "non-numeric argument: rejected");

        check (! GestureInterpretation::parse (msg ("/klangorbit/gesture/unknown", 0, 0.1f, 0.1f), e), "unknown gesture name: rejected");
        check (! GestureInterpretation::parse (msg ("/klangorbit/hand/0/x", 0, 0.1f, 0.1f), e), "non-gesture address: not this driver's message");
        check (GestureInterpretation::isGestureAddress ("/klangorbit/gesture/whatever") && ! GestureInterpretation::isGestureAddress ("/klangorbit/hand/0/x"), "isGestureAddress: prefix match only");
    }

    // --- Execution against a real TrajectoryEngine ---------------------------
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);
        engine.activateObject (1);
        int selected = 0;
        GestureDriver driver (engine, [&selected] { return selected; });

        // aim up (aimY=+1) -> world +X (front), same as the gamepad's stick-up
        driver.execute ({ GestureEvent::Kind::Throw, 0, 0.0f, 1.0f });
        const auto& o0 = engine.getObject (0);
        check (o0.mode == SoundObject::Mode::Impulse, "throw: object switches to Impulse");
        check (o0.velocity.x > 1.0f && std::abs (o0.velocity.y) < 1.0e-3f, "throw: aim up launches toward +X (front), matching the gamepad stick convention");
        check (o0.slingshotTargetId == -1, "throw: no slingshot pull");

        // aim right (aimX=+1) -> world -Y
        driver.execute ({ GestureEvent::Kind::Throw, 1, 1.0f, 0.0f });
        check (engine.getObject (0).velocity.y < -1.0f, "throw: aim right launches toward -Y (this project's y=left-positive)");

        {
            const auto& o = engine.getObject (0);
            const float expected = juce::jmin (GestureActions::defaultMaxPullMeters * SlingGesture::throwVelocityScale, o.maxVelocity);
            check (approx (o.velocity.length(), expected, 0.05f), "throw: full-deflection speed = shared pull scale x throw scale (clamped by maxVelocity)");
        }

        // slingshot with another active object -> gravity pull toward it
        selected = 0;
        driver.execute ({ GestureEvent::Kind::Slingshot, 0, 0.0f, 1.0f });
        check (engine.getObject (0).slingshotTargetId == engine.getObject (1).id, "slingshot: auto-targets the other active object");
        check (approx (engine.getObject (0).slingshotStrength, SlingGesture::slingshotGravityStrength), "slingshot: uses the shared gravity strength");

        // orbit direction comes from the gesture
        driver.execute ({ GestureEvent::Kind::Orbit, 0, 0.5f, 1.0f });
        check (engine.getObject (0).mode == SoundObject::Mode::Orbit && engine.getObject (0).orbitAngularSpeed > 0.0f, "orbit: counter-clockwise gesture -> positive angular speed");
        driver.execute ({ GestureEvent::Kind::Orbit, 0, 0.5f, -1.0f });
        check (engine.getObject (0).orbitAngularSpeed < 0.0f, "orbit: clockwise gesture -> negative angular speed");
        check (approx (engine.getObject (0).orbitRadius, 0.5f * GestureActions::defaultMaxPullMeters), "orbit: radius = radius01 * max pull distance");

        // tiny aim -> ignored, like an accidental tap
        selected = 1;
        driver.execute ({ GestureEvent::Kind::Throw, 0, 0.01f, 0.01f });
        check (engine.getObject (1).mode != SoundObject::Mode::Impulse, "throw: below-minimum aim fires nothing");

        // nothing selected / inactive selection -> no-op
        selected = -1;
        const auto before = engine.getObject (1).velocity;
        driver.execute ({ GestureEvent::Kind::Throw, 0, 0.0f, 1.0f });
        check (engine.getObject (1).velocity.x == before.x, "nothing selected: gesture is a no-op");
        selected = 5;
        driver.execute ({ GestureEvent::Kind::Throw, 0, 0.0f, 1.0f });
        check (engine.getObject (5).mode != SoundObject::Mode::Impulse, "inactive selected object: gesture is a no-op");

        // handleMessage: consumes gesture addresses (even malformed), passes others through
        selected = 0;
        check (driver.handleMessage (msg ("/klangorbit/gesture/throw", 0, 0.0f, 1.0f)), "handleMessage: consumes a valid gesture message");
        juce::OSCMessage bad ("/klangorbit/gesture/throw");
        bad.addString ("nope");
        check (driver.handleMessage (bad), "handleMessage: consumes a malformed gesture message too (must not leak to controller interpretation)");
        check (! driver.handleMessage (juce::OSCMessage ("/klangorbit/hand/0/x", 0.5f)), "handleMessage: leaves ordinary OSC messages alone");
    }

    // --- Grab: per-hand object selection ---------------------------------------
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);
        engine.activateObject (1);
        engine.getObject (0).position = { 2.5f, 0.0f, 0.0f };   // in front  -> frame (aimX 0, aimY +0.5)
        engine.getObject (1).position = { 0.0f, -2.5f, 0.0f };  // to the right -> frame (aimX +0.5, aimY 0)
        engine.getSceneSettings().roomSize = 5.0f;

        int selected = -1;
        int callbackIndex = -99;
        GestureDriver driver (engine, [&selected] { return selected; });
        driver.setSelectionCallback ([&] (int i) { callbackIndex = i; selected = i; });

        GestureEvent e;
        check (GestureInterpretation::parse (msg ("/klangorbit/gesture/grab", 1, 0.25f, -0.75f), e)
               && e.kind == GestureEvent::Kind::Grab && e.handSlot == 1 && approx (e.a, 0.25f) && approx (e.b, -0.75f), "grab: parses (frame position carried through)");
        check (GestureInterpretation::parse (msg ("/klangorbit/gesture/grab", 0, 4.0f, -4.0f), e) && approx (e.a, 1.0f) && approx (e.b, -1.0f), "grab: frame position clamped to -1..1");

        driver.execute ({ GestureEvent::Kind::Grab, 0, 0.0f, 0.5f });
        check (callbackIndex == 0 && selected == 0, "grab: hand 0 over the front object picks it and reports the selection");
        driver.execute ({ GestureEvent::Kind::Grab, 1, 0.5f, 0.0f });
        check (callbackIndex == 1, "grab: hand 1 over the right-hand object picks that one");

        // each hand throws ITS OWN object, regardless of the global selection (now object 1)
        driver.execute ({ GestureEvent::Kind::Throw, 0, 0.0f, 1.0f });
        check (engine.getObject (0).mode == SoundObject::Mode::Impulse && engine.getObject (1).mode != SoundObject::Mode::Impulse, "hand 0's throw moves only its own object");
        driver.execute ({ GestureEvent::Kind::Throw, 1, 0.0f, 1.0f });
        check (engine.getObject (1).mode == SoundObject::Mode::Impulse, "hand 1's throw moves its own object");

        // a grab with nothing near changes nothing
        callbackIndex = -99;
        driver.execute ({ GestureEvent::Kind::Grab, 0, -1.0f, -1.0f });
        check (callbackIndex == -99, "grab: no object within the pick radius -> selection unchanged, no callback");

        // hand that never grabbed falls back to the global selection
        TrajectoryEngine engine2 (SAPOC_MAX_LIVE_INPUTS);
        engine2.activateObject (3);
        int sel2 = 3;
        GestureDriver driver2 (engine2, [&sel2] { return sel2; });
        driver2.execute ({ GestureEvent::Kind::Throw, 1, 0.0f, 1.0f });
        check (engine2.getObject (3).mode == SoundObject::Mode::Impulse, "un-grabbed hand acts on the globally selected object");

        // a grabbed object that gets deactivated falls back too
        engine.deactivateObject (1);
        selected = 0;
        engine.getObject (0).mode = SoundObject::Mode::Static;
        driver.execute ({ GestureEvent::Kind::Throw, 1, 0.0f, 1.0f });
        check (engine.getObject (0).mode == SoundObject::Mode::Impulse, "hand whose grabbed object was removed falls back to the global selection");
    }

    // --- Pause + tracking status -------------------------------------------------
    {
        TrajectoryEngine engine (SAPOC_MAX_LIVE_INPUTS);
        engine.activateObject (0);
        GestureDriver driver (engine, [] { return 0; });
        driver.setStatusTimeoutMs (60);

        check (! driver.getTrackingStatus().bridgeActive, "status: bridge is 'off' before any heartbeat arrived");

        juce::OSCMessage status ("/klangorbit/mediapipe/status", 1.0f);
        check (! driver.handleMessage (status), "status: heartbeat is observed but NOT consumed (still a Learn-mappable controller value)");
        check (driver.getTrackingStatus().bridgeActive && driver.getTrackingStatus().handsVisible == 0, "status: heartbeat -> bridge active, no hands yet");

        check (! driver.handleMessage (juce::OSCMessage ("/klangorbit/hand/1/visible", 1.0f)), "status: visible flag is observed, not consumed");
        check (driver.getTrackingStatus().handsVisible == 1, "status: one visible hand counted");
        driver.handleMessage (juce::OSCMessage ("/klangorbit/hand/0/visible", 1.0f));
        check (driver.getTrackingStatus().handsVisible == 2, "status: two visible hands counted");
        driver.handleMessage (juce::OSCMessage ("/klangorbit/hand/1/visible", 0.0f));
        check (driver.getTrackingStatus().handsVisible == 1, "status: a hand dropping out is counted again");

        juce::Thread::sleep (150);
        check (! driver.getTrackingStatus().bridgeActive && driver.getTrackingStatus().handsVisible == 0, "status: no heartbeat within the timeout -> bridge 'off' again");

        driver.setPaused (true);
        driver.handleMessage (msg ("/klangorbit/gesture/throw", 0, 0.0f, 1.0f));
        check (engine.getObject (0).mode != SoundObject::Mode::Impulse, "paused: gesture message is consumed but changes nothing");
        driver.setPaused (false);
        driver.handleMessage (msg ("/klangorbit/gesture/throw", 0, 0.0f, 1.0f));
        check (engine.getObject (0).mode == SoundObject::Mode::Impulse, "resumed: gestures act again");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
