#include "OscDriver.h"

bool OscInterpretation::interpretMessage (const juce::OSCMessage& message, CanonicalInputEvent& outEvent)
{
    const auto sourceId = "OSC." + message.getAddressPattern().toString();

    if (message.size() == 0)
    {
        outEvent = { sourceId, CanonicalInputEvent::Kind::Button, 1.0f, ParameterRegistry::Polarity::Unipolar };
        return true;
    }

    const auto& arg = message[0];
    float value = 0.0f;
    if (arg.isFloat32())
        value = arg.getFloat32();
    else if (arg.isInt32())
        value = (float) arg.getInt32();
    else
        return false; // non-numeric first argument -- see the header's own comment

    outEvent = { sourceId, CanonicalInputEvent::Kind::Continuous, juce::jlimit (0.0f, 1.0f, value), ParameterRegistry::Polarity::Unipolar };
    return true;
}

OscDriver::OscDriver (CanonicalInputHub& hubToDispatchTo) : hub (hubToDispatchTo)
{
    receiver.addListener (this);
    setPort (port); // best-effort auto-connect on the default port at startup, same "silently fine either way" reasoning as the gamepad/mapping-profile startup paths
}

OscDriver::~OscDriver()
{
    receiver.removeListener (this);
}

bool OscDriver::setPort (int newPort)
{
    receiver.disconnect();
    port = newPort;
    connected = receiver.connect (port);
    return connected;
}

void OscDriver::oscMessageReceived (const juce::OSCMessage& message)
{
    if (messageInterceptor && messageInterceptor (message))
        return;

    CanonicalInputEvent event;
    if (OscInterpretation::interpretMessage (message, event))
        hub.dispatch (event);
}
