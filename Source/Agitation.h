#pragma once
#include "RandomEngine.h"

/**
    Generic, parameter-type-agnostic "Agitation" engine (feature/
    agitation-and-snapshots, Branch 5 of the generative sound design
    effort) -- per-parameter, toggleable random modulation around
    whatever value the parameter had when Agitation was last turned on,
    with Amount/Rate/Smoothing knobs, built directly on top of
    `MeanReverting::step()` (RandomEngine.h) instead of a fourth
    reimplementation of the same Ornstein-Uhlenbeck formula (the first
    three being orbit-radius noise, VoiceFilter's cutoff drift, and the
    Movement modifier) -- see that function's own comment for why.

    Deliberately NOT tied to any specific parameter type (unlike
    VoiceFilterSettings::driftAmplitude or Modifier's own fields, each
    hardcoded to one struct's one field): Branch 5's own spec asks for
    Agitation to be attachable to potentially ANY parameter already
    exposed through `ParameterRegistry` (Source/ParameterRegistry.h),
    whose `Descriptor` is itself already generic over `getValue()`/
    `setValue()`. Keeping this engine free of any dependency on
    GrainCloudSettings/DelayInstanceSettings/SoundObject/etc. is what
    lets `advance()` below be called in a single generic loop over every
    registered parameter (see the later wiring stage) instead of one
    hand-written call site per parameter type, each duplicating the same
    four lines.

    Units: `Settings::amount`/`reversionRate`/`smoothing` are in whatever
    units the TARGET parameter itself uses (Hz, meters, semitones, ...)
    -- same convention as `RandomizedParam::spread` elsewhere in this
    project; this header has no opinion on what a sane range looks like
    for any particular parameter, same division of responsibility
    `MeanReverting::step()` itself already documents (caller clamps).

    Threading/real-time safety: `advance()` calls `SeededRandom`, so
    (like every other control-rate randomization in this project) must
    only be called from the control thread, never the audio thread --
    same cadence as `GrainCloud::update()`/`VoiceFilter::advanceDrift()`,
    once per block/tick, not once per sample. Nothing here allocates.
*/
namespace Agitation
{
    // Rate, optionally tied to tempo instead of a fixed absolute value --
    // see tempoDivisionToReversionRate() below. Covers straight divisions
    // plus their triplet variants, the common set offered by most
    // tempo-synced LFOs/modulators.
    enum class TempoDivision
    {
        Whole,
        Half,
        Quarter,
        Eighth,
        Sixteenth,
        QuarterTriplet,
        EighthTriplet,
        SixteenthTriplet
    };

    // Converts a tempo division into an equivalent reversionRate (1/s) at
    // the given bpm -- one full "return toward baseline" characteristic
    // time constant (1/reversionRate seconds) equals that division's own
    // note length at that tempo (e.g. Quarter at 120 BPM -> a 0.5s note
    // length -> reversionRate 2.0). bpm is clamped away from <= 0 (an
    // unset/invalid tempo) to avoid dividing by zero; callers that have
    // no tempo source at all (shouldn't happen given both the plugin's
    // host-tempo read and the Standalone's own BPM field, but kept safe
    // regardless) simply get a very slow rate rather than a crash.
    inline float tempoDivisionToReversionRate (TempoDivision division, double bpm)
    {
        double beatsPerNote = 1.0;
        constexpr double tripletScale = 2.0 / 3.0;
        switch (division)
        {
            case TempoDivision::Whole:            beatsPerNote = 4.0;                 break;
            case TempoDivision::Half:             beatsPerNote = 2.0;                 break;
            case TempoDivision::Quarter:          beatsPerNote = 1.0;                 break;
            case TempoDivision::Eighth:           beatsPerNote = 0.5;                 break;
            case TempoDivision::Sixteenth:        beatsPerNote = 0.25;                break;
            case TempoDivision::QuarterTriplet:   beatsPerNote = 1.0 * tripletScale;  break;
            case TempoDivision::EighthTriplet:    beatsPerNote = 0.5 * tripletScale;  break;
            case TempoDivision::SixteenthTriplet: beatsPerNote = 0.25 * tripletScale; break;
        }

        const double safeBpm = juce::jmax (1.0, bpm);
        const double noteLengthSeconds = 60.0 * beatsPerNote / safeBpm;
        return (float) (1.0 / noteLengthSeconds);
    }

