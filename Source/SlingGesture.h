#pragma once
#include "Vec3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

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

    // Gravity-well strength (SoundObject::slingshotStrength) for the sling
    // gesture's Slingshot mode -- a fixed constant, deliberately NOT scaled
    // by pull distance like throwVelocityScale/orbitRadiusScale above:
    // pulling harder should just mean "faster" (same launch-speed formula
    // Free Throw already uses), not ALSO "more strongly attracted", which
    // would read as two gesture dimensions fighting for the same control.
    // How strongly a given encounter actually deflects the thrown object
    // is then a genuine, undictated consequence of the physics (approach
    // speed vs. this fixed strength vs. the target's own real Mass, which
    // the user can already dial in via the parameter panel) rather than
    // something this constant tries to guarantee on its own.
    constexpr float slingshotGravityStrength = 8.0f;

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

    // Advances the sling gesture's reference-target selection (see
    // PluginEditor::slingReferenceObjectId), tapped via Tab while the
    // gesture is active. -1 means the world origin ("Center"); any other
    // value is another active object's id. Shared by two of the sling
    // gesture's launch modes, each interpreting it differently: Orbit
    // Shot centers its scripted ellipse there (a fixed point, or another
    // object's LIVE position via `TrajectoryEngine::startOrbit()`'s
    // referenceObjectId parameter); Slingshot instead pulls the thrown
    // object toward it with real gravity (see
    // SoundObject::slingshotTargetId, TrajectoryEngine::throwObject()'s
    // slingshotTargetId parameter, and simulateSlingshotPreview() below).
    // "Center" has no gravity-well meaning for Slingshot -- selecting it
    // there just means no pull is applied, degrading gracefully to a
    // plain Free Throw rather than needing special-casing.
    //
    // Cycles through [-1 ("Center"), every id in activeObjectIds except
    // selfId], wrapping back to -1 after the last one. Rebuilding this
    // list from activeObjectIds on every call (rather than caching it once
    // per gesture) means activating/deactivating an object mid-gesture
    // can't leave a stale id in the cycle. If `current` isn't found in the
    // list (e.g. that object was deactivated since), cycling restarts from
    // -1 rather than getting stuck.
    inline int cycleSlingReference (int current, const std::vector<int>& activeObjectIds, int selfId)
    {
        std::vector<int> candidates { -1 };
        for (int id : activeObjectIds)
            if (id != selfId)
                candidates.push_back (id);

        const auto it = std::find (candidates.begin(), candidates.end(), current);
        const size_t pos = (it != candidates.end()) ? (size_t) std::distance (candidates.begin(), it) : 0;
        return candidates[(pos + 1) % candidates.size()];
    }

    // Forward-simulates a Slingshot throw's near-term path for the live
    // dashed preview while aiming (see PluginEditor::paint()) -- NOT a
    // general N-body simulator, just enough physics to preview what
    // release will actually do: the exact same inverse-square force law,
    // gravityLikeConstant, and softening floor
    // TrajectoryEngine::computeAttractionForce() uses for
    // SoundObject::slingshotTargetId, integrated forward with simple
    // (symplectic) Euler steps at a small fixed dt.
    //
    // The target is treated as fixed at targetPos for the whole preview
    // horizon rather than also simulating ITS motion -- a deliberate
    // simplification for a short (couple-of-seconds) look-ahead, in the
    // same spirit as the Free Throw preview's own documented
    // simplifications (no globalField/damping curvature there either).
    // strength == 0 (no target selected, see cycleSlingReference above)
    // degrades this to a perfectly straight line, i.e. the same preview
    // Free Throw already draws.
    inline std::vector<Vec3> simulateSlingshotPreview (Vec3 startPos, Vec3 initialVelocity,
                                                         Vec3 targetPos, float targetMass, float strength,
                                                         float thrownMass, float gravityConstant,
                                                         int numSteps = 90, float dt = 1.0f / 30.0f)
    {
        std::vector<Vec3> points;
        points.reserve ((size_t) numSteps + 1);

        Vec3 pos = startPos;
        Vec3 vel = initialVelocity;
        points.push_back (pos);

        constexpr float softening = 0.05f; // matches computeAttractionForce()'s own softening floor for this pull
        const float safeMass = std::max (thrownMass, 1.0e-3f);

        for (int i = 0; i < numSteps; ++i)
        {
            const Vec3 diff = targetPos - pos;
            const float dist = std::max (diff.length(), softening);
            const float magnitude = gravityConstant * strength * targetMass / (dist * dist);
            const Vec3 accel = (diff / dist) * (magnitude / safeMass);

            vel += accel * dt;
            pos += vel * dt;
            points.push_back (pos);
        }
        return points;
    }
}
