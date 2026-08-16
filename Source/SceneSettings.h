#pragma once
#include "Vec3.h"

/**
    Parameters that apply to the whole scene (not per object).

    Spherical room boundary around the origin: roomSize <= 0 disables the
    boundary entirely (objects can then drift unbounded, as before). Applies
    to all modes except Manual -- while the mouse is actively dragging an
    object, it is not clamped, that would feel like resistance against the
    mouse.
*/
struct SceneSettings
{
    enum class BoundaryBehavior
    {
        Reflect, // bounce off the boundary (strength via SoundObject::restitution)
        Wrap,    // re-enter on the opposite side
        Absorb   // stop at the boundary, go silent (mode -> Static, gain -> 0)
    };

    float roomSize = 5.0f; // meters, radius of the sphere; <= 0 = no boundary
    BoundaryBehavior boundaryBehavior = BoundaryBehavior::Reflect;

    // Constant force/mass (like wind/gravity), only affects objects in
    // Impulse/Attracted (the force-integrated modes) -- Orbit is defined
    // kinematically and an extra force would just look inconsistent there,
    // Manual/Static are moved externally/not at all.
    Vec3 globalField { 0.0f, 0.0f, 0.0f };

    float timeScale = 1.0f; // time dilation (>1 = fast-forward, <1 = slow-motion) for the whole simulation
};
