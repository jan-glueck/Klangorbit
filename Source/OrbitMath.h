#pragma once
#include "Vec3.h"
#include "SoundObject.h"
#include <algorithm>
#include <cmath>

/**
    Pure math for a SoundObject's Orbit-mode ellipse, shared between
    TrajectoryEngine (actual physics integration, control rate) and
    PluginEditor (sampling points along the same curve to draw an orbit-path
    preview) -- same "shared, not reimplemented" principle as
    SlingGesture.h/GrainRenderer.h. Header-only, no JUCE dependency beyond
    what SoundObject.h itself already needs.
*/
namespace OrbitMath
{
    struct Basis { Vec3 u, v; };

    // Orthonormal basis of the orbit plane (SoundObject::orbitPlaneNormal),
    // rotated within that plane by orbitOrientation. Default
    // orbitPlaneNormal={0,0,1} + orbitOrientation=0 reproduces the original
    // x/y-plane ellipse orientation this formula always used.
    inline Basis computeBasis (const SoundObject& obj)
    {
        Vec3 n = obj.orbitPlaneNormal;
        const float nLen = n.length();
        n = (nLen > 1.0e-6f) ? (n / nLen) : Vec3 { 0.0f, 0.0f, 1.0f };
        const Vec3 arbitrary = (std::abs (n.z) < 0.9f) ? Vec3 { 0.0f, 0.0f, 1.0f } : Vec3 { 1.0f, 0.0f, 0.0f };
        Vec3 u = cross (arbitrary, n);
        u = u / std::max (u.length(), 1.0e-6f);
        const Vec3 v = cross (n, u);

        const float cosO = std::cos (obj.orbitOrientation);
        const float sinO = std::sin (obj.orbitOrientation);
        return { u * cosO + v * sinO, v * cosO - u * sinO };
    }

    // Position on the ellipse at the given phase (radians). Simplified
    // ellipse (semi-axes derived from orbitRadius/orbitEccentricity, not a
    // focus-based Kepler orbit) -- see SoundObject.h.
    inline Vec3 computePosition (const SoundObject& obj, Vec3 center, float phase)
    {
        const Basis basis = computeBasis (obj);
        const float semiMajor = obj.orbitRadius;
        const float semiMinor = obj.orbitRadius * (1.0f - std::min (std::max (obj.orbitEccentricity, 0.0f), 0.95f));

        return center + basis.u * (semiMajor * std::cos (phase))
                       + basis.v * (semiMinor * std::sin (phase));
    }
}
