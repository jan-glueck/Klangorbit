#pragma once
#include <functional>
#include <juce_osc/juce_osc.h>
#include "TrajectoryEngine.h"
#include "GestureActions.h"

/**
    One already-recognized hand gesture, as sent by the MediaPipe bridge
    (MediaPipeBridge/gestures.py does the recognition -- this side only
    executes). Values use the same "stick-like" convention the gamepad's
    left stick has: aim right-positive / up-positive, magnitude <= 1.
*/
struct GestureEvent
{
    enum class Kind { Throw, Slingshot, Orbit };

    Kind kind = Kind::Throw;
    int handSlot = 0;   // 0 = left hand, 1 = right hand
    float a = 0.0f;     // Throw/Slingshot: aimX ; Orbit: radius01 (0..1)
    float b = 0.0f;     // Throw/Slingshot: aimY ; Orbit: direction (+1 counter-clockwise, -1 clockwise)
};

/**
    OSC address convention (all messages: int/float slot, then two floats):
        /klangorbit/gesture/throw      slot aimX aimY
        /klangorbit/gesture/slingshot  slot aimX aimY
        /klangorbit/gesture/orbit      slot radius01 direction
    Pure and side-effect free, factored out (like OscInterpretation) so the
    parsing is unit-testable without a socket.
*/
namespace GestureInterpretation
{
    constexpr const char* addressPrefix = "/klangorbit/gesture/";

    // True if the address belongs to this driver at all (even if its
    // arguments turn out to be malformed) -- such messages must never fall
    // through to OscInterpretation, which would misread the slot number as
    // a continuous control value.
    bool isGestureAddress (const juce::String& address);

    // False for a gesture address with missing/non-numeric arguments or an
    // unknown gesture name. Aim vectors longer than 1 are normalized,
    // radius clamped to 0..1, direction reduced to +/-1.
    bool parse (const juce::OSCMessage& message, GestureEvent& outEvent);
}

/**
    Executes recognized hand gestures on the currently selected object
    through the same GestureActions the gamepad's throw buttons use --
    no gesture-specific physics of its own.

    Threading: handleMessage() is called from OscDriver's message-thread
    callback, the same thread TrajectoryEngine is confined to.
*/
class GestureDriver
{
public:
    GestureDriver (TrajectoryEngine& engineToControl, std::function<int()> selectedObjectQuery);

    // Suitable as OscDriver's message interceptor: returns true if the
    // message was a gesture message (handled or rejected), false to let
    // OscDriver interpret it as an ordinary controller value.
    bool handleMessage (const juce::OSCMessage& message);

    // Public for tests.
    void execute (const GestureEvent& event);

    void setMaxPullDistance (float meters) { maxPullMeters = juce::jmax (0.0f, meters); }

private:
    TrajectoryEngine& engine;
    std::function<int()> selectedObject;
    float maxPullMeters = GestureActions::defaultMaxPullMeters;
};
