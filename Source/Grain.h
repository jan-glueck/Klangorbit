#pragma once
#include "Vec3.h"

/**
    How a grain moves for the duration of its (short) life. See
    GrainCloud::updateGrain() for the actual integration per mode.
*/
enum class GrainMovementMode
{
    RandomWalk,          // smoothed random-direction drift
    Bounce,               // bounces inside a small sphere around its spawn position
    RadialExplosion,      // flies outward from the parent's position at spawn time
    OrbitAroundParent,    // circles the parent's CURRENT (possibly moving) position
    AttractRepelSiblings  // n-body force from other active grains in the same cloud only
};

/** Grain amplitude envelope shape. Only Hann for now, deliberately extensible. */
enum class GrainWindowShape
{
    Hann
};

/**
    Which single movement-related field gets +/- jitterRange randomization
    per spawned grain, on top of the always-present audio-side pitchJitter/
    positionJitterInBuffer (those are separate, named fields -- this is a
    generic one-of-several mechanism for the movement parameters, since
    unlike the audio fields there isn't one obvious jitter target for
    movement).
*/
enum class GrainJitterTarget
{
    None,
    InitialSpeed,     // RadialExplosion
    Lifetime,         // all modes (== grainDuration for that grain)
    BoundaryRadius,   // Bounce
    OrbitRadius        // OrbitAroundParent
};

/**
    Cloud-wide parameters: one set applies to the whole cloud (all grains
    spawned by it), not per grain -- see ParameterPanel, there is no
    per-grain UI.

    A grain's audio envelope and its movement lifetime are the SAME
    duration (grainDuration): a grain is a single spawn-move-fade event,
    not a persistent point that repeatedly re-triggers audio. grainRate is
    how often NEW grains are spawned (classic granular-synthesis "density"
    parameter); grains overlap when grainRate * grainDuration > 1.
*/
struct GrainCloudSettings
{
    bool enabled = false;

    // --- Audio side ------------------------------------------------------
    float grainRate = 10.0f;                  // grains/sec spawned while enabled
    float grainDuration = 0.15f;              // seconds; also the grain's movement lifetime
    float pitchJitter = 0.0f;                 // 0..1, max random +/- playback-rate deviation per grain
    float positionJitterInBuffer = 0.05f;     // seconds, random look-back offset into the ring buffer per grain
    int maxConcurrentGrains = 8;              // per-cloud local cap (on top of the global cap, see PluginProcessor)
    GrainWindowShape windowShape = GrainWindowShape::Hann;

    // --- Movement side ----------------------------------------------------
    GrainMovementMode movementMode = GrainMovementMode::RandomWalk;

    float randomWalkSpeed = 1.0f;             // m/s, RandomWalk

    float boundaryRadius = 1.0f;              // m, Bounce -- sphere around the grain's spawn position
    float restitution = 0.6f;                 // Bounce, 0..1

    float initialSpeed = 2.0f;                // m/s, RadialExplosion
    float acceleration = 0.0f;                // m/s^2 along the explosion direction, RadialExplosion

    float orbitRadius = 0.5f;                 // m, OrbitAroundParent
    float orbitAngularSpeed = 2.0f;           // rad/s, OrbitAroundParent

    float attractionStrength = 1.0f;          // AttractRepelSiblings, negative = repel

    GrainJitterTarget jitterTarget = GrainJitterTarget::None;
    float jitterRange = 0.0f;                 // 0..1, fractional +/- deviation applied to jitterTarget at spawn
};

/**
    Lightweight, pool-managed grain state -- a single spawn-move-fade
    event, not a persistent object. GrainCloud pre-allocates a fixed pool
    and activates/deactivates slots in place; nothing here is ever
    heap-allocated after that (no std::vector members, no pointers).
*/
struct Grain
{
    bool active = false;
    float age = 0.0f;             // seconds since spawn
    float lifetimeSeconds = 0.0f; // grainDuration at spawn time, +/- jitter if targeted
    // Bumped every time this pool slot is (re)spawned, so the audio thread
    // can detect "this is a new grain, not a continuation" even if the
    // slot never reported inactive in between (see PluginProcessor).
    int spawnGeneration = 0;

    Vec3 position { 0.0f, 0.0f, 0.0f };
    Vec3 velocity { 0.0f, 0.0f, 0.0f };

    // Mode-specific runtime state, only the fields relevant to the cloud's
    // current movementMode are meaningful for a given grain.
    float orbitPhase = 0.0f;                        // OrbitAroundParent
    float currentOrbitRadius = 0.0f;                // OrbitAroundParent, jittered at spawn
    Vec3 boundaryCenter { 0.0f, 0.0f, 0.0f };        // Bounce, fixed at spawn position
    float currentBoundaryRadius = 0.0f;             // Bounce, jittered at spawn
    Vec3 explosionDirection { 1.0f, 0.0f, 0.0f };    // RadialExplosion, fixed at spawn
    float currentAcceleration = 0.0f;               // RadialExplosion

    // Audio trigger parameters, captured once at spawn time and used by
    // the audio thread to play back this grain's windowed ring-buffer
    // snippet -- see PluginProcessor.
    int bufferReadStartSample = 0;   // position in the parent's ring buffer at spawn
    float playbackRate = 1.0f;       // pitch ratio (1 +/- pitchJitter deviation)
    int grainLengthSamples = 0;      // grainDuration in samples, fixed at spawn (sample-rate dependent)
};
