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
    enum class Kind { Throw, Slingshot, Orbit, Grab };

    Kind kind = Kind::Throw;
    int handSlot = 0;   // 0 = left hand, 1 = right hand
    float a = 0.0f;     // Grab: frameX ; Throw/Slingshot: aimX ; Orbit: radius01 (0..1)
    float b = 0.0f;     // Grab: frameY ; Throw/Slingshot: aimY ; Orbit: direction (+1 counter-clockwise, -1 clockwise)
};

/**
    OSC address convention (all messages: int/float slot, then two floats):
        /klangorbit/gesture/throw      slot aimX aimY
        /klangorbit/gesture/slingshot  slot aimX aimY
        /klangorbit/gesture/orbit      slot radius01 direction
        /klangorbit/gesture/grab       slot frameX frameY   (pinch just started;
                                                              frame position -1..1,
                                                              right/up positive)
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
    Executes recognized hand gestures through the same GestureActions the
    gamepad's throw buttons use -- no gesture-specific physics of its own.

    Which object a hand acts on: a "grab" (the moment a pinch starts)
    picks the active object nearest the hand's position, treating the
    camera frame as a top-down map of the room (frame centre = origin,
    right = -Y, up = +X -- the gamepad stick's convention -- scaled so the
    frame edge reaches the room boundary) and remembers it for THAT hand
    slot, so each hand can hold its own object. A grab with no object
    close enough changes nothing. A hand that hasn't grabbed anything yet
    acts on the globally selected object, like the gamepad. The most
    recent grab also becomes the globally selected object (via the
    selection callback), so the editor's highlight follows.

    Also tracks whether the bridge is alive (its /klangorbit/mediapipe/
    status heartbeat) and how many hands it currently sees, for the
    editor's tracking indicator; and can be paused, which makes every
    gesture message a no-op without touching the camera side.

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

    // Called with the object index a grab just selected (see class comment).
    void setSelectionCallback (std::function<void (int)> callback) { onObjectSelected = std::move (callback); }

    // Paused: gesture messages are still consumed (never misread as
    // controller values) but change nothing. The camera keeps running --
    // only the bridge process itself can stop that.
    void setPaused (bool shouldPause) { paused = shouldPause; }
    bool isPaused() const { return paused; }

    struct TrackingStatus
    {
        bool bridgeActive = false; // a status heartbeat arrived within the timeout
        int handsVisible = 0;
    };
    TrackingStatus getTrackingStatus() const;
    void setStatusTimeoutMs (int ms) { statusTimeoutMs = ms; }

    // Nearest-object pick radius for a grab, meters (world space).
    void setGrabPickRadius (float meters) { grabPickRadiusMeters = juce::jmax (0.0f, meters); }

    void setMaxPullDistance (float meters) { maxPullMeters = juce::jmax (0.0f, meters); }

private:
    int objectForHand (int handSlot) const;

    TrajectoryEngine& engine;
    std::function<int()> selectedObject;
    std::function<void (int)> onObjectSelected;
    float maxPullMeters = GestureActions::defaultMaxPullMeters;
    float grabPickRadiusMeters = 1.5f;
    int handObject[2] { -1, -1 };
    bool paused = false;

    // Bridge liveness, from messages this driver only OBSERVES (they still
    // flow on to the normal controller interpretation/Learn mode).
    juce::uint32 lastStatusMs = 0;
    bool statusEverSeen = false;
    bool handVisible[2] { false, false };
    int statusTimeoutMs = 1500;
};
