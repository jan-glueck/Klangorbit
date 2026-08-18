#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/TrajectoryEngine.h"
#include "../Source/SlingGesture.h"

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

    // Runs one Orbit-mode integration step (matches TrajectoryEngine::integrate()'s
    // step size assumption closely enough for a single dt) and returns the
    // resulting position.
    Vec3 orbitPositionAfterOneStep (Vec3 center, float semiMajorAxis, float angularSpeed,
                                     float eccentricity, float orientation, double dt = 0.001)
    {
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        engine.startOrbit (0, center, semiMajorAxis, angularSpeed, eccentricity, orientation);
        engine.update (dt);
        return engine.getObject (0).position;
    }
}

int main()
{
    const Vec3 center { 0.0f, 0.0f, 0.0f };
    constexpr float semiMajor = 2.0f;
    constexpr float eccentricity = 0.6f; // semiMinor = 0.4 * semiMajor

    // --- Circular orbit (eccentricity 0) is unaffected by orbitOrientation ---
    // A circle has no distinguishable axis -- rotating the basis must not
    // change its shape, only re-parameterize which phase maps to which point.
    {
        bool allOnCircle = true;
        for (float orientationDeg : { 0.0f, 37.0f, 90.0f, 180.0f, 271.0f })
        {
            const float orientation = juce::degreesToRadians (orientationDeg);
            const Vec3 pos = orbitPositionAfterOneStep (center, semiMajor, 1.0f, 0.0f, orientation);
            const float dist = (pos - center).length();
            if (! approxEqual (dist, semiMajor, 0.01f))
                allOnCircle = false;
        }
        check (allOnCircle, "circular orbit (eccentricity=0) stays on the reference circle regardless of orbitOrientation");
    }

    // --- orbitOrientation=0 reproduces the pre-existing ellipse formula ---
    // (self-consistency: phase=0 is always the semiMajor-distance point on
    // the ellipse, i.e. exactly semiMajor away from the center, whatever the
    // basis direction is.)
    {
        const Vec3 pos = orbitPositionAfterOneStep (center, semiMajor, 1.0f, eccentricity, 0.0f);
        const float dist = (pos - center).length();
        check (approxEqual (dist, semiMajor, 0.01f),
               "orbitOrientation=0, phase~0: ellipse's major-axis point is exactly semiMajorAxis from the center");
    }

    // --- orbitOrientation rotates the major axis by exactly that angle ---
    {
        const Vec3 baseline = orbitPositionAfterOneStep (center, semiMajor, 1.0f, eccentricity, 0.0f) - center;
        const float baseAngle = std::atan2 (baseline.y, baseline.x);

        bool allRotatedCorrectly = true;
        for (float orientationDeg : { 30.0f, 90.0f, 180.0f, -45.0f })
        {
            const float orientation = juce::degreesToRadians (orientationDeg);
            const Vec3 rotated = orbitPositionAfterOneStep (center, semiMajor, 1.0f, eccentricity, orientation) - center;
            const float rotatedAngle = std::atan2 (rotated.y, rotated.x);

            // Compare angles modulo 2*pi via the wrapped difference.
            float diff = rotatedAngle - baseAngle - orientation;
            while (diff > juce::MathConstants<float>::pi)  diff -= juce::MathConstants<float>::twoPi;
            while (diff < -juce::MathConstants<float>::pi) diff += juce::MathConstants<float>::twoPi;

            if (! approxEqual (diff, 0.0f, 0.01f))
                allRotatedCorrectly = false;
        }
        check (allRotatedCorrectly, "orbitOrientation rotates the ellipse's major axis by exactly the given angle");
    }

    // --- startOrbit() default arguments reproduce the old (pre-ellipse-shot) behavior ---
    {
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        engine.startOrbit (0, center, semiMajor, 1.0f); // old 4-argument call, as still used by the double-click gesture
        auto& obj = engine.getObject (0);
        check (approxEqual (obj.orbitEccentricity, 0.0f) && approxEqual (obj.orbitOrientation, 0.0f),
               "startOrbit() without eccentricity/orientation args defaults to a circular, unrotated orbit");
    }

    // --- startOrbit() resets a stale orbitReferenceObjectId ---
    // Regression test: startOrbit() always passes an explicit fixed
    // center, but integrate() prefers a valid orbitReferenceObjectId's
    // live position over orbitCenter if one is set. A leftover reference
    // id from earlier editing must not silently hijack the center
    // startOrbit() was just told to use (affects both the double-click
    // gesture and the sling orbit-shot, which both document "always
    // centered on the origin").
    {
        TrajectoryEngine engine (2);
        engine.activateObject (0);
        engine.activateObject (1);
        engine.getObject (1).position = { 5.0f, 5.0f, 0.0f }; // far from the intended center

        engine.getObject (0).orbitReferenceObjectId = 1; // stale, set before startOrbit()
        engine.startOrbit (0, center, semiMajor, 1.0f);

        check (engine.getObject (0).orbitReferenceObjectId < 0,
               "startOrbit() resets a pre-existing orbitReferenceObjectId to -1");

        engine.update (0.001);
        const float distFromIntendedCenter = (engine.getObject (0).position - center).length();
        check (distFromIntendedCenter < semiMajor + 0.5f,
               "startOrbit() orbits the given center, not a stale orbitReferenceObjectId's position");
    }

    // --- startOrbit(): referenceObjectId targets another object's LIVE position ---
    // ("slingshot" mode, see PluginEditor's sling gesture -- orbiting a
    // moving body instead of a fixed point in space.)
    {
        TrajectoryEngine engine (2);
        engine.activateObject (0); // the "planet" -- given a velocity so its position actually moves
        engine.activateObject (1); // the orbiting/slingshot object

        engine.getObject (0).mode = SoundObject::Mode::Impulse;
        engine.getObject (0).position = { 5.0f, 0.0f, 0.0f };
        engine.getObject (0).velocity = { 1.0f, 0.0f, 0.0f }; // moves steadily along +x

        engine.startOrbit (1, engine.getObject (0).position, 2.0f, 1.0f, 0.0f, 0.0f, /*referenceObjectId*/ 0);
        check (engine.getObject (1).orbitReferenceObjectId == 0,
               "startOrbit(): a valid referenceObjectId is stored on the orbiting object");

        for (int i = 0; i < 100; ++i)
            engine.update (0.01); // 1 second -- the "planet" has moved from (5,0,0) to ~(6,0,0) by now

        const float distFromMovingPlanet = (engine.getObject (1).position - engine.getObject (0).position).length();
        check (distFromMovingPlanet < 2.0f + 0.5f,
               "startOrbit() with a referenceObjectId keeps orbiting the reference object's LIVE (moved) position, not its position at call time");
    }
    {
        // -1 (the default) must reproduce the previous unconditional
        // behavior exactly -- same regression this file already checks
        // above, re-verified here specifically for the new parameter's
        // own default value rather than the older 4/5-argument call forms.
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        engine.getObject (0).orbitReferenceObjectId = 0; // leftover from earlier editing
        engine.startOrbit (0, { 3.0f, 0.0f, 0.0f }, 1.0f, 1.0f);
        check (engine.getObject (0).orbitReferenceObjectId == -1,
               "startOrbit() with the default referenceObjectId (-1) still resets any leftover reference id");
    }

    // --- SlingGesture: pure math helpers ---
    {
        const Vec3 anchor { 1.0f, 2.0f, 0.0f };
        const Vec3 releasePoint { 0.5f, 1.0f, 0.0f };
        const Vec3 pull = SlingGesture::computePullVector (anchor, releasePoint);
        const Vec3 expected = anchor - releasePoint;
        check (approxEqual (pull.x, expected.x) && approxEqual (pull.y, expected.y),
               "computePullVector() returns anchor - releasePoint");
    }
    {
        // Object to the "right" of the center (+x); launching "up" (+y)
        // should impart a counter-clockwise spin (positive sign).
        const float signCcw = SlingGesture::computeOrbitDirectionSign ({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
        check (signCcw > 0.0f, "computeOrbitDirectionSign(): pulling to one side yields a positive (CCW) sign");

        // Same anchor, opposite launch direction ("down") should flip the sign.
        const float signCw = SlingGesture::computeOrbitDirectionSign ({ 1.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f });
        check (signCw < 0.0f, "computeOrbitDirectionSign(): launching to the opposite side flips to negative (CW)");
    }
    {
        // Pull vector pointing along +x (0 rad) and +y (pi/2) should give
        // the expected orientation angles.
        const float orientationAlongX = SlingGesture::computeOrbitOrientation ({ 1.0f, 0.0f, 0.0f });
        const float orientationAlongY = SlingGesture::computeOrbitOrientation ({ 0.0f, 1.0f, 0.0f });
        check (approxEqual (orientationAlongX, 0.0f),
               "computeOrbitOrientation() of a pull vector along +x is 0 rad");
        check (approxEqual (orientationAlongY, juce::MathConstants<float>::halfPi),
               "computeOrbitOrientation() of a pull vector along +y is pi/2 rad");
    }
    {
        // No other active objects at all -- only "Center" (-1) exists to
        // cycle to/from. Must never get stuck or crash.
        const std::vector<int> noOthers {};
        const int next = SlingGesture::cycleSlingReference (-1, noOthers, /*selfId*/ 0);
        check (next == -1, "cycleSlingReference(): with no other objects, cycling from Center stays at Center");
    }
    {
        // Two other active objects (ids 1, 2) besides the slung object
        // itself (id 0): cycling from Center should visit 1, then 2, then
        // wrap back to Center. selfId must never appear in the cycle.
        const std::vector<int> activeIds { 0, 1, 2 };
        int current = -1;
        current = SlingGesture::cycleSlingReference (current, activeIds, 0);
        check (current == 1, "cycleSlingReference(): first press after Center selects the first other active object");
        current = SlingGesture::cycleSlingReference (current, activeIds, 0);
        check (current == 2, "cycleSlingReference(): second press selects the next other active object");
        current = SlingGesture::cycleSlingReference (current, activeIds, 0);
        check (current == -1, "cycleSlingReference(): cycling past the last object wraps back to Center");
    }
    {
        // A stale `current` (e.g. an object deactivated mid-gesture, no
        // longer in the active list) must recover cleanly -- restarting
        // the cycle instead of getting stuck on an id that no longer exists.
        const std::vector<int> activeIds { 0, 1 };
        const int next = SlingGesture::cycleSlingReference (/*current (stale)*/ 5, activeIds, 0);
        check (next == 1, "cycleSlingReference(): a stale current id recovers to the first real candidate instead of getting stuck");
    }

    // --- Sling gesture: Slingshot mode (real gravity assist, not a scripted path) ---
    {
        // throwObject() with a valid slingshotTargetId stores it (and a
        // nonzero strength) on the thrown object.
        TrajectoryEngine engine (2);
        engine.activateObject (0);
        engine.activateObject (1);
        engine.throwObject (0, { 1.0f, 0.0f, 0.0f }, /*slingshotTargetId*/ 1, /*strength*/ 5.0f);
        check (engine.getObject (0).slingshotTargetId == 1 && approxEqual (engine.getObject (0).slingshotStrength, 5.0f),
               "throwObject(): a valid slingshotTargetId/strength is stored on the thrown object");
    }
    {
        // The default (-1) must clear a stale slingshotTargetId left over
        // from an earlier throw -- same stale-state hazard startOrbit()
        // already guards against for orbitReferenceObjectId, now checked
        // here for throwObject() too.
        TrajectoryEngine engine (2);
        engine.activateObject (0);
        engine.activateObject (1);
        engine.throwObject (0, { 1.0f, 0.0f, 0.0f }, 1, 5.0f);
        engine.throwObject (0, { 1.0f, 0.0f, 0.0f }); // default args -- an ordinary Free Throw
        check (engine.getObject (0).slingshotTargetId == -1 && approxEqual (engine.getObject (0).slingshotStrength, 0.0f),
               "throwObject() with the default slingshotTargetId (-1) clears a leftover pull from a previous throw");
    }
    {
        // The actual gravity-assist physics: with a slingshot target off
        // to the side of the initial straight-line path, only the PULLED
        // object's velocity should gain a sideways component toward it --
        // i.e. computeAttractionForce()'s slingshot contribution is
        // really being integrated every tick, not just stored inertly on
        // the object. A short burst (well before damping/drift dominates
        // either simulation) keeps this a direct, robust force check
        // rather than a longer-horizon trajectory comparison.
        TrajectoryEngine plain (2), pulled (2);
        for (auto* e : { &plain, &pulled })
        {
            e->activateObject (0);
            e->activateObject (1);
            e->getObject (1).position = { 1.0f, 1.0f, 0.0f }; // "planet", off to the side
            e->getObject (1).mass = 2.0f;
        }
        plain.throwObject (0, { 2.0f, 0.0f, 0.0f });
        pulled.throwObject (0, { 2.0f, 0.0f, 0.0f }, /*slingshotTargetId*/ 1, SlingGesture::slingshotGravityStrength);

        for (int i = 0; i < 10; ++i)
        {
            plain.update (0.01);
            pulled.update (0.01);
        }

        const float plainY = plain.getObject (0).position.y;
        const float pulledY = pulled.getObject (0).position.y;
        std::printf ("       y-position after a short burst: unaffected=%.5f, slingshot=%.5f (planet at y=1)\n", plainY, pulledY);
        check (approxEqual (plainY, 0.0f, 1.0e-4f),
               "Slingshot test setup: an unaffected throw stays exactly on the x-axis (no sideways force acting on it)");
        check (pulledY > plainY,
               "Slingshot: the gravity pull measurably deflects the thrown object toward the target (gains a y-component toward it)");
    }
    {
        // Root-cause regression for the "flies almost straight into the
        // target and jitters there" bug report: an object's own
        // damping/dragCoefficient (tuned for ordinary decelerating throws)
        // must be IGNORED while a slingshot pull is active -- a real
        // gravity assist is frictionless. Give both objects heavy
        // damping/drag; the target is placed far enough away that its
        // actual pull force is negligible, isolating just the
        // damping-skip behavior (not the deflection itself, already
        // covered above).
        TrajectoryEngine undamped (2), damped (2);
        for (auto* e : { &undamped, &damped })
        {
            e->activateObject (0);
            e->activateObject (1);
            e->getObject (0).damping = 0.5f;
            e->getObject (0).dragCoefficient = 5.0f;
            e->getObject (1).position = { 1000.0f, 0.0f, 0.0f }; // far enough that its pull is negligible
        }
        damped.throwObject (0, { 2.0f, 0.0f, 0.0f }); // ordinary Free Throw -- damping/drag apply as before
        undamped.throwObject (0, { 2.0f, 0.0f, 0.0f }, /*slingshotTargetId*/ 1, 1.0f); // pull active (target too far to matter)

        for (int i = 0; i < 20; ++i)
        {
            damped.update (0.01);
            undamped.update (0.01);
        }

        const float damped_speed = damped.getObject (0).velocity.length();
        const float undamped_speed = undamped.getObject (0).velocity.length();
        std::printf ("       speed after 20 damped steps: ordinary throw=%.5f, slingshot pull=%.5f (initial=2.0)\n", damped_speed, undamped_speed);
        check (damped_speed < 1.0f,
               "Slingshot test setup: an ordinary throw with heavy damping/drag loses most of its speed");
        check (undamped_speed > 1.9f,
               "Slingshot: an active pull suppresses the object's own damping/dragCoefficient (frictionless space flight)");
    }
    {
        // simulateSlingshotPreview() with strength=0 (no target selected,
        // see PluginEditor::paint()) must collapse to a perfectly straight
        // line, matching the Free Throw preview it stands in for.
        const auto straight = SlingGesture::simulateSlingshotPreview (
            { 0.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }, { 0.0f, 5.0f, 0.0f },
            /*targetMass*/ 1.0f, /*strength*/ 0.0f, /*thrownMass*/ 1.0f, /*gravityConstant*/ 1.0f, 30);
        check (! straight.empty(), "simulateSlingshotPreview() test setup produced points");
        bool allOnStraightLine = true;
        for (auto& p : straight)
            if (std::abs (p.y) > 1.0e-4f || std::abs (p.z) > 1.0e-4f) // pure +x motion expected
                allOnStraightLine = false;
        check (allOnStraightLine, "simulateSlingshotPreview(): zero strength degrades to a perfectly straight line");
    }
    {
        // Nonzero strength must curve the previewed path toward the
        // target, and never produce NaN/Inf even close to the target.
        const auto curved = SlingGesture::simulateSlingshotPreview (
            { 0.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }, { 0.0f, 3.0f, 0.0f },
            /*targetMass*/ 2.0f, /*strength*/ SlingGesture::slingshotGravityStrength, /*thrownMass*/ 1.0f,
            /*gravityConstant*/ 1.0f, 60);

        bool allFinite = true;
        for (auto& p : curved)
            if (! std::isfinite (p.x) || ! std::isfinite (p.y) || ! std::isfinite (p.z))
                allFinite = false;
        check (allFinite, "simulateSlingshotPreview(): stays finite throughout, even close to the target");

        const float finalY = curved.back().y;
        check (finalY > 0.5f, "simulateSlingshotPreview(): nonzero strength visibly curves the path toward the target (off the straight +x line)");
    }

    // --- Mean-reverting orbit radius (Ornstein-Uhlenbeck process) ---
    {
        // Default (reversionRate=0, noiseAmplitude=0): must be a complete
        // no-op, i.e. orbitDecay-free existing behavior/presets are
        // unaffected -- radius stays exactly what startOrbit() set.
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        engine.startOrbit (0, center, 3.0f, 1.0f);
        for (int i = 0; i < 200; ++i)
            engine.update (0.01);
        check (approxEqual (engine.getObject (0).orbitRadius, 3.0f, 1.0e-4f),
               "orbitRadiusReversionRate=0 and orbitRadiusNoiseAmplitude=0 leave orbitRadius completely unaffected");
    }
    {
        // Pure deterministic reversion (noiseAmplitude=0): radius must
        // move monotonically toward the baseline, not away from it or in
        // circles -- confirms the sign of the reversion term.
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        engine.startOrbit (0, center, 5.0f, 1.0f); // start far from the baseline below
        engine.getObject (0).orbitRadiusBaseline = 1.0f;
        engine.getObject (0).orbitRadiusReversionRate = 5.0f; // strong enough to visibly converge within the test's short window

        float previousDistance = std::abs (engine.getObject (0).orbitRadius - 1.0f);
        bool alwaysCloserOrEqual = true;
        for (int i = 0; i < 50; ++i)
        {
            engine.update (0.01);
            const float distance = std::abs (engine.getObject (0).orbitRadius - 1.0f);
            if (distance > previousDistance + 1.0e-5f)
                alwaysCloserOrEqual = false;
            previousDistance = distance;
        }
        check (alwaysCloserOrEqual, "pure reversion (no noise) moves orbitRadius monotonically toward orbitRadiusBaseline");
        check (previousDistance < 0.5f, "pure reversion gets orbitRadius close to orbitRadiusBaseline within 0.5s");
    }
    {
        // Noise-only (reversionRate=0): radius must actually move (not
        // stuck), and must respect the hard clamp even with a large
        // amplitude -- never negative, never past the sanity ceiling.
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        engine.startOrbit (0, center, 1.0f, 1.0f);
        engine.getObject (0).orbitRadiusNoiseAmplitude = 50.0f; // deliberately large

        const float startRadius = engine.getObject (0).orbitRadius;
        bool everMoved = false;
        bool stayedInBounds = true;
        for (int i = 0; i < 500; ++i)
        {
            engine.update (0.01);
            const float r = engine.getObject (0).orbitRadius;
            if (std::abs (r - startRadius) > 1.0e-3f) everMoved = true;
            if (r < 0.05f - 1.0e-4f || r > 1000.0f + 1.0e-4f) stayedInBounds = false;
        }
        check (everMoved, "noise-only (no reversion) actually perturbs orbitRadius over time");
        check (stayedInBounds, "orbitRadius stays within its hard clamp [0.05, 1000] even with a large noise amplitude");
    }
    {
        // Combined reversion + noise: unlike orbitDecay (one-directional,
        // unbounded drift), this must stay statistically BOUNDED around
        // the baseline over a long run, not wander off permanently.
        // Theoretical OU stationary std-dev = noiseAmplitude/sqrt(2*rate);
        // checked against a generous multiple to avoid a flaky test while
        // still catching a grossly wrong implementation (inverted sign,
        // missing sqrt(dt) scaling, etc).
        TrajectoryEngine engine (1);
        engine.activateObject (0);
        engine.startOrbit (0, center, 2.0f, 1.0f);
        engine.getObject (0).orbitRadiusBaseline = 2.0f;
        engine.getObject (0).orbitRadiusReversionRate = 1.5f;
        engine.getObject (0).orbitRadiusNoiseAmplitude = 0.3f;

        const float theoreticalStdDev = 0.3f / std::sqrt (2.0f * 1.5f);
        float maxDeviation = 0.0f;
        constexpr int numSteps = 20000;
        for (int i = 0; i < numSteps; ++i)
        {
            engine.update (0.01);
            if (i > numSteps / 4) // discard the initial transient before judging the stationary distribution
                maxDeviation = juce::jmax (maxDeviation, std::abs (engine.getObject (0).orbitRadius - 2.0f));
        }
        check (maxDeviation < theoreticalStdDev * 8.0f,
               "combined reversion+noise stays statistically bounded near the baseline over a long run (not an unbounded drift)");
    }

    std::printf ("\n%s (%d failures)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
