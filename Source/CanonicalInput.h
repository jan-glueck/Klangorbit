#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "ParameterRegistry.h"

/**
    Protocol-neutral representation of "one control changed" -- a
    gamepad stick axis, a trigger, a button, and later (not part of this
    branch) a MIDI CC or an OSC message, whatever the actual hardware or
    protocol underneath it is. Everything downstream of this layer (a
    Learn-mode mapping engine, in a later branch) works ONLY against this
    type; it has no knowledge of gamepads, MIDI, or OSC at all, and never
    needs to change when a new driver is added.

    A driver (GamepadDriver, a later branch, is the first one) translates
    its own hardware/protocol-specific state into this representation and
    posts it to a CanonicalInputHub -- this header and CanonicalInputHub
    itself are the stable contract a future MIDI/OSC driver would target;
    adding one means writing a new, thin driver class that produces the
    SAME CanonicalInputEvent shape, not touching this file.
*/
struct CanonicalInputEvent
{
    enum class Kind
    {
        Continuous, // a smoothly varying analog control -- stick axis, analog trigger pressure
        Button      // a discrete digital control -- button, D-pad direction; value is 0.0 (released) or 1.0 (pressed)
    };

    // Stable, human-readable, hierarchical identifier for exactly which
    // physical control this is -- e.g. "Gamepad0.LeftStick.X",
    // "Gamepad0.ButtonA", later "Midi0.CC1.Ch1", "OSC./orbit/x". Used for
    // Learn-mode ("which input did the user just move?") and any future
    // display/logging. Opaque to this layer -- never parsed/interpreted
    // here, purely a driver-chosen label.
    juce::String sourceId;

    Kind kind = Kind::Continuous;

    // Normalized value: 0.0..1.0 for Polarity::Unipolar (a trigger, most
    // buttons), -1.0..1.0 for Polarity::Bipolar (a stick axis centered at
    // rest). Reuses ParameterRegistry::Polarity rather than duplicating
    // the same two-valued concept under a different name -- a mapping
    // consumer needs both a canonical value AND a target
    // ParameterRegistry::Descriptor's own polarity to convert correctly
    // via Descriptor::denormalize(), so keeping one shared enum avoids an
    // extra translation step between the two.
    float value = 0.0f;
    ParameterRegistry::Polarity polarity = ParameterRegistry::Polarity::Unipolar;
};

/**
    A single, protocol-neutral dispatch point every driver posts
    CanonicalInputEvents to, and every consumer (a Learn-mode mapping
    engine, later) listens on. Not tied to any one driver or consumer --
    multiple drivers can post to the same hub (a gamepad driver and,
    later, a MIDI driver, simultaneously), and multiple listeners can
    observe the same stream (a Learn-mode UI watching for the next input
    alongside an active mapping engine applying existing bindings)
    without knowing about each other.

    Deliberately a plain, explicitly-locked listener vector rather than
    juce::ListenerList -- this needs to be safely callable (dispatch())
    from whichever thread a driver happens to poll/receive on, which
    juce::ListenerList doesn't specifically guarantee, and the actual
    listener set here is expected to stay small (a handful of consumers
    at most), so the simplicity of an explicit lock + snapshot-then-call
    outweighs juce::ListenerList's extra machinery.
*/
class CanonicalInputHub
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void canonicalInputReceived (const CanonicalInputEvent& event) = 0;
    };

    // Not owned -- caller keeps the Listener alive for as long as it's
    // registered, and must call removeListener() before the listener is
    // destroyed.
    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    // Called by a driver whenever a physical control's canonical value
    // changes. Thread-safe -- may be called from any thread a driver
    // happens to poll/receive on. Listener notification runs
    // synchronously on the CALLING thread (no internal queuing/hop to
    // another thread); a listener that needs to run specifically on the
    // message thread is responsible for its own hop (e.g. via
    // juce::MessageManager::callAsync()), same contract as any other
    // JUCE cross-thread broadcaster. Listeners are snapshotted under lock
    // before being called, so a listener add/remove from inside a
    // callback can never deadlock -- though a listener removed mid-
    // dispatch may still receive that one already-in-flight event.
    void dispatch (const CanonicalInputEvent& event);

private:
    juce::CriticalSection lock;
    std::vector<Listener*> listeners;
};
