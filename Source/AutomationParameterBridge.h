#pragma once
// Headless module specifically (not the full juce_audio_processors) --
// RangedAudioParameter/AudioProcessorParameterGroup/NormalisableRange all
// live here, no GUI/editor dependency needed. Lets
// Tools/verify_automation_parameters.cpp link only this lighter module,
// same reasoning as its own CMakeLists.txt comment; the full plugin build
// (which also links juce_audio_processors proper, a superset) resolves
// this identically either way.
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <memory>
#include <vector>
#include "ParameterRegistry.h"

/**
    Bridges one ParameterRegistry::Descriptor to a real, host-automatable
    juce::AudioProcessorParameter -- see PluginProcessor::
    buildAutomationParameters() for how these get constructed/grouped, and
    ParameterRegistry.h's own class comment for why the registry itself was
    deliberately NOT built on juce::AudioProcessorParameter (a different,
    controller-mapping-only consumer -- this class is the bridge that makes
    the SAME descriptors ALSO usable for DAW automation, without the
    registry itself taking on that dependency).

    Delegates entirely to the descriptor's own getValue()/setValue()
    closures -- no separate storage here, so host automation and this
    project's existing GUI/gamepad/MIDI/OSC control paths all read and
    write the exact same underlying SoundObject/GrainCloudSettings/
    SceneSettings field. A host may call setValue() from the audio thread
    during automation playback -- the first time these fields would be
    written from that specific thread, though they're already written
    unsynchronized from the message thread (GUI) and the gamepad/MIDI/OSC
    control paths today, per ParameterRegistry's own "no abstraction
    layer at all" design (see its class comment). A torn read on a
    multi-field Vec3 (three separate parameters for x/y/z) is a benign,
    momentary glitch, not a crash -- the same concurrency model this
    codebase already accepts everywhere else for this class of data, not
    a new category of risk. No new synchronization is added here.

    Deliberately does NOT reuse Descriptor::normalize()/denormalize()
    (which produce [-1,1] for Polarity::Bipolar, meaningful to a
    controller-mapping consumer) -- a DAW automation lane has no use for
    that concept, so this uses a plain linear [minValue, maxValue] ->
    [0,1] juce::NormalisableRange instead, the standard JUCE convention
    every host already expects.
*/
class RegistryAutomationParameter : public juce::RangedAudioParameter
{
public:
    // descriptor must be a STABLE reference into a ParameterRegistry that
    // has already finished registration -- see
    // buildAutomationParameterGroups()'s own comment below.
    explicit RegistryAutomationParameter (const ParameterRegistry::Descriptor& descriptorToWrap);

    float getValue() const override;
    void setValue (float newValue) override;
    float getDefaultValue() const override;
    // Plain numeric text <-> normalized value -- no per-field unit/format
    // string stored on Descriptor to do anything fancier with (see
    // ParameterRegistry.h), same default numeric formatting every other
    // JUCE ranged parameter falls back to without a custom string
    // converter.
    float getValueForText (const juce::String& text) const override;
    const juce::NormalisableRange<float>& getNormalisableRange() const override;

private:
    const ParameterRegistry::Descriptor& descriptor;
    juce::NormalisableRange<float> range;
    float defaultValueNormalized;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegistryAutomationParameter)
};

/**
    Builds one top-level juce::AudioProcessorParameterGroup per distinct
    Scope::SpecificObject `objectId` found in `descriptors` (named
    "Object <id+1>", 1-based, matching ParameterPanel's own display
    convention), each subgrouped by Descriptor::category (reused as-is --
    already exactly the right taxonomy, e.g. "Object Physics"/"Attraction"/
    "Orbit"/"Doppler"/"Grain Cloud"), plus one more top-level "Global" group
    (subgrouped the same way) for every Scope::Global descriptor. Every leaf
    is a RegistryAutomationParameter wrapping that exact Descriptor.

    Scope::SelectedObject descriptors are deliberately skipped entirely --
    NOT bridged to any parameter -- same reasoning ParameterRegistry.h's own
    class comment already gives for why that scope is a poor fit for
    juce::AudioProcessorParameter (identity-stable parameters vs. a scope
    that retargets which object it resolves to from one call to the next).

    Pure/standalone -- no KlangorbitProcessor dependency, so this is
    independently testable (see Tools/verify_automation_parameters.cpp)
    without constructing the full processor. `descriptors` must outlive
    every RegistryAutomationParameter this produces (in practice: the whole
    ParameterRegistry that owns them, which must have already finished
    registration -- its own internal vector must never grow again after
    this call, or these references become dangling on the next reallocation).

    Caller is expected to hand each returned group to
    juce::AudioProcessor::addParameterGroup() -- see
    KlangorbitProcessor::buildAutomationParameters().
*/
std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>>
    buildAutomationParameterGroups (const std::vector<ParameterRegistry::Descriptor>& descriptors);
