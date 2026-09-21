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
        else if (name == "grab")
        {
            e.kind = GestureEvent::Kind::Grab;
            e.a = juce::jlimit (-1.0f, 1.0f, a);
            e.b = juce::jlimit (-1.0f, 1.0f, b);
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
    const juce::String address = message.getAddressPattern().toString();

    if (! GestureInterpretation::isGestureAddress (address))
    {
        // Observed only -- returning false lets these continue on as
        // ordinary controller values (the visible flags/heartbeat are
        // Learn-mappable like any OSC control).
        if (address == "/klangorbit/mediapipe/status")
        {
            lastStatusMs = juce::Time::getMillisecondCounter();
            statusEverSeen = true;
        }
        else if (address.startsWith ("/klangorbit/hand/") && address.endsWith ("/visible") && message.size() > 0)
        {
            const int slot = address.fromFirstOccurrenceOf ("/klangorbit/hand/", false, false).getIntValue();
            const auto& arg = message[0];
            const float v = arg.isFloat32() ? arg.getFloat32() : (arg.isInt32() ? (float) arg.getInt32() : 0.0f);
            if (juce::isPositiveAndBelow (slot, 2))
                handVisible[slot] = v >= 0.5f;
        }
        return false;
    }

    GestureEvent event;
    if (GestureInterpretation::parse (message, event))
        execute (event);
    return true;
}

GestureDriver::TrackingStatus GestureDriver::getTrackingStatus() const
{
    TrackingStatus s;
    s.bridgeActive = statusEverSeen && (int) (juce::Time::getMillisecondCounter() - lastStatusMs) <= statusTimeoutMs;
    if (s.bridgeActive)
        s.handsVisible = (handVisible[0] ? 1 : 0) + (handVisible[1] ? 1 : 0);
    return s;
}

int GestureDriver::objectForHand (int handSlot) const
{
    const int own = handObject[juce::jlimit (0, 1, handSlot)];
    if (juce::isPositiveAndBelow (own, engine.getNumObjects()) && engine.getObject (own).inputChannel >= 0)
        return own;
    return selectedObject ? selectedObject() : -1;
}

void GestureDriver::execute (const GestureEvent& event)
{
    if (paused)
        return;

    if (event.kind == GestureEvent::Kind::Grab)
    {
        const float room = engine.getSceneSettings().roomSize > 0.0f ? engine.getSceneSettings().roomSize : 5.0f;
        const Vec3 hand = GestureActions::aimToPullVector (event.a, event.b, room);
        int best = -1;
        float bestDist = grabPickRadiusMeters;
        for (int i = 0; i < engine.getNumObjects(); ++i)
        {
            if (engine.getObject (i).inputChannel < 0)
                continue;
            const Vec3 d = engine.getObject (i).position - hand;
            const float dist = Vec3 { d.x, d.y, 0.0f }.length(); // top-down map: ignore height
            if (dist <= bestDist)
            {
                bestDist = dist;
                best = i;
            }
        }
        if (best >= 0)
        {
            handObject[juce::jlimit (0, 1, event.handSlot)] = best;
            if (onObjectSelected)
                onObjectSelected (best);
        }
        return;
    }

    const int index = objectForHand (event.handSlot);
    if (! juce::isPositiveAndBelow (index, engine.getNumObjects()) || engine.getObject (index).inputChannel < 0)
        return; // nothing selected/active -- nothing to launch, same gate as the gamepad's throw

    switch (event.kind)
    {
        case GestureEvent::Kind::Grab:
            break; // handled above

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
