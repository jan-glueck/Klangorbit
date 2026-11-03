#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

/**
    Shared randomization infrastructure -- the common foundation
    GrainCloud's per-grain jitter, the upcoming per-voice filter
    (feature/per-voice-filter), DelayInstance taps, and
    TrajectoryEngine's mean-reverting orbit radius all draw from, instead
    of each hand-rolling its own distribution math and RNG. Header-only,
    pure math (no JUCE GUI/audio dependency) -- same convention as
    SlingGesture.h/OrbitMath.h in this project, so it can be included
    anywhere (audio thread, tests) without pulling in anything heavy.
*/

// ============================================================================
// SeededRandom
// ============================================================================

/**
    A small, serializable random source: wraps juce::Random (itself
    allocation-free and audio-thread-safe -- no std::random_device, no
    per-call allocation) with an explicit, stored seed, so "what this
    source produces" is fully determined by that one number. The seed is
    meant to be saved with a preset (see GrainCloudSettings::seed,
    SoundObject::orbitNoiseSeed) specifically so an interesting result
    found by ear (see Docs/experiments/) stays exactly reproducible on
    reload, not just "roughly similar" -- the previous single shared
    juce::Random (PluginProcessor::grainRandom) couldn't offer that: its
    output depended on the combined call order of every object's cloud
    together, not on any one reproducible number.

    Threading: construct/reseed only from the message thread (the control-
    rate timer, same thread this is sampled from) -- juce::Random itself
    has no thread-safety of its own, and nothing here needs the audio
    thread to touch randomness directly (grains/taps are *decided* at
    control rate, then played back deterministically from their already-
    chosen parameters).
*/
class SeededRandom
{
public:
    // Default: seeded unpredictably (reseedRandomly() below), matching
    // what juce::Random's OWN default constructor actually does --
    // Random::Random() delegates to a private ctor whose body calls
    // setSeedRandomly() (see juce_Random.cpp), mixing in this object's
    // address, the global atomic seed counter, and several clocks. (Its
    // member initializer list sets `seed(1)` first, but that's only ever
    // a placeholder immediately overwritten in the constructor body --
    // NOT the actual default behavior; easy to misread from the
    // initializer list alone.) So two freshly default-constructed
    // SeededRandoms are expected to differ, same as two freshly
    // default-constructed juce::Randoms would -- exactly the property a
    // fresh/reactivated GrainCloud or TrajectoryEngine slot wants (see
    // GrainCloud's constructor, TrajectoryEngine::reseedOrbitNoise()):
    // distinct cloud/object instances shouldn't wander in lockstep from
    // sharing one implicit default.
    SeededRandom() { reseedRandomly(); }
    explicit SeededRandom (juce::int64 initialSeed) { setSeed (initialSeed); }

    void setSeed (juce::int64 newSeed)
    {
        seed = newSeed;
        rng.setSeed (newSeed);
    }

    juce::int64 getSeed() const { return seed; }

    // "New seed" button behaviour -- picks an unpredictable new seed (not
    // reproducible itself, by definition), then becomes a fixed, saved,
    // reproducible number again from this point on. Uses JUCE's own
    // system RNG (not this->rng) so reseeding doesn't depend on -- or
    // disturb reproducibility of -- whatever sequence this source may
    // already be partway through.
    void reseedRandomly() { setSeed (juce::Random::getSystemRandom().nextInt64()); }

    float nextFloat01()  { return rng.nextFloat(); }                  // [0, 1)
    float nextBipolar()  { return rng.nextFloat() * 2.0f - 1.0f; }     // [-1, 1)

    // [0, exclusiveMax) -- e.g. picking a uniformly random index into a
    // small candidate list (see GrainCloud.cpp's pickQuantizedSemitoneOffset()).
    int nextInt (int exclusiveMax) { return rng.nextInt (exclusiveMax); }

    // Box-Muller transform -- one standard-normal sample per call (the
    // transform actually produces two independent ones; using only one
    // keeps this call stateless/simple rather than caching the second,
    // same tradeoff TrajectoryEngine::nextGaussian() already made before
    // this extraction -- fine at control rate, not an audio-rate hot path).
    float nextGaussian()
    {
        constexpr float minInput = 1.0e-6f; // avoids log(0)
        const float u1 = juce::jmax (minInput, rng.nextFloat());
        const float u2 = rng.nextFloat();
        return std::sqrt (-2.0f * std::log (u1)) * std::cos (juce::MathConstants<float>::twoPi * u2);
    }

private:
    // Both members' default member initializers below are never actually
    // observed -- SeededRandom()'s body (above) always calls
    // reseedRandomly() before anything else can read either of them,
    // which sets both together via setSeed(). The explicit `juce::Random
    // rng (1)` constructs the same `rng` default-constructing would, just
    // without that redundant extra reseedRandomly() work.
    juce::int64 seed = 0;
    juce::Random rng { (juce::int64) 1 };
};

// ============================================================================
// RandomizedParam
// ============================================================================

