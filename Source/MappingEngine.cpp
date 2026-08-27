#include "MappingEngine.h"

namespace
{
    // Converts a normalized value from one polarity's native range to
    // another's, preserving RELATIVE POSITION across the full range --
    // e.g. a bipolar stick axis (-1..1) bound to a unipolar parameter
    // (0..1, like Gain) should still be able to reach the parameter's
    // FULL range across the stick's FULL travel, not just its upper half
    // (which is what would happen if the bipolar value were simply
    // clamped into [0,1] instead of properly rescaled).
    float convertPolarity (float value, ParameterRegistry::Polarity from, ParameterRegistry::Polarity to)
    {
        if (from == to)
            return value;

        if (from == ParameterRegistry::Polarity::Bipolar) // to == Unipolar
            return (value + 1.0f) * 0.5f;

        // from == Unipolar, to == Bipolar
        return value * 2.0f - 1.0f;
    }
}

MappingEngine::MappingEngine (const ParameterRegistry& registryToRead) : registry (registryToRead) {}

void MappingEngine::addBinding (MappingBinding binding)
{
    removeBinding (binding.sourceId, binding.bank); // enforce at-most-one-binding-per-(sourceId,bank), see header comment
    bindings.push_back (std::move (binding));
}

void MappingEngine::removeBinding (const juce::String& sourceId, int bank)
{
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(),
                                     [&] (const MappingBinding& b) { return b.sourceId == sourceId && b.bank == bank; }),
                     bindings.end());
}

bool MappingEngine::hasBindingFor (const juce::String& sourceId, int bank) const
{
    for (auto& b : bindings)
        if (b.sourceId == sourceId && b.bank == bank)
            return true;
    return false;
}

void MappingEngine::applyBinding (const MappingBinding& binding, const CanonicalInputEvent& event)
{
    const auto* descriptor = registry.find (binding.parameterId);
    if (descriptor == nullptr || ! descriptor->setValue)
        return; // the bound parameter no longer exists (shouldn't normally happen -- the registry is built once at startup) -- stay defensive, not a crash

    const float converted = convertPolarity (event.value, event.polarity, descriptor->polarity);
    descriptor->setValue (descriptor->denormalize (converted));
}

void MappingEngine::canonicalInputReceived (const CanonicalInputEvent& event)
{
    // Track the modifier's held state regardless of Learn mode or
    // existing bindings -- this IS the paging mechanism itself, always
    // active, not something that can be turned off or overridden by a
    // binding (the modifier source is reserved for this one purpose).
    if (event.sourceId == modifierSourceId)
    {
        modifierHeld = (event.value >= 0.5f);
        return; // never itself learnable/bindable -- see below
    }

    if (isLearning())
    {
        addBinding ({ event.sourceId, learningParameterId, getCurrentBank() });
        learningParameterId.clear();
        return;
    }

    const int bank = getCurrentBank();
    for (auto& b : bindings)
        if (b.sourceId == event.sourceId && b.bank == bank)
            applyBinding (b, event);
}
