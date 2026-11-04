#pragma once
#include <array>
#include <vector>
#include "RandomEngine.h"
#include "VoiceFilter.h"
#include "Vec3.h"

/**
    feature/modifier-stack (Branch 4 of the generative sound design
    effort) -- a per-source (GrainCloud, DelayInstance), ORDERED,
    stackable list of simple per-voice modifiers. "Per voice" means
    each individual grain/delay tap decides its own modifier outcome
    independently (sampled once, same "decided once, fixed for this
    voice's life" treatment this project already uses for pitchJitter/
    VoiceFilter cutoff/etc.), not once per cloud/instance -- see
    ModifierStack::sample() below.

    Order matters between multiple Filter-type slots (a filter cascade's
    order genuinely changes the sound, especially with resonance) and
    between/among Rotating Movement-type slots, or a Rotating mixed
    with anything else that moves the voice (rotations don't commute
    with each other, or with a translation, in general -- see
    applyMovement()). Gain/TimeOffset/Pitch, and any number of purely
    LINEAR Movement slots on their own, are commutative accumulations
    instead -- a constant gain multiplier, a time offset in seconds, a
    semitone sum, a sum of translation vectors -- that mathematically
    don't care where in the stack they sit, or relative to a Filter
    slot: scaling a signal before or after a LINEAR filter gives an
    identical result, "shift the playback rate"/"shift the read start
    time" aren't per-sample operations at all (just spawn-time parameter
    adjustments), and adding several fixed translation vectors in any
    order reaches the same final position. This is a disclosed,
    deliberate simplification where it applies -- documented here, not
    hidden -- not a limitation of the UI (every slot can still be freely
    reordered, so the stack reads naturally top-to-bottom), just a fact
    about what reordering actually changes.

    Coexists with, rather than replaces, GrainCloudSettings::filter
    (feature/grain-filter-integration, Branch 2) -- that single
    dedicated filter is untouched (existing presets/behavior stay
    exactly as they were); the stack's own Filter-type slots are purely
    additive on top of it. See GrainCloud.cpp/PluginProcessor.cpp for
    exactly where each is applied in the chain.
*/
namespace ModifierLimits
{
    // Hard cap on how many modifier slots one stack may ever have --
    // bounds per-voice CPU cost (CPU scales with active-voice-count *
    // active-filter-slot-count; voice count itself is already bounded
    // by GrainLimits::maxConcurrentGrainsGlobal/DelayLimits::maxTaps)
    // and sizes the fixed-size per-voice filter-state arrays below
    // (FilterBankState) -- Grain/DelayTap are pool-managed structs with
    // no heap allocation after construction, so this can't be a
    // std::vector sized to "however many Filter slots happen to be in
    // the stack right now".
    constexpr int maxModifiersPerStack = 8;
}

enum class ModifierType
{
    Gain,
    TimeOffset,
    Filter,
    Pitch,
    Movement,
    SpatialOffset
};

// Movement-type slot only. Linear: a constant speed (+ optional
// acceleration) along a direction picked once per voice, uniformly
// random on the sphere (RandomEngine.h::randomUnitVector) -- the same
// "random direction, tunable magnitude" shape RadialExplosion already
// uses for its own explosionDirection, just additive/stackable on top
// of whatever movementMode a GrainCloud/DelayInstance already has,
// instead of a second, parallel movement engine (see this file's own
// comment on "integrate rather than duplicate"). Rotating: a constant
// angular speed (+ optional angular acceleration) that revolves the
// voice's CURRENT position around the parent, around a ROTATION AXIS
// also picked once per voice (uniformly random -- a flat orbit is just
// the {0,0,1}-axis special case, not hard-coded as the only option),
// via Rodrigues' rotation formula (see applyMovement()) -- composes
// with whatever the primary movement mode already computed for this
// tick (applied strictly AFTER it, see applyMovement()'s own comment
// on why that composition happens there and not incrementally), rather
// than replacing it.
enum class MovementModifierShape
{
    Linear,
    Rotating
};

