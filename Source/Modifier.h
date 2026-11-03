#pragma once
#include <array>
#include <vector>
#include "RandomEngine.h"
#include "VoiceFilter.h"

/**
    feature/modifier-stack (Branch 4 of the generative sound design
    effort) -- a per-source (GrainCloud, DelayInstance), ORDERED,
    stackable list of simple per-voice modifiers. "Per voice" means
    each individual grain/delay tap decides its own modifier outcome
    independently (sampled once, same "decided once, fixed for this
    voice's life" treatment this project already uses for pitchJitter/
    VoiceFilter cutoff/etc.), not once per cloud/instance -- see
    ModifierStack::sample() below.

    Order only matters between multiple Filter-type slots (a filter
    cascade's order genuinely changes the sound, especially with
    resonance). Gain/TimeOffset/Pitch are commutative scalar
    accumulations -- a constant gain multiplier, a time offset in
    seconds, a semitone sum -- that mathematically don't care where in
    the stack they sit, or relative to a Filter slot: scaling a signal
    before or after a LINEAR filter gives an identical result, and
    "shift the playback rate"/"shift the read start time" aren't
    per-sample operations at all, just spawn-time parameter
    adjustments. This is a disclosed, deliberate simplification --
    documented here, not hidden -- not a limitation of the UI (every
    slot can still be freely reordered, so the stack reads naturally
    top-to-bottom), just a fact about what reordering actually changes.

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
    Pitch
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
            && filter == other.filter && pitchSemitones == other.pitchSemitones;
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

    // Result of processing a stack once (control rate, at voice spawn/
    // config time) for ONE voice. Gain/TimeOffset/Pitch are pre-reduced
    // to single aggregate scalars (see this file's own class comment on
    // why that's valid); Filter slots keep their stack-order-preserving
    // list.
    struct Outcome
    {
        float gainMultiplier = 1.0f;
        float timeOffsetSeconds = 0.0f;
        float pitchSemitones = 0.0f;
        FilterBankState filters;
    };

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
