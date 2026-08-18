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
