#include "GrainCloud.h"

namespace
{
    Vec3 randomUnitVector (juce::Random& rng)
    {
        const float z = rng.nextFloat() * 2.0f - 1.0f;
        const float azimuth = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        const float r = std::sqrt (juce::jmax (0.0f, 1.0f - z * z));
        return { r * std::cos (azimuth), r * std::sin (azimuth), z };
    }

    // +/- range fractional deviation, e.g. applyJitter (1.0f, 0.2f, rng) is in [0.8, 1.2].
    float applyJitter (float baseValue, float range, juce::Random& rng)
    {
        return baseValue * (1.0f + range * (rng.nextFloat() * 2.0f - 1.0f));
    }
}

GrainCloud::GrainCloud (int poolSize)
{
    grains.resize ((size_t) juce::jmax (0, poolSize));
    snapshotBuffer.resize (grains.size());
}

void GrainCloud::reset()
{
    settings = GrainCloudSettings {};
    for (auto& g : grains)
        g = Grain {};
    timeSinceLastSpawn = 0.0;

    juce::ScopedLock lock (snapshotLock);
    for (auto& s : snapshotBuffer)
        s = Snapshot {};
}

int GrainCloud::getNumActiveGrains() const
{
    int n = 0;
    for (auto& g : grains)
        if (g.active)
            ++n;
    return n;
}

void GrainCloud::setRingBufferContext (int writeHeadSample, double sampleRate)
{
    ringBufferWriteHeadSample = writeHeadSample;
    ringBufferSampleRate = sampleRate;
}

void GrainCloud::getSnapshot (std::vector<Snapshot>& out) const
{
    juce::ScopedLock lock (snapshotLock);
    out = snapshotBuffer;
}

Vec3 GrainCloud::computeSiblingForce (const Grain& g) const
{
    Vec3 force {};
    // Bigger softening floor than TrajectoryEngine's equivalent (0.05) --
    // grains in AttractRepelSiblings mode are spawned close together near
    // the parent (see spawnGrain), so near-coincident pairs are the norm
    // here, not a rare edge case; a small floor still lets 1/dist^2 spike
    // hard enough on the first tick to fling grains apart regardless of
    // attraction sign (see also the velocity clamp in updateGrain()).
    constexpr float minDistance = 0.15f;

    for (auto& other : grains)
    {
        if (&other == &g) continue;
        if (! other.active) continue;

        const Vec3 diff = other.position - g.position;
        float dist = diff.length();
        dist = juce::jmax (dist, minDistance);

        // Same inverse-square shape as TrajectoryEngine::computeAttractionForce,
        // simplified to one cloud-wide strength (no per-grain mass/exponent/range --
        // GrainCloudSettings only exposes attractionStrength).
        const float magnitude = settings.attractionStrength / (dist * dist);
        force += (diff / dist) * magnitude;
    }
    return force;
}

void GrainCloud::spawnGrain (int slot, Vec3 parentPosition, int writeHead, double sampleRate, juce::Random& rng)
{
    auto& g = grains[(size_t) slot];

    g.active = true;
    g.age = 0.0f;
    ++g.spawnGeneration;
    g.position = parentPosition;
    g.velocity = {};

    float lifetime = settings.grainDuration;
    if (settings.jitterTarget == GrainJitterTarget::Lifetime)
        lifetime = applyJitter (lifetime, settings.jitterRange, rng);
    g.lifetimeSeconds = juce::jmax (0.001f, lifetime);

    switch (settings.movementMode)
    {
        case GrainMovementMode::RandomWalk:
            g.velocity = randomUnitVector (rng) * settings.randomWalkSpeed;
            break;

        case GrainMovementMode::Bounce:
        {
            g.boundaryCenter = parentPosition;
            float radius = settings.boundaryRadius;
            if (settings.jitterTarget == GrainJitterTarget::BoundaryRadius)
                radius = applyJitter (radius, settings.jitterRange, rng);
            g.currentBoundaryRadius = juce::jmax (0.01f, radius);
            g.velocity = randomUnitVector (rng) * settings.randomWalkSpeed; // reused as initial bounce speed
            break;
        }

        case GrainMovementMode::RadialExplosion:
        {
            g.explosionDirection = randomUnitVector (rng);
            float speed = settings.initialSpeed;
            if (settings.jitterTarget == GrainJitterTarget::InitialSpeed)
                speed = applyJitter (speed, settings.jitterRange, rng);
            g.velocity = g.explosionDirection * speed;
            g.currentAcceleration = settings.acceleration;
            break;
        }

        case GrainMovementMode::OrbitAroundParent:
        {
            g.orbitPhase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            float radius = settings.orbitRadius;
            if (settings.jitterTarget == GrainJitterTarget::OrbitRadius)
                radius = applyJitter (radius, settings.jitterRange, rng);
            g.currentOrbitRadius = juce::jmax (0.01f, radius);
            g.position = parentPosition + Vec3 { std::cos (g.orbitPhase), std::sin (g.orbitPhase), 0.0f } * g.currentOrbitRadius;
            break;
        }

        case GrainMovementMode::AttractRepelSiblings:
            // Small random offset from the parent, NOT exactly at it --
            // several grains spawned in the same/adjacent control ticks
            // would otherwise start exactly coincident, and 1/dist^2 with
            // dist==0 produces a near-infinite force on the very first
            // tick (see computeSiblingForce's softening floor, which
            // alone isn't enough to fix this: the real problem is the
            // spawn position, not just the force law).
            g.position = parentPosition + randomUnitVector (rng) * 0.15f;
            g.velocity = randomUnitVector (rng) * (settings.randomWalkSpeed * 0.1f);
            break;
    }

    // Audio trigger parameters, fixed for this grain's whole life.
    const int jitterSamples = (int) (settings.positionJitterInBuffer * sampleRate * rng.nextFloat());
    g.bufferReadStartSample = writeHead - jitterSamples;

    float rate = 1.0f;
    if (settings.pitchJitter > 0.0f)
        rate = 1.0f + settings.pitchJitter * (rng.nextFloat() * 2.0f - 1.0f);
    g.playbackRate = juce::jmax (0.1f, rate);

    g.grainLengthSamples = juce::jmax (1, (int) (g.lifetimeSeconds * sampleRate));
}

