#include <cmath>
#include <cstdio>
#include <vector>
#include <juce_core/juce_core.h>
#include "../Source/Modifier.h"

namespace
{
    constexpr double kSampleRate = 48000.0;

    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    Modifier makeGain (float base, float spread = 0.0f)
    {
        Modifier m;
        m.type = ModifierType::Gain;
        m.gain = { base, spread };
        return m;
    }

    Modifier makeTimeOffset (float base, float spread = 0.0f)
    {
        Modifier m;
        m.type = ModifierType::TimeOffset;
        m.timeOffset = { base, spread };
        return m;
    }

    Modifier makePitch (float base, float spread = 0.0f)
    {
        Modifier m;
        m.type = ModifierType::Pitch;
        m.pitchSemitones = { base, spread };
        return m;
    }

    Modifier makeFilter (float cutoff, float resonance = 0.707f)
    {
        Modifier m;
        m.type = ModifierType::Filter;
        m.filter.cutoffHz = { cutoff, 0.0f };
        m.filter.resonance = { resonance, 0.0f };
        return m;
    }

    Modifier makeMovement (MovementModifierShape shape, float speed, float acceleration = 0.0f)
    {
        Modifier m;
        m.type = ModifierType::Movement;
        m.movementShape = shape;
        m.movementSpeed = { speed, 0.0f };
        m.movementAcceleration = { acceleration, 0.0f };
        return m;
    }

    Modifier makeSpatialOffset (SpatialDistributionShape shape, float base, float spread = 0.0f)
    {
        Modifier m;
        m.type = ModifierType::SpatialOffset;
        m.spatialShape = shape;
        m.spatialRadius = { base, spread };
        return m;
    }
}

