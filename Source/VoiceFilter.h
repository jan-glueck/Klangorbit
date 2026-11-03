#pragma once
#include "RandomEngine.h"
#include <cmath>

/**
    Shared per-voice filter module (feature/per-voice-filter, Branch 1 of
    the generative sound design effort) -- a single multi-mode resonant
    filter cheap enough to give EVERY simultaneously active voice (grain,
    delay tap, ...) its own independent instance, the same way each grain
    already gets its own playbackRate/position. Deliberately standalone,
    with no GrainCloud/DelayInstance dependency: feature/grain-filter-
    integration wires this into GrainCloud, a later DelayInstance branch
    wires it into delay taps, both drawing on the exact same code instead
    of each hand-rolling a filter.

    DSP: a zero-delay-feedback ("TPT") state-variable filter (Andrew
    Simper's topology, the same one used in most modern analog-modeled
    softsynths) -- LowPass/BandPass come out of the core recursion
    directly, HighPass/Notch are cheap combinations of those plus the
    input, all four sharing one set of coefficients. Chosen over
    juce::dsp::IIR::Filter specifically because its coefficients are a
    plain struct here (no std::shared_ptr, no heap touch), and because TPT
    stays numerically stable even when cutoff/resonance change every
    block (ordinary biquads can briefly misbehave under fast coefficient
    changes) -- both matter once up to GrainLimits::maxConcurrentGrainsGlobal
    instances may exist at once and this module's own drift (see below)
    changes coefficients continuously.

    Randomization: cutoffHz/resonance are RandomizedParam (RandomEngine.h)
    -- each voice samples its own value once at spawn (VoiceFilter::start()),
    the same "decided once, fixed for this voice's life" treatment Grain::
    playbackRate already uses for pitch jitter. On top of that, an
    OPTIONAL continuous drift lets a voice's cutoff keep wandering around
    its own sampled value for as long as the voice lives, via the same
    Ornstein-Uhlenbeck process (MeanReverting::step()) already driving
    orbit radius -- disabled (driftAmplitude == 0) by default, costing
    nothing beyond the static filter when unused.

    Real-time-safe: nothing here allocates after construction. start()
    and advanceDrift() both call SeededRandom, so (like every other
    control-rate randomization in this project) must be called from the
    control thread, never the audio thread -- advanceDrift() is meant to
    be called once per BLOCK (control rate), not once per sample, same
    cadence GrainCloud::update()/TrajectoryEngine::update() already use;
    processSample() is the only per-sample call and touches no RNG at all.

    A real-time consumer that itself splits decision state (control rate)
    from DSP state (audio rate) across a thread boundary -- as GrainCloud/
    PluginProcessor do for grains, see feature/grain-filter-integration --
    doesn't use start()/advanceDrift() at all: the control-rate side
    samples/drifts its OWN plain cutoff/resonance floats directly via
    RandomizedParam::sample()/MeanReverting::step() (same primitives these
    methods wrap) and publishes them; the audio-rate side then only ever
    calls resetWithValues()/setCutoffAndResonance()/processSample(), none
    of which touch SeededRandom. start()/advanceDrift() remain the
    convenient all-in-one path for any consumer that doesn't need that
    split (e.g. this module's own tests).
*/

enum class VoiceFilterType
{
    LowPass,
    HighPass,
    BandPass,
    Notch
};

namespace VoiceFilterLimits
{
    // Keeps the TPT coefficients (which involve tan(pi * cutoff / sampleRate))
    // well away from the pole at Nyquist, where g -> infinity and the filter
    // would become unstable -- see VoiceFilter::updateCoefficients().
    constexpr float minCutoffHz = 20.0f;
    constexpr float maxCutoffRatioOfSampleRate = 0.45f;

    // Resonance (Q): clamped away from 0 (which would divide by zero in
    // k = 1/Q) and away from values high enough to self-oscillate into a
    // near-unbounded peak at audio sample rates.
    constexpr float minResonance = 0.1f;
    constexpr float maxResonance = 20.0f;
}

/**
    Per-source settings -- one set shared by every voice a given source
    (a GrainCloud, a DelayInstance, ...) spawns, same relationship
    GrainCloudSettings already has to its individual Grains.
*/
struct VoiceFilterSettings
{
    bool enabled = false;
    VoiceFilterType type = VoiceFilterType::LowPass;

    RandomizedParam cutoffHz { 2000.0f, 0.0f };
    RandomizedParam resonance { 0.707f, 0.0f };

    // --- Optional per-voice cutoff drift (Ornstein-Uhlenbeck) -----------
    // Wanders the voice's cutoff around its OWN spawn-time sampled value
    // (not around a fixed scene-wide baseline) -- see VoiceFilter::
    // advanceDrift(). 0 amplitude == disabled, the default.
    float driftReversionRate = 1.0f; // 1/s, pull-back strength toward the voice's own sampled cutoff
    float driftAmplitude = 0.0f;     // Hz, noise strength -- 0 disables drift entirely
    float driftSmoothing = 0.2f;     // seconds, smooths the underlying noise (same meaning as orbit's noiseSmoothing)
};

