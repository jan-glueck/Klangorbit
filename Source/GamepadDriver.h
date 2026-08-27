#pragma once
#include "CanonicalInput.h"
#include "GamepadBridge.h"
#include "TrajectoryEngine.h"
#include "AxisShaping.h"

/**
    First concrete driver on top of the canonical input layer (see
    CanonicalInput.h): polls a connected gamepad via GamepadBridge (Apple's
    GameController framework) and dispatches a raw, UNshaped
    CanonicalInputEvent for every axis/button that changed since the last
    poll() call -- deadzone/response-curve shaping (see below) is applied
    only when this driver interprets the stick for its own built-in
    movement behavior, never to what gets dispatched to the hub; a future
    mapping consumer binding the raw stick to some other parameter
    shouldn't be forced through movement-specific shaping.

    ALSO implements the actual default gamepad movement behavior directly
    (there is no generic mapping/Learn-mode system yet -- that's a later
    branch): the left stick rate-controls the currently selected object's
    position on the ground plane, exactly like TrajectoryEngine::dragTo()
    does for a mouse drag, just continuously (see Mode below). This is
    deliberately the exact behavior a later Learn-mode system's own
    "sensible default mapping" is meant to reproduce -- when that system
    exists, it can keep calling this same method as its literal default
    binding, or reimplement it as an ordinary mapping entry; either way
    nothing about the canonical dispatch above needs to change.

    poll() is meant to be called from KlangorbitProcessor's own control-
    rate timer (~90Hz, the same one driving TrajectoryEngine::update()) --
    per the project's own requirement that gamepad polling live in the
    AudioProcessor, not the editor, so control keeps working with no
    editor window open.
*/
class GamepadDriver
{
public:
    explicit GamepadDriver (TrajectoryEngine& engineToControl);

    // dt: same control-rate delta KlangorbitProcessor::timerCallback()
    // already computes for trajectoryEngine.update() -- reused here
    // rather than tracked separately, so both stay in lockstep.
    // selectedObjectIndex: -1 = nothing selected -- still polls/dispatches
    // canonical events normally, just applies no movement this tick (see
    // ParameterRegistry::Scope::SelectedObject's own "inert while nothing
    // selected" precedent).
    void poll (CanonicalInputHub& hub, int selectedObjectIndex, double dt);

    bool isConnected() const { return lastState.connected; }

    // --- Movement-control tuning -------------------------------------------
    // Deadzone: fraction of full deflection near center that's treated as
    // exactly zero, to reject stick rest-noise/drift. ~5-10% is the
    // conventional range; default sits in the middle of that.
    void setDeadzone (float newDeadzone) { deadzone = juce::jlimit (0.0f, 0.95f, newDeadzone); }
    float getDeadzone() const { return deadzone; }

    // Exponential response curve applied AFTER the deadzone rescale:
    // shaped = sign(x) * |x|^exponent. 1.0 = linear; higher values give
    // finer control near center and reserve full speed for a more
    // deliberate, near-full stick deflection.
    void setCurveExponent (float newExponent) { curveExponent = juce::jmax (0.1f, newExponent); }
    float getCurveExponent() const { return curveExponent; }

    // m/s at full stick deflection -- same order of magnitude as
    // SoundObject::maxVelocity's own default (6.0).
    void setMaxSpeed (float newMaxSpeed) { maxSpeed = juce::jmax (0.0f, newMaxSpeed); }
    float getMaxSpeed() const { return maxSpeed; }

    // Off by default -- rate control (stick position -> exact velocity,
    // no residual motion once centered) is the standard behavior; this is
    // a separate, opt-in alternative where movement continues after the
    // stick is released and decelerates under normal Impulse-mode physics
    // (damping/dragCoefficient), reusing that mode's existing physics
    // rather than a separate scripted inertia model. See
    // driveSelectedObjectMovement()'s own comment.
    void setInertiaModeEnabled (bool shouldBeEnabled);
    bool isInertiaModeEnabled() const { return inertiaModeEnabled; }
    void setInertiaAcceleration (float newAccel) { inertiaAcceleration = juce::jmax (0.0f, newAccel); }
    float getInertiaAcceleration() const { return inertiaAcceleration; }

private:
    void dispatchAxisIfChanged (CanonicalInputHub& hub, const juce::String& sourceId, float previous, float current,
                                 ParameterRegistry::Polarity polarity);
    void dispatchButtonIfChanged (CanonicalInputHub& hub, const juce::String& sourceId, bool previous, bool current);
    void dispatchAllChanges (CanonicalInputHub& hub, const GamepadState& previous, const GamepadState& current);

    void driveSelectedObjectMovement (int selectedObjectIndex, double dt);

    TrajectoryEngine& engine;
    GamepadBridge bridge;
    GamepadState lastState; // previous poll's state -- for change-detection/dispatch

    float deadzone = 0.08f;
    float curveExponent = 2.0f;
    float maxSpeed = 4.0f;

    bool inertiaModeEnabled = false;
    float inertiaAcceleration = 8.0f; // m/s^2, applied to Impulse-mode velocity while the stick is held past the deadzone

    // Which object slot rate-control is currently "holding," so a
    // selection change (or an inertia-mode toggle) can cleanly release
    // the PREVIOUS object (stop it exactly where it is / hand it back to
    // ordinary physics) instead of leaving it drifting forever at a stale
    // velocity nobody is updating anymore. -1 = not currently holding anything.
    int lastControlledObjectIndex = -1;
    void releaseControlledObject(); // clears manualVelocityActive/manualVelocity on lastControlledObjectIndex, if any
};
