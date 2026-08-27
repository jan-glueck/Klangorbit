#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <unordered_map>
#include <vector>

/**
    Central register of every controllable parameter in the plugin.
    SoundObject/GrainCloudSettings/SceneSettings fields register themselves
    here (see PluginProcessor::buildParameterRegistry()) instead of a fixed
    list living in mapping-specific code -- so a controller/MIDI/OSC mapping
    layer can enumerate and bind against them generically, without this
    registry or the fields themselves knowing anything about gamepads,
    MIDI, or OSC.

    Deliberately NOT built on juce::AudioProcessorParameter -- see the
    CHANGELOG entry for this class for the full reasoning. Short version:
    every parameter in this codebase today is a plain public struct field
    (SoundObject, GrainCloudSettings, SceneSettings), read/written directly
    by both the GUI and the audio thread with no abstraction layer at all.
    juce::AudioProcessorParameter is designed for host-automatable,
    identity-STABLE parameters declared once at construction -- a poor fit
    for up to 8 objects worth of fields (most inactive at any given time)
    and, more fundamentally, for Scope::SelectedObject below, which by
    design retargets which underlying field a single registered parameter
    resolves to from one call to the next. This registry deliberately has
    no host-automation ambition; it exists purely for internal
    controller-mapping use.

    Each Descriptor is a self-contained entry: stable ID, display
    name/category (for a future mapping-UI parameter picker), REAL (not
    normalized) value range + polarity, get/set access in that real range,
    and a binding Scope. normalize()/denormalize() convert to/from a 0..1
    (unipolar) or -1..1 (bipolar) range for a mapping consumer -- mirroring
    the normalized/plain value split juce::AudioProcessorParameter itself
    uses internally, without adopting that class's much heavier
    host-automation machinery.

    Registration is a one-shot, complete pass (see
    PluginProcessor::buildParameterRegistry(), called once from the
    constructor) covering EVERY possible object/grain-cloud slot, active or
    not -- mirroring TrajectoryEngine's own fixed-size object pool (see its
    class comment: activation is just a flag, not allocation) and
    ParameterPanel's own existing pattern of building all its rows once
    regardless of activation state. There is deliberately no
    register/unregister churn tied to object activation.
*/
class ParameterRegistry
{
public:
    enum class Polarity { Unipolar, Bipolar };

    // Global: always resolves to one fixed value, independent of any
    //   object selection (e.g. SceneSettings::roomSize).
    // SpecificObject: bound to one fixed object slot's field, by id, for
    //   the parameter's whole lifetime (e.g. "Object 3's Mass" as its own
    //   distinct, always-addressable parameter regardless of what's
    //   currently selected in the UI).
    // SelectedObject: resolves against WHICHEVER object is currently
    //   selected (see PluginProcessor::getSelectedObjectIndex()) -- the
    //   SAME underlying fields as the matching SpecificObject entries,
    //   but the concrete object actually targeted can change between
    //   calls. Reads/writes silently become inert (getValue returns 0,
    //   setValue is a no-op) while nothing is selected, rather than
    //   asserting/crashing -- a controller mapped to a SelectedObject
    //   parameter is expected to routinely go idle when the user clears
    //   the selection, not be treated as an error state.
    enum class Scope { Global, SpecificObject, SelectedObject };

    struct Descriptor
    {
        juce::String id;           // stable, unique -- e.g. "object.3.mass", "selectedObject.mass", "scene.roomSize"
        juce::String displayName;  // e.g. "Mass"
        juce::String category;     // e.g. "Object Physics", "Attraction", "Orbit", "Grain Cloud", "Global"
        float minValue = 0.0f;
        float maxValue = 1.0f;
        Polarity polarity = Polarity::Unipolar;
        Scope scope = Scope::Global;
        int objectId = -1;         // only meaningful for Scope::SpecificObject -- which slot

        std::function<float()> getValue;
        std::function<void (float)> setValue;

        // realValue -> normalized (unipolar 0..1, bipolar -1..1), via
        // minValue/maxValue. Clamped to the descriptor's own range first,
        // so an out-of-range real value (shouldn't normally happen, but
        // isn't asserted against) still produces an in-range normalized
        // result rather than an out-of-bounds one.
        float normalize (float realValue) const;
        // Inverse of normalize() -- an out-of-range normalized input is
        // clamped to [0,1]/[-1,1] first, so this can never produce a
        // realValue outside [minValue, maxValue].
        float denormalize (float normalizedValue) const;
    };

    // Registers one parameter. Asserts (debug builds only) if id is
    // already taken -- catches a copy-paste ID collision immediately
    // rather than silently shadowing an earlier registration; the later
    // registration still wins in release builds (last-write, same as
    // std::unordered_map's own insert-or-assign semantics), not a crash.
    void registerParameter (Descriptor descriptor);

    // nullptr if no parameter with this id is registered.
    const Descriptor* find (const juce::String& id) const;

    // Registration order (stable) -- for a future mapping-UI parameter
    // picker grouped by category, or for iterating/logging all of them
    // (e.g. the Learn-mode UI in a later branch).
    const std::vector<Descriptor>& all() const { return parameters; }

private:
    std::vector<Descriptor> parameters;
    std::unordered_map<juce::String, size_t> idToIndex;
};
