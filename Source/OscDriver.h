#pragma once
#include <juce_osc/juce_osc.h>
#include "CanonicalInput.h"

/**
    Converts one already-received OSCMessage into a CanonicalInputEvent,
    or returns false if the message doesn't produce one (non-numeric
    first argument). Pure, side-effect-free -- factored out from
    OscDriver specifically so this interpretation logic is unit-testable
    without a real network socket or a JUCE message-loop pump (see
    OscDriver's own class comment for why its actual message delivery
    can't easily be exercised the same way).

    Convention (a practical, documented choice -- OSC itself doesn't
    standardize value ranges the way MIDI or a GameController profile
    does):
    - sourceId is "OSC." + the message's own address pattern verbatim
      (e.g. address "/orbit/x" -> sourceId "OSC./orbit/x") -- matches the
      example already used in MappingProfiles/schema/README.md.
    - A message with a float or int32 first argument produces a
      Continuous event, that argument's value clamped to [0,1]
      (Unipolar) -- the overwhelmingly common convention for OSC
      control-surface apps (TouchOSC, Lemur, etc.) is to already send
      normalized 0..1 values, so the raw value IS the canonical value
      directly, not rescaled from some other assumed range.
    - A message with NO arguments produces a Button event (value 1.0) --
      the common "bare trigger" convention some OSC senders use for a
      button press. OSC has no native "release" concept, so only a press
      is ever produced this way.
    - A message whose first argument is present but not numeric (string,
      blob, etc.) produces nothing (returns false).
*/
namespace OscInterpretation
{
    bool interpretMessage (const juce::OSCMessage& message, CanonicalInputEvent& outEvent);
}

/**
    Third driver on the canonical input layer (see CanonicalInput.h) --
    listens for OSC messages on a UDP port and dispatches them via
    OscInterpretation::interpretMessage() above. Confirms the canonical
    layer's own promise a second time: this class (plus the free function
    above) is the only new code needed to add an OSC source --
    ParameterRegistry, CanonicalInputHub, and MappingEngine all needed
    zero changes, exactly as they didn't for MidiDriver either.

    Threading: uses juce::OSCReceiver::Listener<MessageLoopCallback> (the
    default, deliberately not the RealtimeCallback variant) -- JUCE
    itself marshals oscMessageReceived() onto the message thread, so this
    class dispatches to CanonicalInputHub directly from that callback,
    with no queue of its own needed (unlike MidiDriver, whose MIDI
    genuinely arrives on the audio thread and needs one).
*/
class OscDriver : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    explicit OscDriver (CanonicalInputHub& hubToDispatchTo);
    ~OscDriver() override;

    // Binds the given UDP port, replacing any current connection.
    // Returns false if the port couldn't be bound (e.g. already in use)
    // -- see isConnected()/getPort() for a future UI indicator to report
    // that; no UI exists for this yet (same "no UI yet" gap as
    // GamepadDriver's own deadzone/curve/inertia tuning).
    bool setPort (int newPort);
    bool isConnected() const { return connected; }
    int getPort() const { return port; }

private:
    void oscMessageReceived (const juce::OSCMessage& message) override;

    CanonicalInputHub& hub;
    juce::OSCReceiver receiver;
    int port = 9000; // common OSC default (e.g. TouchOSC's own default receive port)
    bool connected = false;
};