// SpatialOffset-type slot only -- the "spatial distribution generator
// for start positions" the original spec asked for, checked against
// what grain spawning already does: today every movement mode starts a
// grain exactly at the parent's position (or, for OrbitAroundParent, at
// its own orbit offset) -- there is no existing spread mechanism this
// would duplicate. Sampled ONCE per voice (same event as every other
// spawn/config-time-decided modifier) and added, CONSTANT for the
// voice's whole life, to wherever its own movement mode/ring
// arrangement already puts it -- not a one-off "nudge at t=0" that a
// recompute-from-scratch primary mode (OrbitAroundParent; DelayInstance's
// ring) would immediately erase (same risk Movement's own split guards
// against, see applyMovement()'s comment), but a per-voice constant
// translation that persists for as long as the voice exists.
//
// Each shape reduces to ONE sample of spatialRadius (a RandomizedParam,
// so "probability distribution" -- Uniform/Normal/Ramp/LogUniform/
// PowerLaw/Discrete -- comes for free, no second mechanism needed) per
// axis/draw it needs:
//  - Sphere: volume-uniform fill (radial cube-root correction) within a
//    sphere of radius spatialRadius.sample(), direction random on the
//    sphere (randomUnitVector()).
//  - SphericalShell: direction random on the sphere, radius EXACTLY
//    spatialRadius.sample() (no internal volume-fill correction) -- a
//    spread > 0 gives a "fuzzy" shell of varying radius rather than a
//    razor-thin one.
//  - Ring: a uniformly random angle around a flat (X/Y plane) circle of
//    radius spatialRadius.sample() -- same plane convention as
//    OrbitAroundParent/DelayInstance's own ring, just a random angle
//    per voice instead of an even arrangement.
//  - Line: spatialRadius.sample() itself is the signed offset along a
//    fixed (Z) axis -- a RandomizedParam with base 0 and some spread
//    already produces a symmetric range either side of center, so no
//    separate sign/angle draw is needed.
//  - Cuboid: spatialRadius sampled THREE independent times, once per
//    axis -- base 0 + spread S on all three gives a uniform fill of a
//    cube with half-extent S; this is deliberately a cube, not an
//    arbitrary box, to reuse the one RandomizedParam three times rather
//    than needing three independent ones.
enum class SpatialDistributionShape
{
    Sphere,
    SphericalShell,
    Ring,
    Line,
    Cuboid
};

/**
    One stack slot. Only the field(s) relevant to `type` are meaningful
    -- same "struct holds every variant's fields, only the active one
    is read" convention as GrainCloudSettings' own movement-mode
    fields.
*/
struct Modifier
{
    bool enabled = true;
    ModifierType type = ModifierType::Gain;

    // Gain: linear multiplier, sampled once per voice. Multiple enabled
    // Gain slots multiply together (commutative, see this file's own
    // comment above).
    RandomizedParam gain { 1.0f, 0.0f };

    // TimeOffset: seconds, sampled once per voice, ADDS to the voice's
    // own read-start/delay time (commutative with every other slot).
    // Positive reads further into the past (grains) / adds extra delay
    // (taps); the caller clamps to whatever range is physically
    // available.
    RandomizedParam timeOffset { 0.0f, 0.0f };

    // Filter: a full VoiceFilterSettings (Source/VoiceFilter.h) -- its
    // own cutoff/resonance RandomizedParam, sampled once per voice,
    // with its own optional continuous drift. Multiple Filter slots
    // cascade IN STACK ORDER -- the one case where order is audible.
    VoiceFilterSettings filter;

    // Pitch: semitone offset, sampled once per voice, ADDS (sums with
    // every other enabled Pitch slot, and with the source's own
    // existing pitch jitter where applicable) before being converted to
    // a single playback-rate ratio.
    RandomizedParam pitchSemitones { 0.0f, 0.0f };

