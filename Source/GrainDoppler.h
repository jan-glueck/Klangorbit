#pragma once
#include "Vec3.h"
#include <algorithm>
#include <cmath>

/**
    Optional per-grain Doppler pitch shift (see GrainCloudSettings::dopplerEnabled).
    Deliberately much simpler than PropagationProcessor's per-object Doppler
    (a variable delay line whose rate of change produces the pitch shift as
    a side effect, plus air absorption/directivity): grains skip
    PropagationProcessor entirely for performance (see PluginProcessor's
    grain-rendering comment), so this computes a single, per-block-constant
    pitch RATIO directly from the classic Doppler formula and the grain's
    already-known position/velocity, no delay line involved.

    Header-only and JUCE-GUI-free (only <cmath>/<algorithm>), same spirit
    as SlingGesture.h/OrbitMath.h -- shared, not reimplemented, between
    PluginProcessor and Tools/verify_grain_cloud.cpp.
*/
namespace GrainDoppler
{
    // Classic Doppler ratio: speedOfSound / (speedOfSound - radialVelocity),
    // where radialVelocity is positive when the grain moves TOWARD the
    // listener (at the world origin). dopplerFactor scales the velocity
    // term before computing the ratio (0 = no shift, 1 = physical, >1 =
    // exaggerated) -- the same semantics SoundObject::dopplerFactor
    // already has for main objects, so it's one familiar knob, not a
    // second one.
    //
    // Clamped defensively: the denominator can't collapse toward zero
    // (which would happen at/near the classic "sonic boom" condition, or
    // trivially if SceneSettings::speedOfSound is set artistically low),
    // and the final ratio is bounded to a sane audible range -- grains are
    // short-lived and numerous, so a single wildly mispitched grain is a
    // much more noticeable artifact than a softly clamped one.
    inline float computeDopplerRatio (Vec3 grainPosition, Vec3 grainVelocity, float dopplerFactor, float speedOfSound)
    {
        const float safeSpeedOfSound = std::max (1.0f, speedOfSound);
        const float distance = std::max (grainPosition.length(), 0.01f);
        const Vec3 dirToListener = (-grainPosition) / distance; // listener at the origin

        const float radialVelocityTowardListener = grainVelocity.dot (dirToListener) * dopplerFactor;
        const float denominator = std::max (0.1f * safeSpeedOfSound, safeSpeedOfSound - radialVelocityTowardListener);

        const float ratio = safeSpeedOfSound / denominator;
        return std::min (4.0f, std::max (0.25f, ratio));
    }
}
