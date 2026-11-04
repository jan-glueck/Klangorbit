#pragma once
#include "SnapshotManager.h"
#include "ParameterRegistry.h"
#include <cmath>

/**
    Soft crossfade between two Snapshots (feature/agitation-and-
    snapshots, Branch 5, Stage 4's "weiches Ueberblenden (Morphing)
    zwischen zwei Snapshots mit Glaettung"). `morph` (0 = pure A, 1 =
    pure B) is the UI's own target value, dragged directly -- `advance()`
    smooths ONE shared value toward it (`smoothedMorph`, a simple
    one-pole filter, same `exp(-dt/smoothing)` shape used everywhere
    else in this project, e.g. VoiceFilter's cutoff drift) and applies
    THAT to every parameter uniformly, rather than giving each
    parameter its own independent smoother -- simpler, and every
    morphed parameter then reaches its target at the same felt rate,
    instead of some arriving before others.

    Only parameters present in BOTH snapshots' own entries are ever
    morphed -- one present in only A or only B has no defined "other
    endpoint" to blend toward, so it's silently excluded rather than
    guessing (e.g. holding it fixed, or fading it to/from some default)
    -- see setEndpoints().

    Threading: advance() calls straight through to whichever
    Descriptor::setValue() lambdas buildParameterRegistry() registered
    -- same message-thread-only rule every other control-rate consumer
    of ParameterRegistry already carries (AgitationEngine::advanceAll(),
    GrainCloud::update(), ...).
*/
class SnapshotMorphEngine
{
public:
    // Replaces whatever endpoints were set before with the parameter
    // ids common to BOTH `a` and `b` (see this class's own comment).
    // Always restarts smoothedMorph at 0 (pure A), regardless of
    // whatever `morph` itself is currently set to -- a crossfade with
    // a freshly (re)defined pair of endpoints starting anywhere BUT A
    // would be surprising, and this guarantees advance()'s very first
    // tick afterward always has a defined, smoothable starting point
    // rather than snapping straight to `morph` with no smoothing at
    // all on that one tick.
    void setEndpoints (const SnapshotManager::Snapshot& a, const SnapshotManager::Snapshot& b)
    {
        entries.clear();
        smoothedMorph = 0.0f;

        for (auto& entryA : a.entries)
            for (auto& entryB : b.entries)
                if (entryA.id == entryB.id)
                {
                    entries.push_back ({ entryA.id, entryA.value, entryB.value });
                    break;
                }
    }

    // Back to "nothing to morph" -- advance() becomes a complete no-op.
    void clear()
    {
        entries.clear();
        smoothedMorph = 0.0f;
    }

    bool hasEndpoints() const { return ! entries.empty(); }
    int getNumMorphedParameters() const { return (int) entries.size(); }

    float morph = 0.0f;     // 0 = pure A, 1 = pure B -- the UI's own target
    float smoothing = 0.2f; // seconds, one-pole smoothing time constant toward `morph`

    float getSmoothedMorph() const { return smoothedMorph; }

    // Advances one control-rate tick -- a no-op while hasEndpoints() ==
    // false, same early-out convention as Agitation::advance().
    void advance (const ParameterRegistry& registry, float dt)
    {
        if (entries.empty())
            return;

        if (smoothing > 0.0f)
        {
            const float coeff = std::exp (-dt / smoothing);
            smoothedMorph = coeff * smoothedMorph + (1.0f - coeff) * morph;
        }
        else
        {
            smoothedMorph = morph;
        }

        for (auto& e : entries)
        {
            const auto* descriptor = registry.find (e.id);
            if (descriptor != nullptr)
                descriptor->setValue (e.valueA + (e.valueB - e.valueA) * smoothedMorph);
        }
    }

private:
    struct Entry { juce::String id; float valueA = 0.0f; float valueB = 0.0f; };
    std::vector<Entry> entries;
    float smoothedMorph = 0.0f;
};