    // Movement: see MovementModifierShape's own comment above for what
    // Linear/Rotating each mean. movementSpeed/movementAcceleration are
    // reused for BOTH shapes (m/s and m/s^2 for Linear; rad/s and
    // rad/s^2 for Rotating) -- same "struct holds every variant's
    // fields" convention as the rest of this struct, just one level
    // deeper. Each sampled ONCE per voice; acceleration then
    // continuously changes the voice's OWN persistent speed every tick
    // (see ModifierStack::advanceMovement()), same Euler-integration
    // style as RadialExplosion/OrbitAroundParent's own per-tick updates.
    MovementModifierShape movementShape = MovementModifierShape::Linear;
    RandomizedParam movementSpeed { 0.0f, 0.0f };
    RandomizedParam movementAcceleration { 0.0f, 0.0f };

    // SpatialOffset: see SpatialDistributionShape's own comment above
    // for what each shape means and how spatialRadius (sampled once,
    // up to three times for Cuboid) turns into a 3D offset. base 0,
    // spread 0 (the default) samples to exactly zero for every shape,
    // i.e. a freshly-added SpatialOffset slot is a no-op until actually
    // dialed in, same "inert until configured" convention as a fresh
    // Gain slot (base 1, spread 0) or Filter slot.
    SpatialDistributionShape spatialShape = SpatialDistributionShape::Sphere;
    RandomizedParam spatialRadius { 0.0f, 0.0f };

    // Deliberate bit-exact field comparison -- lets a long-lived voice
    // (DelayInstance's taps, which have no per-spawn "redecide" moment
    // the way a short-lived grain does) detect "did the user actually
    // edit this stack since I last sampled it" and resample, instead of
    // caching the UI's edit until some unrelated field happens to
    // change too. See DelayInstance::resyncTapConfigIfChanged().
    bool operator== (const Modifier& other) const
    {
        return enabled == other.enabled && type == other.type
            && gain == other.gain && timeOffset == other.timeOffset
            && filter == other.filter && pitchSemitones == other.pitchSemitones
            && movementShape == other.movementShape
            && movementSpeed == other.movementSpeed && movementAcceleration == other.movementAcceleration
            && spatialShape == other.spatialShape && spatialRadius == other.spatialRadius;
    }
    bool operator!= (const Modifier& other) const { return ! (*this == other); }
};

namespace ModifierStack
{
    // Per-voice state for the stack's Filter-type slots specifically --
    // bundled into one struct so Grain/DelayTap/Snapshot each only need
    // one member instead of four separate parallel arrays. baseCutoffHz/
    // driftSmoothedNoise are control-rate-internal only (drift's
    // baseline/noise state) -- the audio thread only ever reads
    // cutoffHz/resonance, but copying the whole small struct through a
    // Snapshot is simpler than stripping it down to a partial variant.
    struct FilterBankState
    {
        int numSlots = 0;
        std::array<float, ModifierLimits::maxModifiersPerStack> cutoffHz {};
        std::array<float, ModifierLimits::maxModifiersPerStack> baseCutoffHz {};
        std::array<float, ModifierLimits::maxModifiersPerStack> resonance {};
        std::array<float, ModifierLimits::maxModifiersPerStack> driftSmoothedNoise {};
    };

    // Per-voice state for the stack's Movement-type slots -- see
    // MovementModifierShape's own comment. direction is the fixed
    // random unit vector picked once per voice: a TRAVEL direction for
    // Linear, a ROTATION AXIS for Rotating. currentSpeed is grown every
    // tick by acceleration (m/s^2 for Linear, rad/s^2 for Rotating).
    //
    // accumulatedOffset/accumulatedAngle are the actual, ALWAYS-growing
    // integrals of currentSpeed over the voice's whole life so far --
    // deliberately NOT applied as an in-place "position += one tick's
    // delta" (see advanceMovement()'s own comment for why that would
    // silently stop accumulating for any primary movement mode that
    // recomputes its own position from scratch each tick, e.g.
    // OrbitAroundParent/DelayInstance's ring, rather than integrating
    // it persistently the way RandomWalk/Bounce/RadialExplosion do).
    // Tracking the modifier's OWN running totals here, completely
    // independent of whatever `position` the caller happens to pass in,
    // and composing them only at the point of use (applyMovement())
    // makes this correct regardless of which kind of primary mode it's
    // layered on top of.
    struct MovementBankState
    {
        int numSlots = 0;
        std::array<MovementModifierShape, ModifierLimits::maxModifiersPerStack> shape {};
        std::array<Vec3, ModifierLimits::maxModifiersPerStack> direction {};
        std::array<float, ModifierLimits::maxModifiersPerStack> currentSpeed {};
        std::array<float, ModifierLimits::maxModifiersPerStack> acceleration {};
        std::array<Vec3, ModifierLimits::maxModifiersPerStack> accumulatedOffset {};  // Linear only
        std::array<float, ModifierLimits::maxModifiersPerStack> accumulatedAngle {};  // Rotating only, radians
    };

