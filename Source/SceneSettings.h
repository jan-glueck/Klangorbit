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
    // Purely a rendering toggle -- the boundary still applies physically
    // (reflect/wrap/absorb) even while hidden; only PluginEditor::paint()'s
    // drawShadedBoundarySphere() call is skipped when false.
    bool showRoomBoundary = true;

    // Constant force/mass (like wind/gravity), only affects objects in
    // Impulse/Attracted (the force-integrated modes) -- Orbit is defined
    // kinematically and an extra force would just look inconsistent there,
    // Manual/Static are moved externally/not at all.
    Vec3 globalField { 0.0f, 0.0f, 0.0f };

    float timeScale = 1.0f; // time dilation (>1 = fast-forward, <1 = slow-motion) for the whole simulation

    // --- Acoustic propagation (medium properties) -----------------------
    // Speed of sound, m/s. Physically ~343 at 20C (~331.3 + 0.606*temperature),
    // but deliberately kept independent of `temperature` below rather than
    // computed from it -- letting it drift from the physical value (e.g.
    // artificially down to 50 m/s) is an intentional creative tool: normal
    // movement speeds then produce strongly audible, surreal Doppler shift
    // and propagation delay instead of a naturalistic one. See
    // PropagationProcessor for how it's used.
    float speedOfSound = 343.0f;

    // Degrees Celsius. Only feeds the (simplified) air-absorption model
    // below, NOT speedOfSound (see above) -- kept independent on purpose.
    float temperature = 20.0f;
    // Percent, 0..100. Has the strongest, non-monotonic effect on air
    // absorption in the simplified model (peaks around medium humidity,
    // lower at both extremes -- counterintuitive but real, see
    // PropagationProcessor).
    float relativeHumidity = 50.0f;
    // kPa. Minor effect in the simplified absorption model, included for
    // completeness (real ISO 9613-1 absorption does depend on it, if only
    // weakly compared to humidity).
    float atmosphericPressure = 101.325f;

    // m/s, world-space. Shifts the effective speed of sound in the
    // propagation direction (source -> listener): a tailwind speeds up
    // arrival, a headwind slows/attenuates it. Physically real (sound is
    // carried by the medium) and also a distinctly non-standard creative
    // tool -- see PropagationProcessor.
    Vec3 windVector { 0.0f, 0.0f, 0.0f };
};
