#include "ParameterRegistry.h"

float ParameterRegistry::Descriptor::normalize (float realValue) const
{
    const float range = juce::jmax (1.0e-9f, maxValue - minValue);
    const float t = juce::jlimit (0.0f, 1.0f, (realValue - minValue) / range);
    return polarity == Polarity::Bipolar ? (t * 2.0f - 1.0f) : t;
}

float ParameterRegistry::Descriptor::denormalize (float normalizedValue) const
{
    const float t = polarity == Polarity::Bipolar
        ? juce::jlimit (0.0f, 1.0f, (normalizedValue + 1.0f) * 0.5f)
        : juce::jlimit (0.0f, 1.0f, normalizedValue);
    return minValue + t * (maxValue - minValue);
}

void ParameterRegistry::registerParameter (Descriptor descriptor)
{
    jassert (idToIndex.find (descriptor.id) == idToIndex.end()); // see the header's own comment on ID collisions

    idToIndex[descriptor.id] = parameters.size();
    parameters.push_back (std::move (descriptor));
}

const ParameterRegistry::Descriptor* ParameterRegistry::find (const juce::String& id) const
{
    const auto it = idToIndex.find (id);
    return it == idToIndex.end() ? nullptr : &parameters[it->second];
}