/**
    One voice's filter instance + its own randomized/drifting state.
    Owns no heap memory.
*/
class VoiceFilter
{
public:
    void prepare (double newSampleRate) { sampleRate = newSampleRate; }

    // Samples this voice's own cutoff/resonance from settings and resets
    // the filter's internal state -- call exactly once, when the voice
    // spawns, not per block.
    void start (const VoiceFilterSettings& settings, SeededRandom& rng)
    {
        type = settings.type;
        baseCutoffHz = settings.cutoffHz.sample (rng);
        currentResonance = settings.resonance.sample (rng);
        driftSmoothedNoise = 0.0f;
        ic1eq = 0.0f;
        ic2eq = 0.0f;
        updateCoefficients (baseCutoffHz, currentResonance);
    }

    // Advances the optional cutoff drift by dt seconds -- cheap no-op
    // whenever settings.driftAmplitude <= 0 (the default/disabled case).
    // Call once per block (control rate), same cadence as GrainCloud::
    // update()/TrajectoryEngine's own per-tick randomization.
    void advanceDrift (const VoiceFilterSettings& settings, float dt, SeededRandom& rng)
    {
        if (settings.driftAmplitude <= 0.0f)
            return;

        const float driftedCutoffHz = MeanReverting::step (
            lastAppliedCutoffHz, baseCutoffHz,
            settings.driftReversionRate, settings.driftAmplitude,
            settings.driftSmoothing, dt, rng.nextGaussian(), driftSmoothedNoise);

        updateCoefficients (driftedCutoffHz, currentResonance);
    }

    // Audio-thread-safe counterpart to start(): resets filter state for a
    // new voice whose cutoff/resonance have ALREADY been decided at
    // control rate elsewhere (e.g. GrainCloud::spawnGrain() sampling from
    // RandomizedParam and publishing the result through its Snapshot) --
    // touches no RNG, so unlike start()/advanceDrift() this is safe to
    // call from the audio thread. Used when a consumer's decision state
    // (which per-voice cutoff/resonance to use, and any drift on top of
    // it) must live on the control-rate side of a thread boundary this
    // class itself doesn't cross -- see GrainCloud.cpp/PluginProcessor.cpp
    // for exactly that split (Grain holds the decided values, GrainAudioState
    // holds this filter's audio-rate DSP state).
    void resetWithValues (VoiceFilterType newType, float cutoffHz, float resonanceQ)
    {
        type = newType;
        baseCutoffHz = cutoffHz;
        currentResonance = resonanceQ;
        driftSmoothedNoise = 0.0f;
        ic1eq = 0.0f;
        ic2eq = 0.0f;
        updateCoefficients (cutoffHz, resonanceQ);
    }

    // Audio-thread-safe counterpart to advanceDrift(): re-derives this
    // block's coefficients from an already-decided cutoff/resonance (e.g.
    // this tick's post-drift snapshot value) WITHOUT touching ic1eq/ic2eq
    // -- keeps the filter's own state continuous across blocks instead of
    // restarting it, so a changing control-rate value doesn't click.
    // Touches no RNG.
    void setCutoffAndResonance (float cutoffHz, float resonanceQ)
    {
        currentResonance = resonanceQ;
        updateCoefficients (cutoffHz, resonanceQ);
    }

    // The only per-sample call -- touches no RNG, allocates nothing.
    float processSample (float input)
    {
        const float v3 = input - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        switch (type)
        {
            case VoiceFilterType::LowPass:  return v2;
            case VoiceFilterType::BandPass: return v1;
            case VoiceFilterType::HighPass: return input - k * v1 - v2;
            case VoiceFilterType::Notch:    return input - k * v1;
        }
        return v2; // unreachable, silences -Wreturn-type
    }

    float getCurrentCutoffHz() const { return lastAppliedCutoffHz; }

private:
    void updateCoefficients (float cutoffHz, float q)
    {
        cutoffHz = juce::jlimit (VoiceFilterLimits::minCutoffHz,
                                  (float) sampleRate * VoiceFilterLimits::maxCutoffRatioOfSampleRate,
                                  cutoffHz);
        q = juce::jlimit (VoiceFilterLimits::minResonance, VoiceFilterLimits::maxResonance, q);
        lastAppliedCutoffHz = cutoffHz;

        const float g = std::tan (juce::MathConstants<float>::pi * cutoffHz / (float) sampleRate);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double sampleRate = 48000.0;
    VoiceFilterType type = VoiceFilterType::LowPass;

    float baseCutoffHz = 2000.0f;      // this voice's own spawn-time sampled cutoff -- drift's baseline
    float lastAppliedCutoffHz = 2000.0f; // post-drift, post-clamp value the current coefficients reflect
    float currentResonance = 0.707f;
    float driftSmoothedNoise = 0.0f;

    // TPT state-variable filter state/coefficients.
    float ic1eq = 0.0f, ic2eq = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
};