int main()
{
    // --- Empty/disabled stacks are a no-op -----------------------------
    {
        SeededRandom rng (1);
        auto out = ModifierStack::sample ({}, rng);
        check (out.gainMultiplier == 1.0f && out.timeOffsetSeconds == 0.0f && out.pitchSemitones == 0.0f
                   && out.filters.numSlots == 0,
               "sample(): an empty stack produces the fully-neutral default Outcome");
    }
    {
        std::vector<Modifier> stack { makeGain (2.0f), makeTimeOffset (0.5f), makePitch (7.0f), makeFilter (1000.0f) };
        for (auto& m : stack) m.enabled = false;
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (out.gainMultiplier == 1.0f && out.timeOffsetSeconds == 0.0f && out.pitchSemitones == 0.0f
                   && out.filters.numSlots == 0,
               "sample(): every slot disabled behaves exactly like an empty stack");
    }

    // --- Gain/TimeOffset/Pitch aggregate correctly ----------------------
    {
        std::vector<Modifier> stack { makeGain (2.0f), makeGain (0.5f), makeGain (3.0f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (std::abs (out.gainMultiplier - 3.0f) < 1.0e-4f, "sample(): multiple Gain slots multiply together (2.0 * 0.5 * 3.0 == 3.0)");
    }
    {
        std::vector<Modifier> stack { makeTimeOffset (0.1f), makeTimeOffset (-0.03f), makeTimeOffset (0.2f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (std::abs (out.timeOffsetSeconds - 0.27f) < 1.0e-4f, "sample(): multiple TimeOffset slots sum (0.1 - 0.03 + 0.2 == 0.27)");
    }
    {
        std::vector<Modifier> stack { makePitch (12.0f), makePitch (-5.0f), makePitch (2.0f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (std::abs (out.pitchSemitones - 9.0f) < 1.0e-4f, "sample(): multiple Pitch slots sum (12 - 5 + 2 == 9)");
    }
    {
        // A disabled slot in the middle of several enabled ones of the
        // same type must simply be skipped, not break the aggregation.
        std::vector<Modifier> stack { makeGain (2.0f), makeGain (100.0f), makeGain (3.0f) };
        stack[1].enabled = false;
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (std::abs (out.gainMultiplier - 6.0f) < 1.0e-4f, "sample(): a disabled slot in the middle of the stack is skipped, not zeroed-into the aggregate");
    }

    // --- Filter slots: order, sampling, overflow ------------------------
    {
        std::vector<Modifier> stack { makeFilter (500.0f), makeFilter (2000.0f), makeFilter (8000.0f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (out.filters.numSlots == 3, "sample(): 3 Filter slots produce exactly 3 entries in FilterBankState");
        check (std::abs (out.filters.cutoffHz[0] - 500.0f) < 1.0e-3f
                   && std::abs (out.filters.cutoffHz[1] - 2000.0f) < 1.0e-3f
                   && std::abs (out.filters.cutoffHz[2] - 8000.0f) < 1.0e-3f,
               "sample(): Filter slots are captured in STACK ORDER (spread=0 reproduces each one's own base cutoff exactly)");
    }
    {
        // Gain/TimeOffset/Pitch slots interleaved between Filter slots
        // must not shift the Filter slots' own indexing.
        std::vector<Modifier> stack { makeFilter (1000.0f), makeGain (2.0f), makeFilter (3000.0f), makePitch (5.0f), makeFilter (6000.0f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (out.filters.numSlots == 3
                   && std::abs (out.filters.cutoffHz[0] - 1000.0f) < 1.0f
                   && std::abs (out.filters.cutoffHz[1] - 3000.0f) < 1.0f
                   && std::abs (out.filters.cutoffHz[2] - 6000.0f) < 1.0f,
               "sample(): non-Filter slots interleaved in the stack don't disturb Filter slots' own 0..2 indexing");
    }
    {
        std::vector<Modifier> stack;
        for (int i = 0; i < ModifierLimits::maxModifiersPerStack + 3; ++i)
            stack.push_back (makeFilter (1000.0f + 100.0f * (float) i));
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (out.filters.numSlots == ModifierLimits::maxModifiersPerStack,
               "sample(): more Filter slots than ModifierLimits::maxModifiersPerStack silently caps at the limit, never overflows the fixed-size array");
    }

    // --- Reproducibility -------------------------------------------------
    {
        std::vector<Modifier> stack { makeGain (1.0f, 0.5f), makeTimeOffset (0.0f, 0.1f), makePitch (0.0f, 12.0f), makeFilter (1000.0f) };
        stack[3].filter.cutoffHz.spread = 500.0f;

        SeededRandom rngA (42);
        auto outA = ModifierStack::sample (stack, rngA);
        SeededRandom rngB (42);
        auto outB = ModifierStack::sample (stack, rngB);

        check (outA.gainMultiplier == outB.gainMultiplier
                   && outA.timeOffsetSeconds == outB.timeOffsetSeconds
                   && outA.pitchSemitones == outB.pitchSemitones
                   && outA.filters.cutoffHz[0] == outB.filters.cutoffHz[0],
               "sample(): the same seed produces an identical Outcome for a stack with randomized spreads");
    }

    // --- Drift ------------------------------------------------------------
    {
        std::vector<Modifier> stack { makeFilter (1000.0f) }; // driftAmplitude defaults to 0 (disabled)
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        const float before = out.filters.cutoffHz[0];
        for (int i = 0; i < 50; ++i)
            ModifierStack::advanceFilterDrift (stack, 0.01f, rng, out.filters, (float) kSampleRate);
        check (out.filters.cutoffHz[0] == before, "advanceFilterDrift(): a slot with driftAmplitude == 0 (the default) never moves");
    }
    {
        std::vector<Modifier> stack { makeFilter (1000.0f) };
        stack[0].filter.driftAmplitude = 400.0f;
        stack[0].filter.driftReversionRate = 0.5f;
        stack[0].filter.driftSmoothing = 0.1f;

        SeededRandom rng (9);
        auto out = ModifierStack::sample (stack, rng);
        const float before = out.filters.cutoffHz[0];

        bool everMoved = false;
        bool everOutOfRange = false;
        for (int i = 0; i < 2000; ++i)
        {
            ModifierStack::advanceFilterDrift (stack, 0.01f, rng, out.filters, (float) kSampleRate);
            const float now = out.filters.cutoffHz[0];
            if (now != before) everMoved = true;
            if (now < VoiceFilterLimits::minCutoffHz || now > (float) kSampleRate * VoiceFilterLimits::maxCutoffRatioOfSampleRate)
                everOutOfRange = true;
        }
        check (everMoved, "advanceFilterDrift(): a slot with driftAmplitude > 0 actually moves its cutoff over time");
        check (! everOutOfRange, "advanceFilterDrift(): a drifting slot's cutoff never leaves VoiceFilterLimits' safe range");
    }
    {
        // Multiple Filter slots interleaved with non-Filter slots: drift
        // must still land on the correct slot index, same guard as the
        // sample()-side indexing test above.
        std::vector<Modifier> stack { makeFilter (1000.0f), makeGain (1.0f), makeFilter (5000.0f) };
        stack[0].filter.driftAmplitude = 0.0f;   // slot 0: static
        stack[2].filter.driftAmplitude = 400.0f; // slot 1 (of filters): drifting

        SeededRandom rng (3);
        auto out = ModifierStack::sample (stack, rng);
        const float before0 = out.filters.cutoffHz[0];
        const float before1 = out.filters.cutoffHz[1];

        for (int i = 0; i < 500; ++i)
            ModifierStack::advanceFilterDrift (stack, 0.01f, rng, out.filters, (float) kSampleRate);

        check (out.filters.cutoffHz[0] == before0, "advanceFilterDrift(): slot 0 (driftAmplitude == 0) stays static even while slot 1 drifts");
        check (out.filters.cutoffHz[1] != before1, "advanceFilterDrift(): slot 1 (driftAmplitude > 0) drifts independently of slot 0, matched by correct index despite the interleaved Gain slot");
    }

    // --- Movement modifiers ----------------------------------------------
    {
        std::vector<Modifier> stack { makeMovement (MovementModifierShape::Linear, 2.0f) };
        SeededRandom rng (1);
        auto out = ModifierStack::sample (stack, rng);
        check (out.movement.numSlots == 1, "sample(): one Movement slot produces exactly one MovementBankState entry");
        check (std::abs (out.movement.direction[0].length() - 1.0f) < 1.0e-3f, "sample(): a Linear slot's direction is a unit vector");
        check (std::abs (out.movement.currentSpeed[0] - 2.0f) < 1.0e-4f, "sample(): a Linear slot's initial currentSpeed equals its sampled movementSpeed");
    }
    {
        std::vector<Modifier> stack { makeMovement (MovementModifierShape::Rotating, 1.0f) };
        SeededRandom rng (2);
        auto out = ModifierStack::sample (stack, rng);
        check (std::abs (out.movement.direction[0].length() - 1.0f) < 1.0e-3f, "sample(): a Rotating slot's direction (rotation axis) is also a unit vector");
    }
    {
        // Interleaved with non-Movement slots -- must not disturb Movement's own 0..1 indexing.
        std::vector<Modifier> stack {
            makeGain (2.0f),
            makeMovement (MovementModifierShape::Linear, 1.0f),
            makeFilter (1000.0f),
            makeMovement (MovementModifierShape::Rotating, 0.5f)
        };
        SeededRandom rng (3);
        auto out = ModifierStack::sample (stack, rng);
        check (out.movement.numSlots == 2, "sample(): 2 Movement slots produce exactly 2 MovementBankState entries (interleaved Gain/Filter slots don't count)");
        check (out.movement.shape[0] == MovementModifierShape::Linear && out.movement.shape[1] == MovementModifierShape::Rotating,
               "sample(): Movement slots are captured in STACK ORDER, indexed independently of the interleaved Gain/Filter slots");
    }
    {
        std::vector<Modifier> stack;
        for (int i = 0; i < ModifierLimits::maxModifiersPerStack + 3; ++i)
            stack.push_back (makeMovement (MovementModifierShape::Linear, 1.0f));
        SeededRandom rng (4);
        auto out = ModifierStack::sample (stack, rng);
        check (out.movement.numSlots == ModifierLimits::maxModifiersPerStack,
               "sample(): more Movement slots than ModifierLimits::maxModifiersPerStack silently caps at the limit");
    }

    // advanceMovement()+applyMovement(): Linear, constant speed -- straight-line motion at exactly `speed` m/s along `direction`.
    {
        std::vector<Modifier> stack { makeMovement (MovementModifierShape::Linear, 2.0f) };
        SeededRandom rng (5);
        auto out = ModifierStack::sample (stack, rng);
        constexpr Vec3 primaryPosition { 0.0f, 0.0f, 0.0f }; // fixed -- isolates the modifier's own contribution
        constexpr float dt = 0.01f;
        Vec3 rendered = primaryPosition;
        for (int i = 0; i < 100; ++i) // 1s of ticks
        {
            ModifierStack::advanceMovement (stack, dt, out.movement);
            rendered = ModifierStack::applyMovement (stack, out.movement, primaryPosition, { 0.0f, 0.0f, 0.0f });
        }

        const float expectedDistance = 2.0f * 1.0f; // speed * 1s elapsed
        std::printf ("       Linear movement (speed=2, no accel) after 1s: distance=%.4f (expected %.4f)\n", rendered.length(), expectedDistance);
        check (std::abs (rendered.length() - expectedDistance) < 0.01f, "applyMovement(): Linear slot's rendered offset is exactly speed*time along its own fixed direction, with no acceleration");
    }

    // Linear with acceleration -- speed should have increased, so the SECOND half-second covers more distance than the first.
    {
        std::vector<Modifier> stack { makeMovement (MovementModifierShape::Linear, 0.0f, 4.0f) }; // starts at rest, accelerates
        SeededRandom rng (6);
        auto out = ModifierStack::sample (stack, rng);
        constexpr Vec3 primaryPosition { 0.0f, 0.0f, 0.0f };
        constexpr float dt = 0.01f;
        Vec3 rendered = primaryPosition;
        for (int i = 0; i < 50; ++i) // first 0.5s
        {
            ModifierStack::advanceMovement (stack, dt, out.movement);
            rendered = ModifierStack::applyMovement (stack, out.movement, primaryPosition, { 0.0f, 0.0f, 0.0f });
        }
        const float distAfterFirstHalf = rendered.length();
        for (int i = 0; i < 50; ++i) // second 0.5s
        {
            ModifierStack::advanceMovement (stack, dt, out.movement);
            rendered = ModifierStack::applyMovement (stack, out.movement, primaryPosition, { 0.0f, 0.0f, 0.0f });
        }
        const float distAfterSecondHalf = rendered.length() - distAfterFirstHalf;

        std::printf ("       Linear movement with acceleration: first 0.5s covered %.4f, second 0.5s covered %.4f\n", distAfterFirstHalf, distAfterSecondHalf);
        check (distAfterSecondHalf > distAfterFirstHalf, "applyMovement(): Linear slot's acceleration increases its speed over time (second half-second covers more distance)");
    }

    // Rotating -- preserves distance from parent (pure rotation) and the component along its own axis.
    {
        std::vector<Modifier> stack { makeMovement (MovementModifierShape::Rotating, juce::MathConstants<float>::twoPi) }; // one full turn per second
        SeededRandom rng (7);
        auto out = ModifierStack::sample (stack, rng);
        const Vec3 axis = out.movement.direction[0];
        const Vec3 parent { 5.0f, -2.0f, 1.0f };

        // Start somewhere not exactly on the axis, a fixed radius from parent.
        Vec3 start = parent + Vec3 { 1.0f, 0.0f, 0.0f };
        if (std::abs (axis.dot ({ 1.0f, 0.0f, 0.0f })) > 0.999f) // unlucky: axis nearly parallel to our probe offset
            start = parent + Vec3 { 0.0f, 1.0f, 0.0f };

        const float startRadius = (start - parent).length();
        const float startAxialComponent = axis.dot (start - parent);

        constexpr float dt = 0.01f;
        bool radiusPreserved = true, axialComponentPreserved = true;
        Vec3 rendered = start;
        for (int i = 0; i < 50; ++i) // half a second -- half of one full turn
        {
            ModifierStack::advanceMovement (stack, dt, out.movement);
            rendered = ModifierStack::applyMovement (stack, out.movement, start, parent);
            if (std::abs ((rendered - parent).length() - startRadius) > 0.01f) radiusPreserved = false;
            if (std::abs (axis.dot (rendered - parent) - startAxialComponent) > 0.01f) axialComponentPreserved = false;
        }
        check (radiusPreserved, "applyMovement(): Rotating slot preserves distance from the parent at every tick (pure rotation)");
        check (axialComponentPreserved, "applyMovement(): Rotating slot preserves the component of position along its own rotation axis");

        // After exactly half a turn (0.5s at one turn/second), position should be diametrically opposite across the axis.
        const float distanceFromStart = (rendered - start).length();
        std::printf ("       Rotating movement: after half a turn, distance moved from start = %.4f (expected ~%.4f, i.e. 2x radius)\n", distanceFromStart, 2.0f * startRadius);
        check (std::abs (distanceFromStart - 2.0f * startRadius) < 0.05f, "applyMovement(): Rotating slot lands diametrically opposite after exactly half a turn");
    }

    // The whole reason advanceMovement()/applyMovement() are split in two:
    // a PRIMARY position that's recomputed from scratch every tick (like
    // OrbitAroundParent/DelayInstance's own ring, simulated here by a
    // closed-form sine wave, not accumulated the way RandomWalk's is)
    // must still see the modifier's contribution keep GROWING over the
    // voice's whole life, not reset to one tick's worth every time.
    {
        std::vector<Modifier> stack { makeMovement (MovementModifierShape::Linear, 1.0f) }; // 1 m/s, constant
        SeededRandom rng (8);
        auto out = ModifierStack::sample (stack, rng);
        constexpr float dt = 0.01f;

        Vec3 rendered {};
        for (int i = 0; i < 300; ++i) // 3s of ticks
        {
            // A primary position that is NOT accumulated -- recomputed
            // fresh from the tick index every single call, same shape
            // as OrbitAroundParent's own "position = f(phase)".
            const Vec3 freshPrimaryPosition { std::sin ((float) i * 0.1f), 0.0f, 0.0f };
            ModifierStack::advanceMovement (stack, dt, out.movement);
            rendered = ModifierStack::applyMovement (stack, out.movement, freshPrimaryPosition, {});
        }

        const float expectedOffsetDistance = 1.0f * 3.0f; // speed * 3s elapsed
        const float actualOffsetDistance = out.movement.accumulatedOffset[0].length();
        std::printf ("       Movement offset after 3s on top of a from-scratch-recomputed primary position: %.4f (expected %.4f)\n",
                     actualOffsetDistance, expectedOffsetDistance);
        check (std::abs (actualOffsetDistance - expectedOffsetDistance) < 0.02f,
               "applyMovement(): the modifier's own contribution keeps accumulating over the voice's whole life even when the primary position is recomputed from scratch every tick, not just one tick's worth");
    }

    // --- SpatialOffset (spatial start-position distribution) -----------------
    {
        // Default (base=0, spread=0) must sample to exactly zero, for every shape.
        SeededRandom rng (10);
        bool allZero = true;
        for (auto shape : { SpatialDistributionShape::Sphere, SpatialDistributionShape::SphericalShell,
                             SpatialDistributionShape::Ring, SpatialDistributionShape::Line, SpatialDistributionShape::Cuboid })
        {
            Modifier m = makeSpatialOffset (shape, 0.0f, 0.0f);
            for (int i = 0; i < 20; ++i)
                if (ModifierStack::sampleSpatialOffset (m, rng).length() > 1.0e-5f)
                    allZero = false;
        }
        check (allZero, "sampleSpatialOffset(): base=0/spread=0 (the default) samples to exactly zero for every shape -- a freshly-added slot is a no-op");
    }
    {
        Modifier m = makeSpatialOffset (SpatialDistributionShape::Sphere, 5.0f, 0.0f);
        SeededRandom rng (11);
        bool allWithinRadius = true, anyVariation = false;
        float firstLength = -1.0f;
        for (int i = 0; i < 100; ++i)
        {
            const float len = ModifierStack::sampleSpatialOffset (m, rng).length();
            if (len > 5.001f) allWithinRadius = false;
            if (firstLength < 0.0f) firstLength = len;
            else if (std::abs (len - firstLength) > 0.01f) anyVariation = true;
        }
        check (allWithinRadius, "sampleSpatialOffset(): Sphere never samples a point further than its own maxRadius from the center");
        check (anyVariation, "sampleSpatialOffset(): Sphere fills the VOLUME (sampled radii vary, not all sitting on the surface)");
    }
    {
        Modifier m = makeSpatialOffset (SpatialDistributionShape::SphericalShell, 5.0f, 0.0f);
        SeededRandom rng (12);
        bool allOnShell = true;
        for (int i = 0; i < 50; ++i)
            if (std::abs (ModifierStack::sampleSpatialOffset (m, rng).length() - 5.0f) > 0.01f) allOnShell = false;
        check (allOnShell, "sampleSpatialOffset(): SphericalShell (spread=0) samples exactly on the shell's own radius every time, unlike Sphere's volume fill");
    }
    {
        Modifier m = makeSpatialOffset (SpatialDistributionShape::Ring, 3.0f, 0.0f);
        SeededRandom rng (13);
        bool allOnRingRadiusAndFlat = true;
        for (int i = 0; i < 50; ++i)
        {
            const Vec3 p = ModifierStack::sampleSpatialOffset (m, rng);
            if (std::abs (p.length() - 3.0f) > 0.01f) allOnRingRadiusAndFlat = false;
            if (std::abs (p.z) > 1.0e-4f) allOnRingRadiusAndFlat = false;
        }
        check (allOnRingRadiusAndFlat, "sampleSpatialOffset(): Ring samples lie exactly at its own radius, flat in the X/Y plane (z == 0)");
    }
    {
        Modifier m = makeSpatialOffset (SpatialDistributionShape::Line, 0.0f, 2.0f);
        SeededRandom rng (14);
        bool allOnAxis = true, anyNegative = false, anyPositive = false;
        for (int i = 0; i < 50; ++i)
        {
            const Vec3 p = ModifierStack::sampleSpatialOffset (m, rng);
            if (std::abs (p.x) > 1.0e-4f || std::abs (p.y) > 1.0e-4f) allOnAxis = false;
            if (p.z < 0.0f) anyNegative = true;
            if (p.z > 0.0f) anyPositive = true;
        }
        check (allOnAxis, "sampleSpatialOffset(): Line samples only ever move along the Z axis (x == y == 0)");
        check (anyNegative && anyPositive, "sampleSpatialOffset(): Line with base=0 samples both sides of center (a signed RandomizedParam offset, no separate sign draw needed)");
    }
    {
        Modifier m = makeSpatialOffset (SpatialDistributionShape::Cuboid, 0.0f, 1.0f);
        SeededRandom rng (15);
        bool allWithinHalfExtent = true;
        std::vector<float> xs, ys, zs;
        for (int i = 0; i < 50; ++i)
        {
            const Vec3 p = ModifierStack::sampleSpatialOffset (m, rng);
            if (std::abs (p.x) > 1.001f || std::abs (p.y) > 1.001f || std::abs (p.z) > 1.001f) allWithinHalfExtent = false;
            xs.push_back (p.x); ys.push_back (p.y); zs.push_back (p.z);
        }
        const bool xVariesIndependentlyOfY = xs[0] != ys[0] || xs[1] != ys[1]; // extremely unlikely to collide if truly independent draws
        check (allWithinHalfExtent, "sampleSpatialOffset(): Cuboid stays within its own half-extent on every axis");
        check (xVariesIndependentlyOfY, "sampleSpatialOffset(): Cuboid's three axes are independent draws, not the same value repeated");
    }
    {
        // sample(): multiple SpatialOffset slots sum (commutative Vec3 addition).
        std::vector<Modifier> stack {
            makeSpatialOffset (SpatialDistributionShape::Line, 1.0f, 0.0f),
            makeSpatialOffset (SpatialDistributionShape::Line, 2.0f, 0.0f)
        };
        SeededRandom rng (16);
        auto out = ModifierStack::sample (stack, rng);
        check (std::abs (out.spatialOffset.z - 3.0f) < 1.0e-3f, "sample(): multiple SpatialOffset slots sum (1.0 + 2.0 == 3.0 along Z)");
    }

    std::printf (g_failures == 0 ? "ALL TESTS PASSED (%d failures)\n" : "SOME TESTS FAILED (%d failures)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
