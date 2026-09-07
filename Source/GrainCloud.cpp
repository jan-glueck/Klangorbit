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

    // --- PitchJitterMode::Scale (see Grain.h's own comment on both enums) ---

    // Semitone offsets within one octave (0 = the grain's own natural
    // pitch) for each PitchQuantizeScale -- standard music-theory interval
    // sets. Returned by reference to a function-local static, not
    // recomputed per call.
    const std::vector<int>& scaleSemitones (PitchQuantizeScale scale)
    {
        static const std::vector<int> octaves    { 0 };
        static const std::vector<int> fifths      { 0, 7 };
        static const std::vector<int> majorTriad  { 0, 4, 7 };
        static const std::vector<int> minorTriad  { 0, 3, 7 };
        static const std::vector<int> majorScale  { 0, 2, 4, 5, 7, 9, 11 };
        static const std::vector<int> dorian      { 0, 2, 3, 5, 7, 9, 10 };
        static const std::vector<int> lydian      { 0, 2, 4, 6, 7, 9, 11 };
        static const std::vector<int> mixolydian  { 0, 2, 4, 5, 7, 9, 10 };
        static const std::vector<int> aeolian     { 0, 2, 3, 5, 7, 8, 10 };
        static const std::vector<int> wholeTone   { 0, 2, 4, 6, 8, 10 };
        static const std::vector<int> octatonic   { 0, 2, 3, 5, 6, 8, 9, 11 };
        static const std::vector<int> hexatonic   { 0, 1, 4, 5, 8, 9 };
        static const std::vector<int> acoustic    { 0, 2, 4, 6, 7, 9, 10 }; // "overtone scale"/Lydian Dominant

        switch (scale)
        {
            case PitchQuantizeScale::Octaves:    return octaves;
            case PitchQuantizeScale::Fifths:     return fifths;
            case PitchQuantizeScale::MajorTriad: return majorTriad;
            case PitchQuantizeScale::MinorTriad: return minorTriad;
            case PitchQuantizeScale::MajorScale: return majorScale;
            case PitchQuantizeScale::Dorian:     return dorian;
            case PitchQuantizeScale::Lydian:     return lydian;
            case PitchQuantizeScale::Mixolydian: return mixolydian;
            case PitchQuantizeScale::Aeolian:    return aeolian;
            case PitchQuantizeScale::WholeTone:  return wholeTone;
            case PitchQuantizeScale::Octatonic:  return octatonic;
            case PitchQuantizeScale::Hexatonic:  return hexatonic;
            case PitchQuantizeScale::Acoustic:   return acoustic;
        }
        return majorScale; // unreachable (every enumerator handled above) -- silences -Wreturn-type
    }

    // pitchJitter (0..1) -> +/- semitone reach, capped at one octave (12
    // semitones = playbackRate 2.0) so a Scale-mode grain's rate stays
    // within exactly the range Random mode's own algebra already
    // self-limits to -- see Grain.h's GrainLimits::maxPitchJitterPlaybackRate
    // and the ring buffer sizing (requiredRingBufferSeconds) that depends on
    // that ceiling never being exceeded by EITHER mode.
    constexpr float maxScaleReachSemitones = 12.0f;

    // Picks one semitone offset at random from every degree of `scale`
    // (across as many octaves up/down as needed) that falls within +/-
    // (pitchJitter * maxScaleReachSemitones) of the grain's own natural
    // pitch (offset 0, always a candidate). pitchJitter <= 0 always yields
    // exactly 0 (no deviation) -- same "no jitter" floor Random mode has
    // via its own `if (settings.pitchJitter > 0.0f)` guard in spawnGrain().
    float pickQuantizedSemitoneOffset (PitchQuantizeScale scale, float pitchJitter, juce::Random& rng)
    {
        const float reach = juce::jmax (0.0f, pitchJitter) * maxScaleReachSemitones;
        const auto& steps = scaleSemitones (scale);

        std::vector<int> candidates;
        const int maxOctave = (int) std::ceil (reach / 12.0f) + 1; // +1 margin: a degree near an octave's top can still land within reach from the next octave up/down
        for (int octave = -maxOctave; octave <= maxOctave; ++octave)
            for (int step : steps)
            {
                const int offset = step + octave * 12;
                if ((float) std::abs (offset) <= reach)
                    candidates.push_back (offset);
            }

        if (candidates.empty())
            return 0.0f; // reach ~0 -- only the root (offset 0) qualifies, or floating-point rounding excluded even that

        return (float) candidates[(size_t) rng.nextInt ((int) candidates.size())];
    }

    // Point at `radius` and angle `phase` on the circle lying in the plane
    // perpendicular to `normal`, centered at the origin (caller adds the
    // parent's position) -- used by OrbitAroundParent for both flat
    // (normal={0,0,1}) and tilted/spherical orbits (see
    // GrainCloudSettings::orbitSphereSpread).
    //
    // Builds an orthonormal (u, v) basis for the plane via a seed vector
    // and two cross products; the seed is deliberately {0,1,0} (not the
    // more obvious {0,0,1} or {1,0,0}) specifically so that normal=
    // {0,0,1} reproduces u={1,0,0}, v={0,1,0} exactly -- i.e. this
    // function is a strict generalization of the original hardcoded
    // "cos(phase), sin(phase), 0" x/y-plane formula, not just a
    // same-shape-different-parametrization replacement. Falls back to a
    // different seed only when normal is too close to {0,1,0} itself for
    // the cross product to stay well-conditioned.
    Vec3 orbitPlaneOffset (Vec3 normal, float phase, float radius)
    {
        Vec3 seed = (std::abs (normal.y) < 0.9f) ? Vec3 { 0.0f, 1.0f, 0.0f } : Vec3 { 1.0f, 0.0f, 0.0f };

        Vec3 u = cross (seed, normal);
        const float uLen = u.length();
        u = (uLen > 1.0e-6f) ? (u / uLen) : Vec3 { 1.0f, 0.0f, 0.0f };

        const Vec3 v = cross (normal, u); // already unit length: normal and u are orthonormal

        return (u * std::cos (phase) + v * std::sin (phase)) * radius;
    }

    // Maps a uniform [0,1) draw to a [0,1] fraction biased per distribution.
    // Deliberately simple power-curve shaping (not a general parametric
    // model) -- see GrainReadDepthDistribution's own comment in Grain.h.
    float sampleDepthFraction (GrainReadDepthDistribution distribution, juce::Random& rng)
    {
        const float u = rng.nextFloat();
        switch (distribution)
        {
            case GrainReadDepthDistribution::WeightedTowardRecent: return u * u;                       // skewed toward 0 (shallow)
            case GrainReadDepthDistribution::WeightedTowardOld:    return 1.0f - (1.0f - u) * (1.0f - u); // skewed toward 1 (deep)
            case GrainReadDepthDistribution::Uniform:
            default:                                               return u;
        }
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

namespace
{
    // Short enough to feel instant/inaudible as a timing delay (well under
    // a spawn interval at any reasonable Spawn Rate), long enough for the
    // existing Hann envelope to taper smoothly to its guaranteed-exact
    // zero ending (see the grain-click fix this project already made --
    // GrainRenderer.h's envelope reaches exactly 0 at grainLengthSamples-1
    // regardless of how short that length is).
    constexpr float voiceStealReleaseSeconds = 0.01f;
}

int GrainCloud::findOldestStealableSlot() const
{
    int best = -1;
    float oldestAge = -1.0f;
    for (int i = 0; i < (int) grains.size(); ++i)
    {
        auto& g = grains[(size_t) i];
        if (! g.active || g.beingStolen) continue;
        if (g.age > oldestAge) { oldestAge = g.age; best = i; }
    }
    return best;
}

void GrainCloud::beginVoiceSteal (Grain& g)
{
    g.beingStolen = true;
    const float shortenedLifetime = g.age + voiceStealReleaseSeconds;
    if (shortenedLifetime < g.lifetimeSeconds)
    {
        g.lifetimeSeconds = shortenedLifetime;
        g.grainLengthSamples = juce::jmax (1, (int) (shortenedLifetime * ringBufferSampleRate));
    }
    // else: this grain was already going to end within voiceStealReleaseSeconds
    // anyway (near its natural end) -- no need to shorten it further, just
    // let it finish and free its slot on schedule.
}

void GrainCloud::spawnGrain (int slot, Vec3 parentPosition, int writeHead, double sampleRate, juce::Random& rng)
{
    auto& g = grains[(size_t) slot];

    g.active = true;
    g.beingStolen = false; // this slot is starting fresh, not winding down from a steal anymore
    g.age = 0.0f;
    ++g.spawnGeneration;
    g.position = parentPosition;
    g.velocity = {};

    float lifetime = settings.grainDuration;
    if (settings.grainDurationJitter > 0.0f)
        lifetime = applyJitter (lifetime, settings.grainDurationJitter, rng);
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
            if (settings.boundaryRadiusJitter > 0.0f)
                radius = applyJitter (radius, settings.boundaryRadiusJitter, rng);
            g.currentBoundaryRadius = juce::jmax (0.01f, radius);
            g.velocity = randomUnitVector (rng) * settings.randomWalkSpeed; // reused as initial bounce speed
            break;
        }

        case GrainMovementMode::RadialExplosion:
        {
            g.explosionDirection = randomUnitVector (rng);
            float speed = settings.initialSpeed;
            if (settings.initialSpeedJitter > 0.0f)
                speed = applyJitter (speed, settings.initialSpeedJitter, rng);
            g.velocity = g.explosionDirection * speed;
            g.currentAcceleration = settings.acceleration;
            break;
        }

        case GrainMovementMode::OrbitAroundParent:
        {
            g.orbitPhase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            float radius = settings.orbitRadius;
            if (settings.orbitRadiusJitter > 0.0f)
                radius = applyJitter (radius, settings.orbitRadiusJitter, rng);
            g.currentOrbitRadius = juce::jmax (0.01f, radius);

            g.orbitPlaneNormal = { 0.0f, 0.0f, 1.0f };
            if (settings.orbitSphereSpread > 0.0f)
            {
                const Vec3 randomAxis = randomUnitVector (rng);
                Vec3 blended = g.orbitPlaneNormal * (1.0f - settings.orbitSphereSpread) + randomAxis * settings.orbitSphereSpread;
                const float len = blended.length();
                g.orbitPlaneNormal = (len > 1.0e-6f) ? (blended / len) : Vec3 { 0.0f, 0.0f, 1.0f };
            }

            g.position = parentPosition + orbitPlaneOffset (g.orbitPlaneNormal, g.orbitPhase, g.currentOrbitRadius);
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

    // Independent, additive reach further back into history -- see
    // grainReadDepthRangeMin/Max's comment in Grain.h. min > max (e.g. a
    // stale UI drag mid-adjustment) is treated as "just use max", rather
    // than producing a negative range.
    float depthSeconds = 0.0f;
    if (settings.grainReadDepthRangeMax > 0.0f)
    {
        if (settings.grainReadDepthRangeMax > settings.grainReadDepthRangeMin)
        {
            const float t = sampleDepthFraction (settings.grainReadDepthDistribution, rng);
            depthSeconds = settings.grainReadDepthRangeMin + t * (settings.grainReadDepthRangeMax - settings.grainReadDepthRangeMin);
        }
        else
        {
            depthSeconds = settings.grainReadDepthRangeMax;
        }
    }
    const int depthSamples = (int) (depthSeconds * sampleRate);

    g.bufferReadStartSample = writeHead - jitterSamples - depthSamples;

    float rate = 1.0f;
    if (settings.pitchJitter > 0.0f)
    {
        if (settings.pitchJitterMode == PitchJitterMode::Scale)
        {
            const float semitoneOffset = pickQuantizedSemitoneOffset (settings.pitchQuantizeScale, settings.pitchJitter, rng);
            rate = std::pow (2.0f, semitoneOffset / 12.0f);
        }
        else
        {
            rate = 1.0f + settings.pitchJitter * (rng.nextFloat() * 2.0f - 1.0f);
        }
    }
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
            // g.orbitPlaneNormal was fixed once at spawn (see spawnGrain());
            // {0,0,1} reproduces the original flat x/y-plane orbit exactly.
            const Vec3 offset = orbitPlaneOffset (g.orbitPlaneNormal, g.orbitPhase, g.currentOrbitRadius);
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

        // grainRate alone determines this interval -- grainDuration never
        // enters this calculation, so the two are independent by design
        // (see GrainCloudSettings's own class comment). Recomputed fresh
        // each time below so grainRateJitter (if any) gives each spawn
        // its own randomized interval, not one fixed jittered value
        // reused for the whole cloud.
        auto nextSpawnInterval = [this, &rng]
        {
            double interval = 1.0 / (double) settings.grainRate;
            if (settings.grainRateJitter > 0.0f)
                interval = (double) applyJitter ((float) interval, settings.grainRateJitter, rng);
            return juce::jmax (1.0e-4, interval); // floor against jitter pushing it to ~0
        };

        double spawnInterval = nextSpawnInterval();

        while (timeSinceLastSpawn >= spawnInterval)
        {
            if (globalGrainBudget <= 0)
            {
                // The shared, scene-wide CPU ceiling (see PluginProcessor)
                // -- unlike maxConcurrentGrains below, this can't be
                // worked around by stealing from this cloud's OWN grains,
                // since it's a limit across every object's cloud combined.
                timeSinceLastSpawn = spawnInterval; // cap backlog, avoid a burst once capacity frees up
                break;
            }

            int slot = -1;
            if (getNumActiveGrains() < settings.maxConcurrentGrains)
            {
                for (int i = 0; i < (int) grains.size(); ++i)
                {
                    if (! grains[(size_t) i].active)
                    {
                        slot = i;
                        break;
                    }
                }
            }

            if (slot < 0)
            {
                // At this cloud's own maxConcurrentGrains cap (the common
                // case once grainRate*grainDuration exceeds it), or
                // (defensively) no free pool slot for some other reason --
                // voice-steal the oldest active grain instead of stalling
                // the spawn schedule (see GrainCloudSettings's own
                // comment). The actual new spawn happens on a later call
                // to update(), once the stolen grain's short forced
                // fade-out (beginVoiceSteal()) actually frees its slot --
                // timeSinceLastSpawn keeps the backlog pending for that.
                const int stealFrom = findOldestStealableSlot();
                if (stealFrom >= 0)
                    beginVoiceSteal (grains[(size_t) stealFrom]);

                timeSinceLastSpawn = spawnInterval; // cap backlog, same reasoning as the budget case above
                break;
            }

            spawnGrain (slot, parentPosition, ringBufferWriteHeadSample, ringBufferSampleRate, rng);
            --globalGrainBudget;
            timeSinceLastSpawn -= spawnInterval;
            spawnInterval = nextSpawnInterval();
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