/**
    How a RandomizedParam turns its own base/spread into a sampled value.
    Discrete picks uniformly from discreteValues and ignores base/spread
    entirely -- everything else samples a continuous value from
    [base-spread, base+spread] (LogUniform's spread is a RATIO, not an
    absolute offset -- see its own comment below), optionally then snapped
    to the nearest entry in quantizeValues (RandomizedParam's OWN
    quantizeToSet, orthogonal to distribution -- any continuous
    distribution can be quantized afterward, the same "continuous reach,
    then snap to a scale degree" shape GrainCloud's existing
    PitchJitterMode::Scale already uses for pitch specifically; this
    generalizes that idea to any parameter).
*/
enum class RandomDistribution
{
    Uniform,     // flat within [base-spread, base+spread]
    Normal,      // Gaussian, spread = standard deviation (unbounded tails, unlike the others)
    Ramp,        // linearly biased toward one end of the range -- see rampTowardMax
    LogUniform,  // flat in LOG space -- for frequencies, where linear jitter feels lopsided (see spread's own comment)
    PowerLaw,    // tunable power-shaped skew, more extreme than Ramp -- see powerExponent
    Discrete     // uniformly picks one of discreteValues; base/spread/everything else below is unused
};

/**
    One randomizable scalar: a base value, how far it may wander, which
    shape that wandering follows, and an optional snap to a fixed set of
    values afterward. This is the ONE sampling implementation every
    randomized parameter in this project is meant to route through from
    here on (per-grain jitter already migrated, see GrainCloud.cpp; the
    upcoming per-voice filter and DelayInstance are built on this from the
    start) -- not a parallel system alongside the existing per-field
    *Jitter floats, which keep their own storage/behaviour exactly as
    before for preset-compatibility reasons (see GrainCloud.cpp's
    sampleLegacyJitter()), but now sample through the same SeededRandom
    primitives underneath.
*/
struct RandomizedParam
{
    float base = 0.0f;

    // Meaning depends on distribution:
    //  - Uniform/Normal/Ramp/PowerLaw: an absolute offset in the
    //    parameter's own real units (e.g. +/-200 Hz around a 1000 Hz
    //    cutoff base).
    //  - LogUniform: a multiplicative RATIO, not an offset -- the sampled
    //    value lies in [base/spread, base*spread]. A spread of 2.0 means
    //    "within one octave either way" for a frequency-like base. Kept
    //    deliberately >= 1 (clamped at sample time) since a ratio < 1
    //    would invert min/max.
    //  - Discrete: unused (see discreteValues instead).
    float spread = 0.0f;

    RandomDistribution distribution = RandomDistribution::Uniform;

    // Ramp only: true biases the sampled value toward base+spread (the
    // high end), false toward base-spread (the low end). Fixed sqrt()
    // shaping, not a tunable exponent -- see powerExponent below for the
    // distribution that deliberately DOES expose one; Ramp is meant to be
    // the "simple, one-knob lean" option, PowerLaw the "more extreme,
    // tunable skew" one. Two genuinely different use cases, not a
    // redundant pair.
    bool rampTowardMax = true;

    // PowerLaw only: shapes a uniform(0,1) sample via std::pow(u, exponent)
    // before mapping it onto [base-spread, base+spread]. exponent > 1
    // biases toward the LOW end (base-spread) -- most samples small, a
    // long tail of rare large ones, the classic "many small events, few
    // big ones" power-law feel (e.g. sparse rhythmic density, mostly
    // short/quiet with occasional long/loud outliers). exponent < 1
    // biases toward the HIGH end instead. exponent == 1 is exactly
    // Uniform. Deliberately NOT a true Pareto/xmin-based power law (which
    // needs a hard lower bound and diverges at x=0) -- this is a simpler,
    // bounded, practical power-SHAPED distribution in the same
    // "disclosed, good-enough-for-sound-design simplification" spirit as
    // e.g. PropagationProcessor's own air-absorption model.
    float powerExponent = 2.0f;

    // Discrete only: candidates sampled uniformly (each equally likely --
    // no weighting). base/spread/distribution-shape are irrelevant when
    // this is non-empty AND distribution == Discrete.
    std::vector<float> discreteValues;

    // Orthogonal to distribution (see this struct's own comment): if
    // true, the continuously-sampled value is snapped to the nearest
    // entry in quantizeValues afterward. Ignored when quantizeValues is
    // empty, even if this is true (defensive, not an error case -- an
    // empty quantization set behaves like "no quantization" rather than
    // "always return 0").
    bool quantizeToSet = false;
    std::vector<float> quantizeValues;

    float sample (SeededRandom& rng) const;

    // Deliberate bit-exact field comparison -- used to detect "did a
    // caller actually write a new value since I last cached this" (see
    // DelayInstance::resyncTapConfigIfChanged()'s own comment on this
    // same convention, applied one level up to a whole Modifier), not an
    // approximate-equality question.
    bool operator== (const RandomizedParam& other) const
    {
        return base == other.base && spread == other.spread && distribution == other.distribution
            && rampTowardMax == other.rampTowardMax && powerExponent == other.powerExponent
            && discreteValues == other.discreteValues
            && quantizeToSet == other.quantizeToSet && quantizeValues == other.quantizeValues;
    }
    bool operator!= (const RandomizedParam& other) const { return ! (*this == other); }
};

