#include "AutomationParameterBridge.h"
#include <map>

RegistryAutomationParameter::RegistryAutomationParameter (const ParameterRegistry::Descriptor& descriptorToWrap)
    : juce::RangedAudioParameter (juce::ParameterID (descriptorToWrap.id, 1), descriptorToWrap.displayName),
      descriptor (descriptorToWrap),
      range (descriptorToWrap.minValue, descriptorToWrap.maxValue),
      defaultValueNormalized (range.convertTo0to1 (descriptorToWrap.getValue()))
{
}

float RegistryAutomationParameter::getValue() const
{
    return range.convertTo0to1 (descriptor.getValue());
}

void RegistryAutomationParameter::setValue (float newValue)
{
    descriptor.setValue (range.convertFrom0to1 (newValue));
}

float RegistryAutomationParameter::getDefaultValue() const
{
    return defaultValueNormalized;
}

float RegistryAutomationParameter::getValueForText (const juce::String& text) const
{
    return range.convertTo0to1 (text.getFloatValue());
}

const juce::NormalisableRange<float>& RegistryAutomationParameter::getNormalisableRange() const
{
    return range;
}

std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>>
    buildAutomationParameterGroups (const std::vector<ParameterRegistry::Descriptor>& descriptors)
{
    using Descriptor = ParameterRegistry::Descriptor;

    // objectId (0-based) -> category -> descriptors in that category, in
    // registration order. std::map (not unordered_map) purely so the
    // resulting group/parameter order comes out stable and sorted, not
    // dependent on the caller's own registration order.
    std::map<int, std::map<juce::String, std::vector<const Descriptor*>>> perObject;
    std::map<juce::String, std::vector<const Descriptor*>> global;

    for (auto& d : descriptors)
    {
        if (d.scope == ParameterRegistry::Scope::SpecificObject)
            perObject[d.objectId][d.category].push_back (&d);
        else if (d.scope == ParameterRegistry::Scope::Global)
            global[d.category].push_back (&d);
        // Scope::SelectedObject deliberately skipped -- see this
        // function's own header comment.
    }

    // Alphanumeric-only group ID -- AudioProcessorParameterGroup's own doc
    // comment says not to use "." or other special characters (category
    // strings like "Object Physics" contain a space).
    static const juce::String idChars ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    auto makeCategoryGroup = [] (const juce::String& groupIdPrefix, const juce::String& category,
                                  const std::vector<const Descriptor*>& groupDescriptors)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            groupIdPrefix + "_" + category.retainCharacters (idChars), category, " | ");
        for (auto* d : groupDescriptors)
            group->addChild (std::make_unique<RegistryAutomationParameter> (*d));
        return group;
    };

    std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>> topLevelGroups;

    // One top-level group per object slot ("Object 1".."Object N", 1-based
    // -- matches ParameterPanel's own "Object N" display convention),
    // subgrouped by category. AU can't nest subgroups (JUCE's own doc
    // comment on AudioProcessorParameterGroup) -- flattened automatically
    // using the " | " separator given above, no special-casing needed here.
    for (auto& objectEntry : perObject)
    {
        const int objectId = objectEntry.first;
        const juce::String groupIdPrefix = "obj" + juce::String (objectId);
        auto objectGroup = std::make_unique<juce::AudioProcessorParameterGroup> (
            groupIdPrefix, "Object " + juce::String (objectId + 1), " | ");
        for (auto& categoryEntry : objectEntry.second)
            objectGroup->addChild (makeCategoryGroup (groupIdPrefix, categoryEntry.first, categoryEntry.second));
        topLevelGroups.push_back (std::move (objectGroup));
    }

    // One "Global" top-level group for every Scope::Global parameter.
    if (! global.empty())
    {
        auto globalGroup = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", " | ");
        for (auto& categoryEntry : global)
            globalGroup->addChild (makeCategoryGroup ("global", categoryEntry.first, categoryEntry.second));
        topLevelGroups.push_back (std::move (globalGroup));
    }

    return topLevelGroups;
}