    // Result of processing a stack once (control rate, at voice spawn/
    // config time) for ONE voice. Gain/TimeOffset/Pitch/SpatialOffset
    // are pre-reduced to single aggregate scalars (see this file's own
    // class comment on why that's valid for the first three;
    // SpatialOffset's own Vec3 sum is commutative the same way, see
    // SpatialDistributionShape's own comment); Filter/Movement slots
    // keep their stack-order-preserving lists.
    struct Outcome
    {
        float gainMultiplier = 1.0f;
        float timeOffsetSeconds = 0.0f;
        float pitchSemitones = 0.0f;
        Vec3 spatialOffset;
        FilterBankState filters;
        MovementBankState movement;
    };

    // Draws ONE SpatialOffset-type slot's own 3D offset -- see
    // SpatialDistributionShape's own comment for what each shape means
    // and why reusing spatialRadius (sampled once, or three times for
    // Cuboid) covers "shape + probability distribution" with no second
    // mechanism. A free function (not inlined into sample()'s switch)
    // purely for readability -- the shape dispatch is its own small
    // switch, nested inside sample()'s would read awkwardly.
    inline Vec3 sampleSpatialOffset (const Modifier& m, SeededRandom& rng)
    {
        switch (m.spatialShape)
        {
            case SpatialDistributionShape::Sphere:
            {
                const float maxRadius = m.spatialRadius.sample (rng);
                const float fill = std::cbrt (rng.nextFloat01()); // volume-uniform radial scaling
                return randomUnitVector (rng) * (maxRadius * fill);
            }
            case SpatialDistributionShape::SphericalShell:
                return randomUnitVector (rng) * m.spatialRadius.sample (rng);

            case SpatialDistributionShape::Ring:
            {
                const float r = m.spatialRadius.sample (rng);
                const float angle = rng.nextFloat01() * juce::MathConstants<float>::twoPi;
                return { std::cos (angle) * r, std::sin (angle) * r, 0.0f };
            }
            case SpatialDistributionShape::Line:
                return { 0.0f, 0.0f, m.spatialRadius.sample (rng) };

            case SpatialDistributionShape::Cuboid:
                return { m.spatialRadius.sample (rng), m.spatialRadius.sample (rng), m.spatialRadius.sample (rng) };
        }
        return {};
    }