namespace RandomEngineDetail
{
    inline float nearestInSet (float value, const std::vector<float>& set)
    {
        float best = set.front();
        float bestDist = std::abs (value - best);
        for (size_t i = 1; i < set.size(); ++i)
        {
            const float dist = std::abs (value - set[i]);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = set[i];
            }
        }
        return best;
    }
}

inline float RandomizedParam::sample (SeededRandom& rng) const
{
    if (distribution == RandomDistribution::Discrete)
    {
        if (discreteValues.empty())
            return base;
        const int index = juce::jlimit (0, (int) discreteValues.size() - 1,
                                         (int) (rng.nextFloat01() * (float) discreteValues.size()));
        return discreteValues[(size_t) index];
    }

    float result = base;

    switch (distribution)
    {
        case RandomDistribution::Uniform:
            result = base + spread * rng.nextBipolar();
            break;

        case RandomDistribution::Normal:
            result = base + spread * rng.nextGaussian();
            break;

        case RandomDistribution::Ramp:
        {
            const float u = rng.nextFloat01();
            const float shaped = rampTowardMax ? std::sqrt (u) : 1.0f - std::sqrt (1.0f - u);
            result = (base - spread) + 2.0f * spread * shaped;
            break;
        }

        case RandomDistribution::LogUniform:
        {
            const float ratio = juce::jmax (1.0f + 1.0e-6f, spread);
            if (base <= 0.0f)
            {
                jassertfalse; // LogUniform needs a positive base (it's meant for frequencies) -- falls back to base rather than producing NaN/Inf
                result = base;
                break;
            }
            const float logMin = std::log (base / ratio);
            const float logMax = std::log (base * ratio);
            result = std::exp (logMin + (logMax - logMin) * rng.nextFloat01());
            break;
        }

        case RandomDistribution::PowerLaw:
        {
            const float u = rng.nextFloat01();
            const float shaped = std::pow (u, juce::jmax (0.01f, powerExponent));
            result = (base - spread) + 2.0f * spread * shaped;
            break;
        }

        case RandomDistribution::Discrete:
            break; // handled above, unreachable here
    }

    if (quantizeToSet && ! quantizeValues.empty())
        result = RandomEngineDetail::nearestInSet (result, quantizeValues);

    return result;
}

// ============================================================================
// Mean-reverting (Ornstein-Uhlenbeck) process
// ============================================================================

namespace MeanReverting
{
    // One discrete-time step of an Ornstein-Uhlenbeck process: wanders
    // around `baseline` instead of drifting away permanently, pulled back
    // proportionally to how far it's strayed (reversionRate) and pushed by
    // smoothed Gaussian noise (noiseAmplitude). Extracted, unchanged,
    // from what was TrajectoryEngine::integrate()'s own orbit-radius-only
    // inline math -- now also the basis for feature/per-voice-filter's
    // optional time modulation and feature/agitation-and-snapshots'
    // Agitation, instead of three separate reimplementations of the same
    // formula (exactly the duplication risk flagged when this branch was
    // scoped).
    //
    // current/baseline/reversionRate/noiseAmplitude/noiseSmoothing: same
    // meaning as SoundObject::orbitRadius/-Baseline/-ReversionRate/
    // -NoiseAmplitude/-NoiseSmoothing had before this extraction. dt:
    // seconds since the last step. gaussianSample: one std-normal sample
    // (call rng.nextGaussian() yourself -- kept external rather than
    // taking a SeededRandom& here so a caller that already drew its
    // sample for another reason this tick doesn't need a second RNG call).
    // smoothedNoise: PERSISTENT state the caller owns (e.g.
    // SoundObject::orbitRadiusNoiseSmoothed) and passes by reference each
    // call -- this function is otherwise stateless.
    //
    // Returns the new value for `current`; the caller clamps to its own
    // valid range (this function doesn't know what that is -- orbit
    // radius and a filter cutoff have very different valid ranges).
    //
    // Reversion/noise both exactly 0 is treated as "disabled" by every
    // existing caller (an explicit `if` around the call site, not inside
    // this function) -- kept that way rather than special-cased here, so
    // the no-op case costs a caller-side branch instead of this function
    // silently doing nothing while still touching smoothedNoise.
    inline float step (float current, float baseline, float reversionRate, float noiseAmplitude,
                        float noiseSmoothing, float dt, float gaussianSample, float& smoothedNoise)
    {
        const float reversion = reversionRate * (baseline - current) * dt;

        const float smoothingCoeff = noiseSmoothing > 0.0f ? std::exp (-dt / noiseSmoothing) : 0.0f;
        smoothedNoise = smoothingCoeff * smoothedNoise + (1.0f - smoothingCoeff) * gaussianSample;

        const float noise = noiseAmplitude * std::sqrt (juce::jmax (0.0f, dt)) * smoothedNoise;
        return current + reversion + noise;
    }
}
