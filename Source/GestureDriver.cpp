#include "GestureDriver.h"
#include <cmath>

namespace GestureInterpretation
{
    bool isGestureAddress (const juce::String& address)
    {
        return address.startsWith (addressPrefix);
    }

    static bool numericArg (const juce::OSCMessage& m, int index, float& out)
    {
        if (index >= m.size())
            return false;
        const auto& arg = m[index];
        if (arg.isFloat32()) { out = arg.getFloat32(); return std::isfinite (out); }
        if (arg.isInt32())   { out = (float) arg.getInt32(); return true; }
        return false;
    }

    bool parse (const juce::OSCMessage& message, GestureEvent& outEvent)
    {
        const juce::String address = message.getAddressPattern().toString();
        if (! isGestureAddress (address))
            return false;

        const juce::String name = address.fromFirstOccurrenceOf (addressPrefix, false, false);
        float slot = 0.0f, a = 0.0f, b = 0.0f;
        if (! numericArg (message, 0, slot) || ! numericArg (message, 1, a) || ! numericArg (message, 2, b))
            return false;

        GestureEvent e;
        e.handSlot = juce::jlimit (0, 1, (int) slot);

        if (name == "throw" || name == "slingshot")
        {
            e.kind = (name == "throw") ? GestureEvent::Kind::Throw : GestureEvent::Kind::Slingshot;
            const float len = std::hypot (a, b);
            e.a = (len > 1.0f) ? a / len : a;
            e.b = (len > 1.0f) ? b / len : b;
        }
        else if (name == "orbit")
        {
            e.kind = GestureEvent::Kind::Orbit;
            e.a = juce::jlimit (0.0f, 1.0f, a);
            e.b = (b >= 0.0f) ? 1.0f : -1.0f;
        }
        else
        {
            return false;
        }

        outEvent = e;
        return true;
    }
}

GestureDriver::GestureDriver (TrajectoryEngine& engineToControl, std::function<int()> selectedObjectQuery)
    : engine (engineToControl), selectedObject (std::move (selectedObjectQuery)) {}

bool GestureDriver::handleMessage (const juce::OSCMessage& message)
{
    if (! GestureInterpretation::isGestureAddress (message.getAddressPattern().toString()))
        return false;

    GestureEvent event;
    if (GestureInterpretation::parse (message, event))
        execute (event);
    return true;
}

void GestureDriver::execute (const GestureEvent& event)
{
    const int index = selectedObject ? selectedObject() : -1;
    if (! juce::isPositiveAndBelow (index, engine.getNumObjects()) || engine.getObject (index).inputChannel < 0)
        return; // nothing selected/active -- nothing to launch, same gate as the gamepad's throw

    switch (event.kind)
    {
        case GestureEvent::Kind::Throw:
        case GestureEvent::Kind::Slingshot:
        {
            const Vec3 pull = GestureActions::aimToPullVector (event.a, event.b, maxPullMeters);
            if (pull.length() < SlingGesture::minPullDistanceMeters)
                return; // same accidental-tap gate as every other launch source
            GestureActions::fire (engine,
                                  event.kind == GestureEvent::Kind::Throw ? GestureActions::LaunchMode::FreeThrow
                                                                          : GestureActions::LaunchMode::Slingshot,
                                  index, pull);
            break;
        }

        case GestureEvent::Kind::Orbit:
            GestureActions::fireOrbit (engine, index, event.a * maxPullMeters, event.b);
            break;
    }
}