    // Samples every ENABLED slot once, in stack order, for one voice.
    // Must be called from the control thread (touches SeededRandom) --
    // same threading rule as everything else in RandomEngine.h/
    // VoiceFilter.h. A stack longer than ModifierLimits::
    // maxModifiersPerStack Filter-type slots silently ignores the
    // overflow ones (defensive, not an error case -- the UI itself
    // should also cap how many slots can be added).
    inline Outcome sample (const std::vector<Modifier>& stack, SeededRandom& rng)
    {
        Outcome out;
        for (auto& m : stack)
        {
            if (! m.enabled)
                continue;

            switch (m.type)
            {
                case ModifierType::Gain:
                    out.gainMultiplier *= m.gain.sample (rng);
                    break;

                case ModifierType::TimeOffset:
                    out.timeOffsetSeconds += m.timeOffset.sample (rng);
                    break;

                case ModifierType::Pitch:
                    out.pitchSemitones += m.pitchSemitones.sample (rng);
                    break;

                case ModifierType::Filter:
                    if (out.filters.numSlots < ModifierLimits::maxModifiersPerStack)
                    {
                        const int i = out.filters.numSlots++;
                        out.filters.cutoffHz[(size_t) i] = m.filter.cutoffHz.sample (rng);
                        out.filters.resonance[(size_t) i] = m.filter.resonance.sample (rng);
                        out.filters.baseCutoffHz[(size_t) i] = out.filters.cutoffHz[(size_t) i];
                        out.filters.driftSmoothedNoise[(size_t) i] = 0.0f;
                    }
                    break;

                case ModifierType::Movement:
                    if (out.movement.numSlots < ModifierLimits::maxModifiersPerStack)
                    {
                        const int i = out.movement.numSlots++;
                        out.movement.shape[(size_t) i] = m.movementShape;
                        // Used as the TRAVEL direction for Linear, or the
                        // ROTATION AXIS for Rotating (Rodrigues' formula,
                        // see advanceMovement()) -- one random-unit-vector
                        // sample covers both shapes uniformly, rather than
                        // Rotating being artificially restricted to a
                        // fixed flat (Z-only) axis.
                        out.movement.direction[(size_t) i] = randomUnitVector (rng);
                        out.movement.currentSpeed[(size_t) i] = m.movementSpeed.sample (rng);
                        out.movement.acceleration[(size_t) i] = m.movementAcceleration.sample (rng);
                    }
                    break;

                case ModifierType::SpatialOffset:
                    out.spatialOffset += sampleSpatialOffset (m, rng);
                    break;
            }
        }
        return out;
    }

    // Advances optional per-filter-slot cutoff drift by dt seconds --
    // cheap no-op per slot whenever that slot's own driftAmplitude <= 0
    // (the common case). `filters` is the SAME FilterBankState sample()
    // produced for this voice, persisted and passed back in every tick
    // of the voice's life -- must be walked in the exact same stack
    // order sample() used, which holds as long as the stack itself
    // isn't mutated mid-voice-life (true in practice: a stack edit only
    // takes effect for voices spawned/reconfigured afterward).
    inline void advanceFilterDrift (const std::vector<Modifier>& stack, float dt, SeededRandom& rng,
                                      FilterBankState& filters, float sampleRate)
    {
        int slot = 0;
        for (auto& m : stack)
        {
            if (! m.enabled || m.type != ModifierType::Filter)
                continue;
            if (slot >= filters.numSlots)
                break;

            if (m.filter.driftAmplitude > 0.0f)
            {
                const size_t i = (size_t) slot;
                filters.cutoffHz[i] = MeanReverting::step (
                    filters.cutoffHz[i], filters.baseCutoffHz[i],
                    m.filter.driftReversionRate, m.filter.driftAmplitude,
                    m.filter.driftSmoothing, dt, rng.nextGaussian(), filters.driftSmoothedNoise[i]);
                filters.cutoffHz[i] = juce::jlimit (VoiceFilterLimits::minCutoffHz,
                                                      sampleRate * VoiceFilterLimits::maxCutoffRatioOfSampleRate,
                                                      filters.cutoffHz[i]);
            }
            ++slot;
        }
    }

    // Grows every Movement-type slot's own persistent accumulators by
    // dt -- currentSpeed by its own constant acceleration, then
    // accumulatedOffset/accumulatedAngle by currentSpeed (Euler
    // integration, same style as RadialExplosion/OrbitAroundParent's
    // own per-tick updates). No drift/randomness here, so unlike
    // advanceFilterDrift() this touches no RNG and is safe to call
    // every tick unconditionally. Deliberately does NOT touch `position`
    // at all -- see applyMovement() for why the two are split, and call
    // this FIRST, once per tick, before calling that. `movement` is the
    // SAME MovementBankState sample() produced for this voice,
    // persisted and passed back in every tick of the voice's life --
    // must be walked in the exact same stack order sample() used (see
    // advanceFilterDrift()'s identical caveat).
    inline void advanceMovement (const std::vector<Modifier>& stack, float dt, MovementBankState& movement)
    {
        int slot = 0;
        for (auto& m : stack)
        {
            if (! m.enabled || m.type != ModifierType::Movement)
                continue;
            if (slot >= movement.numSlots)
                break;

            const size_t i = (size_t) slot;
            movement.currentSpeed[i] += movement.acceleration[i] * dt;

            if (movement.shape[i] == MovementModifierShape::Linear)
                movement.accumulatedOffset[i] += movement.direction[i] * (movement.currentSpeed[i] * dt);
            else // Rotating
                movement.accumulatedAngle[i] += movement.currentSpeed[i] * dt;

            ++slot;
        }
    }