void GrainCloud::updateGrain (Grain& g, float fdt, Vec3 parentPosition, Vec3 /*parentVelocity*/)
{
    switch (settings.movementMode)
    {
        case GrainMovementMode::RandomWalk:
            // Direction/speed are only randomized once at spawn (see
            // spawnGrain) -- no rng is available here, and re-randomizing
            // every control tick would look like jitter rather than a
            // walk. Each grain gets one smooth drift for its short life.
            g.position += g.velocity * fdt;
            break;

        case GrainMovementMode::Bounce:
        {
            g.position += g.velocity * fdt;

            const Vec3 offset = g.position - g.boundaryCenter;
            const float dist = offset.length();
            if (dist > g.currentBoundaryRadius && dist > 1.0e-6f)
            {
                const Vec3 normal = offset / dist;
                g.position = g.boundaryCenter + normal * g.currentBoundaryRadius;

                const float vDotN = g.velocity.dot (normal);
                if (vDotN > 0.0f) // only reflect if actually moving outward
                    g.velocity -= normal * (vDotN * (1.0f + settings.restitution));
            }
            break;
        }

        case GrainMovementMode::RadialExplosion:
            g.velocity += g.explosionDirection * (g.currentAcceleration * fdt);
            g.position += g.velocity * fdt;
            break;

        case GrainMovementMode::OrbitAroundParent:
        {
            g.orbitPhase += settings.orbitAngularSpeed * fdt;
            const Vec3 offset { std::cos (g.orbitPhase) * g.currentOrbitRadius,
                                 std::sin (g.orbitPhase) * g.currentOrbitRadius,
                                 0.0f };
            g.position = parentPosition + offset; // tracks the CURRENT (possibly moving) parent position
            break;
        }

        case GrainMovementMode::AttractRepelSiblings:
        {
            const Vec3 force = computeSiblingForce (g);
            g.velocity += force * fdt; // unit mass: force doubles as acceleration

            // Safety clamp: sibling forces can still spike hard on a close
            // pass (small softening, no per-grain mass/exponent tuning
            // like TrajectoryEngine's n-body has) -- cap speed so one
            // close encounter can't fling a grain out to an absurd
            // distance within its short lifetime, regardless of sign.
            constexpr float maxSiblingSpeed = 8.0f;
            const float speed = g.velocity.length();
            if (speed > maxSiblingSpeed)
                g.velocity = g.velocity * (maxSiblingSpeed / speed);

            g.position += g.velocity * fdt;
            break;
        }
    }
}

void GrainCloud::update (double dtSeconds, Vec3 parentPosition, Vec3 parentVelocity,
                          int& globalGrainBudget, juce::Random& rng)
{
    const float fdt = (float) dtSeconds;

    for (auto& g : grains)
    {
        if (! g.active)
            continue;

        g.age += fdt;
        if (g.age >= g.lifetimeSeconds)
        {
            g.active = false;
            continue;
        }

        updateGrain (g, fdt, parentPosition, parentVelocity);
    }

    if (settings.enabled && settings.grainRate > 0.0f)
    {
        timeSinceLastSpawn += dtSeconds;
        const double spawnInterval = 1.0 / (double) settings.grainRate;

        while (timeSinceLastSpawn >= spawnInterval)
        {
            if (globalGrainBudget <= 0 || getNumActiveGrains() >= settings.maxConcurrentGrains)
            {
                timeSinceLastSpawn = spawnInterval; // cap backlog, avoid a burst once capacity frees up
                break;
            }

            int slot = -1;
            for (int i = 0; i < (int) grains.size(); ++i)
            {
                if (! grains[(size_t) i].active)
                {
                    slot = i;
                    break;
                }
            }
            if (slot < 0)
            {
                timeSinceLastSpawn = spawnInterval; // pool exhausted, same backlog cap as above
                break;
            }

            spawnGrain (slot, parentPosition, ringBufferWriteHeadSample, ringBufferSampleRate, rng);
            --globalGrainBudget;
            timeSinceLastSpawn -= spawnInterval;
        }
    }
    else
    {
        timeSinceLastSpawn = 0.0;
    }

    juce::ScopedLock lock (snapshotLock);
    for (size_t i = 0; i < grains.size(); ++i)
    {
        auto& g = grains[i];
        auto& s = snapshotBuffer[i];
        s.active = g.active;
        s.position = g.position;
        s.velocity = g.velocity;
        s.ageFraction = g.lifetimeSeconds > 0.0f ? juce::jlimit (0.0f, 1.0f, g.age / g.lifetimeSeconds) : 1.0f;
        s.spawnGeneration = g.spawnGeneration;
        s.bufferReadStartSample = g.bufferReadStartSample;
        s.playbackRate = g.playbackRate;
        s.grainLengthSamples = g.grainLengthSamples;
    }
}