    struct Settings
    {
        bool enabled = false;

        // 1/s, pull-back strength toward the captured baseline ("Rate") --
        // used directly unless tempoSynced, in which case
        // tempoDivisionToReversionRate() below overrides it each call.
        float reversionRate = 1.0f;
        // Noise strength, in the target parameter's own units ("Amount")
        // -- 0 (the default) disables modulation entirely, same "no
        // amplitude means no effect" early-out convention as
        // VoiceFilterSettings::driftAmplitude.
        float amount = 0.0f;
        // Seconds, smooths the underlying noise ("Glaettung") -- same
        // meaning as VoiceFilterSettings::driftSmoothing/orbit's own
        // noiseSmoothing.
        float smoothing = 0.2f;

        bool tempoSynced = false;
        TempoDivision tempoDivision = TempoDivision::Quarter;

        // Deliberate bit-exact field comparison -- same convention as
        // RandomizedParam/VoiceFilterSettings/Modifier's own operator==
        // elsewhere in this project (detects an in-place edit, not just a
        // wholesale replacement).
        bool operator== (const Settings& other) const
        {
            return enabled == other.enabled && reversionRate == other.reversionRate
                && amount == other.amount && smoothing == other.smoothing
                && tempoSynced == other.tempoSynced && tempoDivision == other.tempoDivision;
        }
        bool operator!= (const Settings& other) const { return ! (*this == other); }
    };

    // Persistent, per-parameter state -- the caller owns one instance per
    // agitated parameter (e.g. keyed by that parameter's own
    // ParameterRegistry id in the later wiring stage), same "caller owns
    // the persistent state, this namespace stays stateless" split
    // MeanReverting::step() itself already uses for smoothedNoise.
    struct State
    {
        float baseline = 0.0f;
        float smoothedNoise = 0.0f;
    };

    // Captures `currentValue` as the new center this modifier will wander
    // around, and resets the smoothed noise -- call exactly once whenever
    // Settings::enabled transitions false -> true for this parameter (NOT
    // every tick), same "fresh start, no stale state" treatment as
    // VoiceFilter::start(). Forgetting to call this after re-enabling
    // leaves baseline at whatever it was the last time Agitation was on
    // for this parameter, which is harmless but means the modulation
    // wanders around a stale center instead of wherever the parameter
    // actually sits now.
    inline void start (State& state, float currentValue)
    {
        state.baseline = currentValue;
        state.smoothedNoise = 0.0f;
    }

    // Advances one control-rate tick and returns the new value to write
    // back to the target parameter. A cheap no-op (returns currentValue
    // completely unchanged, including leaving `state` untouched) whenever
    // !settings.enabled or settings.amount <= 0 -- same early-out
    // convention as VoiceFilter::advanceDrift(), and deliberately so: a
    // caller driving many parameters at once (the later wiring stage)
    // can call this unconditionally for every registered parameter every
    // tick without needing its own enabled-check first.
    //
    // `bpm` is only consulted while settings.tempoSynced is true -- pass
    // whatever this call site's own tempo source resolves to (host
    // playhead tempo in the plugin, the Standalone's own BPM field
    // otherwise); harmless to pass any positive placeholder value while
    // tempoSynced is false.
    inline float advance (const Settings& settings, State& state, float currentValue,
                           float dt, double bpm, SeededRandom& rng)
    {
        if (! settings.enabled || settings.amount <= 0.0f)
            return currentValue;

        const float reversionRate = settings.tempoSynced
            ? tempoDivisionToReversionRate (settings.tempoDivision, bpm)
            : settings.reversionRate;

        return MeanReverting::step (currentValue, state.baseline, reversionRate,
                                     settings.amount, settings.smoothing, dt,
                                     rng.nextGaussian(), state.smoothedNoise);
    }
}
