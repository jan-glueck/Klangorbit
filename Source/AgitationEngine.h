#pragma once
#include "Agitation.h"
#include "ParameterRegistry.h"
#include <unordered_map>

/**
    Owns one Agitation::Settings/State pair per ParameterRegistry
    parameter id, and drives all of them with a single generic
    advanceAll() call (feature/agitation-and-snapshots, Branch 5, Stage
    2) -- the piece that actually lets Agitation attach to ANY
    registered parameter (SoundObject/GrainCloudSettings/
    DelayInstanceSettings/SceneSettings fields alike) without a
    per-parameter-type integration, by going exclusively through
    ParameterRegistry::Descriptor's own type-erased getValue()/
    setValue() -- see Agitation.h's own class comment for why that
    matters.

    Entries are created lazily (settingsFor() default-constructs one on
    first access, same "touching it is enough to bring it into
    existence" convention `std::unordered_map::operator[]` itself
    already has) -- there's no need to pre-populate one for every
    registered parameter up front, since the overwhelming majority of
    them will never have Agitation turned on.

    The false -> true transition of Settings::enabled (however it
    happened -- UI, a preset load, a Snapshot recall) is detected
    inside advanceAll() itself, which then calls Agitation::start() to
    capture a fresh baseline from the parameter's OWN current value,
    exactly once for that transition -- callers (UI code) only ever
    need to flip settings.enabled themselves, never call start()
    directly; this is what keeps a UI binding for this completely
    generic too (just toggling a bool, like any other toggle row).

    Threading: advanceAll() calls straight through to whichever
    Descriptor::getValue()/setValue() lambdas buildParameterRegistry()
    registered -- same message-thread-only rule those already carry
    (see ParameterRegistry.h), same cadence as TrajectoryEngine::
    update()/GrainCloud::update() (once per control-rate tick, not per
    sample).
*/
class AgitationEngine
{
public:
    // Lazily creates a default (disabled) entry on first access --
    // UI code reads/writes directly through the returned reference
    // (e.g. toggling .enabled, dragging .amount), same as any other
    // plain-struct-field binding in this project.
    Agitation::Settings& settingsFor (const juce::String& parameterId)
    {
        return entries[parameterId].settings;
    }

    // Read-only variant for UI code that only needs to display the
    // current configuration (e.g. refreshFromModel()-style refresh
    // passes) without implicitly creating an entry for every parameter
    // it merely glances at.
    const Agitation::Settings* findSettings (const juce::String& parameterId) const
    {
        const auto it = entries.find (parameterId);
        return it != entries.end() ? &it->second.settings : nullptr;
    }

    // Removes this parameter's entry entirely (back to "never
    // configured" -- indistinguishable from one that was never
    // touched). Used when a Snapshot recall or preset load needs to
    // clear Agitation for parameters the loaded data doesn't mention,
    // rather than leaving a stale enabled entry from before the load.
    void remove (const juce::String& parameterId) { entries.erase (parameterId); }

    void clear() { entries.clear(); }

    // Entries currently known (both enabled and disabled -- disabled
    // ones linger harmlessly, see settingsFor()'s own comment) --
    // iteration order follows std::unordered_map's own (unspecified,
    // but stable across calls as long as nothing is inserted/erased in
    // between), fine for this namespace's own "nobody cares about
    // order" usage (persistence, UI listing one row per configured
    // parameter).
    const std::unordered_map<juce::String, Agitation::Settings>& all() const
    {
        // Exposed as id -> Settings only (not the internal id -> Entry,
        // whose State/wasEnabled are this class's own private book-
        // keeping) -- rebuilt into a plain map here rather than exposing
        // Entry directly, since this is only ever called for occasional
        // whole-scene operations (persistence, UI listing), never per
        // tick.
        allSettingsCache.clear();
        for (auto& [id, entry] : entries)
            allSettingsCache.emplace (id, entry.settings);
        return allSettingsCache;
    }

    // Advances every currently-enabled entry by dt seconds, reading/
    // writing through `registry`'s own getValue()/setValue() -- the
    // one generic call site every registered parameter goes through
    // regardless of which struct it actually lives on. A parameter id
    // with an entry here but no longer found in `registry` (shouldn't
    // normally happen -- registration is a one-shot pass covering
    // every possible slot, see ParameterRegistry.h's own class
    // comment) is silently skipped rather than asserting/crashing.
    //
    // `bpm` is only consulted by entries with tempoSynced == true, see
    // Agitation::advance(). `rng` is this engine's own shared
    // randomization source -- deliberately ONE rng for every agitated
    // parameter (not one per parameter) is an intentional simplicity
    // choice for this stage: unlike GrainCloud/DelayInstance, there is
    // no natural "one independent stream per voice" unit here, and a
    // shared stream costs nothing extra since advanceAll() already
    // visits every entry from a single thread, one at a time.
    void advanceAll (const ParameterRegistry& registry, float dt, double bpm, SeededRandom& rng)
    {
        for (auto& [id, entry] : entries)
        {
            const bool justEnabled = entry.settings.enabled && ! entry.wasEnabled;
            entry.wasEnabled = entry.settings.enabled;

            if (! entry.settings.enabled || entry.settings.amount <= 0.0f)
                continue;

            const auto* descriptor = registry.find (id);
            if (descriptor == nullptr)
                continue;

            if (justEnabled)
                Agitation::start (entry.state, descriptor->getValue());

            const float newValue = Agitation::advance (entry.settings, entry.state, descriptor->getValue(), dt, bpm, rng);
            descriptor->setValue (newValue);
        }
    }

private:
    struct Entry
    {
        Agitation::Settings settings;
        Agitation::State state;
        // Tracks the previous tick's settings.enabled -- NOT persisted,
        // purely so advanceAll() can detect a false -> true transition
        // on whichever tick it actually happens, see this class's own
        // comment above.
        bool wasEnabled = false;
    };

    std::unordered_map<juce::String, Entry> entries;
    // Mutable: lazily rebuilt by all() (a const, logically read-only
    // method from callers' point of view) purely as a projection of
    // `entries` -- same "cache that doesn't change observable behavior"
    // treatment as e.g. GrainCloud's own snapshotBuffer.
    mutable std::unordered_map<juce::String, Agitation::Settings> allSettingsCache;
};
