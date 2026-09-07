#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "CanonicalInput.h"
#include "ParameterRegistry.h"

/**
    One controller-input-to-parameter binding: sourceId (a
    CanonicalInputEvent::sourceId, e.g. "Gamepad0.LeftStick.X") drives
    parameterId (a ParameterRegistry::Descriptor::id, e.g.
    "selectedObject.mass") while bank is the currently active paging
    layer (see MappingEngine::getCurrentBank()).
*/
struct MappingBinding
{
    juce::String sourceId;
    juce::String parameterId;
    int bank = 0;
};

/**
    Protocol-neutral Learn-mode mapping engine -- the actual "MIDI-Learn,
    but for any canonical input source" system. Listens on a
    CanonicalInputHub (see CanonicalInput.h) and, for each event, either
    (a) captures it as a brand-new binding if Learn mode is currently
    armed, or (b) looks up any existing binding for that (sourceId,
    current bank) pair and applies it to the target ParameterRegistry
    parameter.

    Works entirely against the canonical layer -- it has no idea whether
    an event came from a gamepad, and will work identically once a MIDI
    or OSC driver exists and posts to the same hub.

    Paging/banking: a held "modifier" source (default
    "Gamepad0.LeftTrigger", see setModifierSourceId()) unlocks a SECOND
    mapping layer (bank 1) -- the same physical control can drive a
    different parameter depending on whether the modifier is currently
    held, exactly the same "a held modifier changes what a gesture means"
    principle the sling launch gesture's own Ctrl/Alt modifiers already
    established for mouse gestures (SlingGesture.h), generalized here into
    a protocol-neutral mapping mechanism rather than mouse/keyboard-
    specific code. Exactly two banks (0 = default, 1 = modified) --
    matches the project's own "a second mapping layer" framing, not an
    arbitrary N-bank system.

    Default deliberately NOT a shoulder button: GamepadDriver's own fixed
    default scheme already gives Left/Right Shoulder a built-in, always-on
    meaning (Orbit Shot/Slingshot, see its class comment) -- reusing either
    one here would double-book the same physical button for two unrelated
    behaviors out of the box. Left Trigger has no built-in behavior of its
    own (see GamepadDriver.h), so it's free; canonicalInputReceived() below
    treats any source's value>=0.5 as "held," which works identically for
    an analog trigger axis as it did for a digital shoulder button.

    Bindings persist as their own file format (see
    MappingProfileManager.h), a separate schema from Presets/schema/ --
    see that header's own comment on why.
*/
class MappingEngine : public CanonicalInputHub::Listener
{
public:
    // Not "registryToControl" -- this class never mutates the registry
    // itself (never calls registerParameter()), only invokes a target
    // Descriptor's own stored setValue()/getValue() function objects
    // (both callable through a const Descriptor*, since std::function::
    // operator() is itself const-qualified) to mutate whatever EXTERNAL
    // state those closures capture. const&, not a design compromise.
    explicit MappingEngine (const ParameterRegistry& registryToRead);

    void canonicalInputReceived (const CanonicalInputEvent& event) override;

    // --- Bindings -----------------------------------------------------------
    // Replaces any existing binding for the same (sourceId, bank) pair --
    // a single physical control drives at most one parameter per bank, so
    // re-learning a control simply retargets it rather than accumulating
    // duplicates. The same parameterId CAN be bound from multiple
    // sources/banks though (nothing here prevents that).
    void addBinding (MappingBinding binding);
    void removeBinding (const juce::String& sourceId, int bank);
    void clearAllBindings() { bindings.clear(); }
    const std::vector<MappingBinding>& getBindings() const { return bindings; }
    bool hasBindingFor (const juce::String& sourceId, int bank) const;

    // --- Learn mode -----------------------------------------------------
    // Arms Learn mode: the NEXT canonical input event received (other
    // than the modifier source itself, see canonicalInputReceived())
    // becomes a new binding to this parameter, in whichever bank is
    // active at that moment -- then Learn mode turns itself off
    // automatically. Call again with a different target to change what's
    // being learned before anything arrives; cancelLearning() aborts
    // without creating a binding.
    void startLearning (const juce::String& parameterId) { learningParameterId = parameterId; }
    void cancelLearning() { learningParameterId.clear(); }
    bool isLearning() const { return learningParameterId.isNotEmpty(); }
    juce::String getLearningParameterId() const { return learningParameterId; }

    // --- Paging / banking -------------------------------------------------
    void setModifierSourceId (const juce::String& sourceId) { modifierSourceId = sourceId; modifierHeld = false; }
    juce::String getModifierSourceId() const { return modifierSourceId; }
    // 0 = default bank, 1 = the modifier is currently held.
    int getCurrentBank() const { return modifierHeld ? 1 : 0; }

private:
    void applyBinding (const MappingBinding& binding, const CanonicalInputEvent& event);

    const ParameterRegistry& registry;
    std::vector<MappingBinding> bindings;

    juce::String modifierSourceId = "Gamepad0.LeftTrigger";
    bool modifierHeld = false;

    juce::String learningParameterId; // empty = not currently learning
};