    // Composes every Movement-type slot's own TOTAL accumulated
    // contribution so far onto `primaryPosition` (whatever the voice's
    // primary movement mode computed for this tick, read-only here --
    // never mutated) and returns the result to actually render/publish.
    //
    // Deliberately NOT folded into advanceMovement() as an in-place
    // `position += thisTicksDelta`: that would only ever apply ONE
    // tick's worth of change on top of whatever `position` already
    // holds, which is correct ONLY for primary modes that themselves
    // persistently accumulate position (RandomWalk/Bounce/
    // RadialExplosion/AttractRepelSiblings) -- for a primary mode that
    // instead recomputes its own position from scratch every tick
    // (OrbitAroundParent; DelayInstance's ring arrangement is the same
    // shape), a `+=` would silently discard everything accumulated
    // before the current tick, leaving a Movement modifier's visible
    // effect stuck at a single tick's magnitude no matter how long the
    // voice lives. Re-deriving the FULL offset/rotation from this
    // slot's own running totals (accumulatedOffset/accumulatedAngle,
    // grown every tick by advanceMovement() above) and adding or
    // applying it fresh each call is correct either way, since it never
    // depends on what `primaryPosition` itself contained on a previous
    // call.
    //
    // Multiple Movement slots compose IN STACK ORDER (each one's
    // transform is applied to the previous slot's own output, not
    // independently to the raw primary position) -- see this file's
    // own class comment on why that makes Rotating (and anything mixed
    // with it) order-sensitive, unlike Linear-only combinations.
    inline Vec3 applyMovement (const std::vector<Modifier>& stack, const MovementBankState& movement,
                                 Vec3 primaryPosition, Vec3 parentPosition)
    {
        Vec3 pos = primaryPosition;
        int slot = 0;
        for (auto& m : stack)
        {
            if (! m.enabled || m.type != ModifierType::Movement)
                continue;
            if (slot >= movement.numSlots)
                break;

            const size_t i = (size_t) slot;
            if (movement.shape[i] == MovementModifierShape::Linear)
            {
                pos += movement.accumulatedOffset[i];
            }
            else // Rotating -- Rodrigues' rotation formula around movement.direction[i], by the FULL accumulated angle
            {
                const float angle = movement.accumulatedAngle[i];
                const Vec3 axis = movement.direction[i];
                const Vec3 offset = pos - parentPosition;
                const float c = std::cos (angle);
                const float s = std::sin (angle);
                const Vec3 rotated = offset * c + cross (axis, offset) * s + axis * (axis.dot (offset) * (1.0f - c));
                pos = parentPosition + rotated;
            }

            ++slot;
        }
        return pos;
    }

    // Looks up the i-th enabled Filter-type slot's own VoiceFilterType.
    // Cloud/instance-wide and static per slot (doesn't vary per voice,
    // unlike cutoff/resonance), so it's read directly from the stack
    // itself rather than threaded through FilterBankState -- same
    // pattern as the single dedicated filter's own `type` field (read
    // directly from GrainCloudSettings::filter.type in
    // PluginProcessor.cpp). O(stack size); only called once per slot on
    // a voice's (re)spawn, never per sample.
    inline VoiceFilterType filterSlotType (const std::vector<Modifier>& stack, int slotIndex)
    {
        int slot = 0;
        for (auto& m : stack)
        {
            if (! m.enabled || m.type != ModifierType::Filter)
                continue;
            if (slot == slotIndex)
                return m.filter.type;
            ++slot;
        }
        jassertfalse; // slotIndex should always be < the FilterBankState::numSlots sample() produced from this same stack
        return VoiceFilterType::LowPass;
    }
}
