#pragma once
#include "Vec3.h"
#include <array>
#include <cmath>

/**
    Pure math for the "sling" launch gesture (Shift+drag+release on an
    object in the 2D editor -- see PluginEditor). Header-only and free of
    any JUCE GUI types so the exact same code runs in the editor and in
    Tools/verify_orbit.cpp, rather than the test reimplementing the logic
    separately (same pattern as GrainRenderer.h).

    The gesture itself (mouse handling, modifier-key polling, drawing the
    bow line) lives in PluginEditor -- only the parts that are pure,
    testable math live here.
*/
namespace SlingGesture
{
    // One selectable orbit shape, cycled with the Alt key while Ctrl has
    // switched the gesture into orbit mode (see PluginEditor). Fixed,
    // discrete steps rather than continuous eccentricity control -- easy
    // to reason about mid-gesture without a numeric readout.
    struct EccentricityStep
    {
        float eccentricity;
        const char* label;
    };

    inline constexpr std::array<EccentricityStep, 4> orbitEccentricitySteps {{
        { 0.0f,  "Circular" },
        { 0.3f,  "Ellipse (light)" },
        { 0.6f,  "Ellipse (medium)" },
        { 0.85f, "Ellipse (extreme)" }
    }};

    // Below this pull distance (meters), releasing fires no shot at all --
    // treated the same as a plain click, so an accidental Shift+click
    // doesn't launch a near-zero-velocity throw. Mirrors the 3-pixel
    // threshold PluginEditor already uses to distinguish a selection click
    // from a real drag.
    constexpr float minPullDistanceMeters = 0.15f;

    // Free-throw launch speed (m/s) per meter pulled back.
    constexpr float throwVelocityScale = 3.0f;

    // Orbit semi-major axis (m) per meter pulled back -- deliberately 1:1,
    // so "how far you pull" reads directly as "how big the orbit is".
    constexpr float orbitRadiusScale = 1.0f;
    constexpr float minOrbitRadiusMeters = 0.2f;

    // Orbit angular speed magnitude (rad/s) for a sling-launched orbit --
    // constant, matching the existing double-click orbit gesture
    // (PluginEditor::mouseDoubleClick). Only the SIGN is derived from the
    // gesture (see computeOrbitDirectionSign below); scaling the speed
    // itself by pull distance too was deliberately left out to keep the
    // gesture's effect on the orbit predictable (distance -> size only).
    constexpr float orbitAngularSpeedMagnitude = 1.0f;

    // "Zugvektor" per the design spec: anchor minus release point. Points
    // in the launch direction (opposite of the drag itself); its length is
    // the pull distance.
    inline Vec3 computePullVector (Vec3 anchor, Vec3 releasePoint)
    {
        return anchor - releasePoint;
    }

    // Sign of the z-component of the 2D cross product between the anchor's
    // position relative to the orbit center and the launch direction --
    // physically, which way the throw imparts angular momentum around the
    // center. Positive => counter-clockwise (x/y plane, viewed from +z),
    // negative => clockwise. This derives the orbit's spin direction
    // straight from the gesture's own geometry instead of a separate
    // control: pulling to one side of the center vs. the other naturally
    // produces the opposite spin.
    inline float computeOrbitDirectionSign (Vec3 anchorRelativeToCenter, Vec3 launchVector)
    {
        const float crossZ = anchorRelativeToCenter.x * launchVector.y - anchorRelativeToCenter.y * launchVector.x;
        return crossZ >= 0.0f ? 1.0f : -1.0f;
    }

    // Orientation (radians) of a sling-launched ellipse's major axis: the
    // angle of the pull line itself (anchor -> release point), in the x/y
    // plane. The user is literally drawing the axis they want with the bow
    // line, so the ellipse's long axis lines up with what was drawn.
    inline float computeOrbitOrientation (Vec3 pullVector)
    {
        return std::atan2 (pullVector.y, pullVector.x);
    }
}
