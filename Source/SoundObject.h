#pragma once
#include <juce_core/juce_core.h>
#include "Vec3.h"

/**
    State of a single sound object in space.

    Position in Cartesian coordinates (meters, right-handed):
        x = front/back (front positive)
        y = left/right (left positive)
        z = up/down    (up positive)

    Updated by TrajectoryEngine (control rate, ~60-120 Hz) and read by
    AmbisonicsEncoder (audio rate). Not audio data itself, just metadata --
    the actual signal comes separately via the assigned input channel index
    (inputChannel).
*/
struct SoundObject
{
    int id = -1;

    // Which live input channel (0-based) feeds this object.
    // -1 = no input assigned (object silent / just a placeholder).
    int inputChannel = -1;

    Vec3 position   { 1.0f, 0.0f, 0.0f }; // starting position: 1m in front
    Vec3 velocity   { 0.0f, 0.0f, 0.0f };
    float mass = 1.0f; // for n-body attraction/repulsion

    // Motion mode, evaluated by TrajectoryEngine
    enum class Mode
    {
        Static,       // stays at position (e.g. dragged by mouse)
        Manual,       // currently being moved live via mouse/MIDI, no physics
        Orbit,        // circles around orbitCenter with orbitRadius/orbitSpeed
        Impulse,      // was "thrown", moves freely under velocity + force field
        Attracted     // subject to n-body forces from other objects/points
    };
    Mode mode = Mode::Static;

    // Orbit mode parameters
    Vec3 orbitCenter { 0.0f, 0.0f, 0.0f };
    float orbitRadius = 1.0f;
    float orbitAngularSpeed = 1.0f; // rad/s
    float orbitPhase = 0.0f;        // current angle, advanced over time

    // For attraction/repulsion: strength, negative sign = repulsive.
    // Applies when THIS object acts as a source on others (see also
    // forceExponent/minDistance/maxRange/attractionPulse* below -- all of
    // them are also properties of the source, not of the attracted object).
    float attractionStrength = 0.0f;

    // Friction/damping for Impulse mode, 0 = no damping, 1 = stops instantly.
    // Simple multiplicative decay per simulation step (cheap, but step-rate
    // dependent). For a more physically consistent, velocity-proportional
    // brake, see dragCoefficient.
    float damping = 0.02f;

    float gain = 1.0f; // manual per-object gain, in addition to distance attenuation

    // --- Inertia / motion limits ---------------------------------------
    // <= 0 = unlimited.
    float maxVelocity = 6.0f;
    // Real, velocity-proportional braking force (F = -dragCoefficient * velocity),
    // in addition to damping. 0 = off.
    float dragCoefficient = 0.0f;
    // Elasticity when bouncing off the room boundary (SceneSettings::roomSize,
    // Reflect mode). 0 = the outward-facing velocity component is removed
    // (object at most slides tangentially along the wall), 1 = perfectly
    // elastic bounce.
    float restitution = 0.6f;
    // Velocities below this magnitude are hard-snapped to 0.
    // Without this, a damped object only approaches rest asymptotically
    // (never mathematically comes to a full stop).
    float velocitySnapThreshold = 0.01f;

    // --- n-body refinement (applies when this object acts as a source) -----
    // Exponent in the force law, 2 = classic inverse-square law
    // (default behavior, unchanged from earlier versions).
    float forceExponent = 2.0f;
    // Softening radius, prevents hard force spikes at very small
    // distances (replaces the previous global constant of the same name).
    float minDistance = 0.05f;
    // Cutoff radius beyond which this source no longer exerts any force.
    // <= 0 = unlimited range.
    float maxRange = 0.0f;
    // Periodic modulation of attractionStrength: effective strength =
    // attractionStrength * (1 + attractionPulseDepth * sin(phase)).
    // attractionPulseRate = 0 (default) => no modulation.
    float attractionPulseRate = 0.0f;  // Hz
    float attractionPulseDepth = 0.0f; // 0..1
    float attractionPulsePhase = 0.0f; // runtime state, not a starting parameter

    // --- Orbit extensions ------------------------------------------------
    // Normal vector of the orbit plane, default {0,0,1} = previous
    // behavior (circle/ellipse in the x/y plane).
    Vec3 orbitPlaneNormal { 0.0f, 0.0f, 1.0f };
    // 0 = circular orbit, <1 = ellipse. Simplified approximation (fixed
    // semi-axes orbitRadius/orbitRadius*(1-e), not a focus-based Kepler
    // orbit with variable angular speed) -- deliberately kept simple for
    // the POC.
    float orbitEccentricity = 0.0f;
    // Radius change per second while in Orbit mode, 0 = stable orbit.
    float orbitDecay = 0.0f;
    // -1 = orbitCenter is a fixed point (previous behavior). Otherwise the
    // id of another SoundObject to orbit around (e.g. moon-around-planet
    // hierarchies).
    int orbitReferenceObjectId = -1;
};
