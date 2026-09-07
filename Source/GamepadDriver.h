#pragma once
#include <functional>
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
    only when this driver interprets a control for its own built-in
    behaviors, never to what gets dispatched to the hub; a future mapping
    consumer binding a raw axis/button to some other parameter shouldn't be
    forced through behavior-specific shaping.

    ALSO implements the project's fixed default gamepad control scheme --
    genuinely overridable via MappingEngine (see setLeftStickOverrideQuery()
    below), not just described as such, but sensible out of the box with no
    mapping profile required:
      - Left stick: rate-controls the selected object's position on the
        ground plane (driveSelectedObjectMovement(), unchanged from before
        this class grew the rest of this scheme).
      - D-pad Up/Down (edge-triggered): cycle the selection to the next/
        previous ACTIVE object, wrapping (bidirectional -- was Button X
        only, forward-only, before; moved here for a more natural "navigate
        a list" feel and freed Button X for a future/MappingEngine
        binding). D-pad Left/Right independently drive camera zoom, polled
        separately by KlangorbitEditor (see below) -- the two axes don't
        interact. Button A: activate the next inactive object slot (mirrors
        PluginEditor::addObjectClicked()) and select it. Button B:
        deactivate the currently selected object (mirrors
        removeObjectClicked()) and clear the selection. See
        driveObjectManagement().
      - Button Y / Left Shoulder / Right Shoulder (HELD): Free Throw /
        Orbit Shot / Slingshot, the same three launch modes
        PluginEditor's mouse-driven sling gesture offers (see
        SlingGesture.h) -- while held, the left stick's direction and
        magnitude choose the launch direction/strength (a direct analog
        aim, not the mouse gesture's pull-BACK-then-release metaphor,
        which has no natural equivalent without a screen cursor to pull
        away from -- see driveThrowGesture()'s own comment); releasing the
        button fires it. Suspends driveSelectedObjectMovement() for that
        object while held (same "don't fight over the same stick" handling
        setLeftStickOverrideQuery() already uses).
    None of the above touches Camera3D or requires an editor window --
    Camera3D is editor-only view state (see Camera3D's own class comment),
    so the right stick (camera look) and D-pad Left/Right (camera zoom) are
    instead polled directly by KlangorbitEditor's own timer via
    getLastState() below, entirely separately from this class -- see
    PluginEditor.cpp's updateGamepadCamera().

    Movement/throw-gesture state itself stays special-cased here rather
    than being expressed as ordinary MappingBindings, because both need
    behavior (Manual-mode switching, manualVelocityActive ownership,
    deadzone/curve shaping, held-button aim-then-release sequencing) that
    doesn't fit "write one normalized value into one registered parameter."

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
    // selectedObjectIndex: the processor's own source of truth (see
    // KlangorbitProcessor::getSelectedObjectIndex()), passed BY REFERENCE
    // so this driver's own built-in object-management behavior (cycle/add/
    // remove, see driveObjectManagement()) can update it directly, exactly
    // as if the change had come from the editor's own selectObject() --
    // -1 = nothing selected, same "inert while nothing selected"
    // convention ParameterRegistry::Scope::SelectedObject already
    // documents. The caller (KlangorbitProcessor::timerCallback()) is
    // responsible for keeping any of ITS OWN other copies of this value in
    // sync afterward -- there are none on the processor side, but
    // KlangorbitEditor keeps a display copy for rendering and must poll
    // getSelectedObjectIndex() each tick to notice a change this driver
    // made in the background (see PluginEditor.cpp's timerCallback()).
    void poll (CanonicalInputHub& hub, int& selectedObjectIndex, double dt);

    bool isConnected() const { return lastState.connected; }

    // Raw, unshaped snapshot from the most recent poll() -- lets
    // KlangorbitEditor read the right stick/D-pad directly for camera
    // control (see the class comment) without this driver needing to know
    // anything about Camera3D. Continuous polling (not the hub's
    // change-only dispatch) is deliberate here: a camera that only turns
    // WHILE the stick's value is changing would stop mid-turn the instant
    // the stick holds steady at a nonzero deflection, which isn't how a
    // look-around control should feel.
    const GamepadState& getLastState() const { return lastState; }

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

    // Lets a MappingEngine (or anything else) claim the left stick away
    // from this driver's own built-in movement behavior -- called with a
    // predicate that answers "is this canonical sourceId currently
    // explicitly bound (in whichever bank is active right now)?". If
    // either "Gamepad0.LeftStick.X" or "Gamepad0.LeftStick.Y" comes back
    // true, driveSelectedObjectMovement() does nothing that tick (see its
    // own comment on why NOT partially -- an axis rebound alone would
    // otherwise leave movement half-working). nullptr (the default)
    // means nothing is ever overridden -- built-in movement always
    // applies, i.e. this driver's original, MappingEngine-free behavior.
    void setLeftStickOverrideQuery (std::function<bool (const juce::String&)> query) { isSourceOverridden = std::move (query); }

    // Meters of launch strength at full left-stick deflection while a
    // throw-gesture button is held (see driveThrowGesture()) -- same order
    // of magnitude as the mouse sling gesture's own pull distances
    // (SlingGesture::minPullDistanceMeters etc.), tuned so a full-deflection
    // Free Throw (SlingGesture::throwVelocityScale * this) lands close to
    // SoundObject::maxVelocity's own default (6.0) rather than needing to
    // be clamped away immediately.
    void setThrowMaxPullDistance (float newMaxPullDistanceMeters) { throwMaxPullDistanceMeters = juce::jmax (0.0f, newMaxPullDistanceMeters); }
    float getThrowMaxPullDistance() const { return throwMaxPullDistanceMeters; }

    // --- Object-management/throw-gesture logic (see the class comment's
    // corresponding bullets) -- public specifically so a test can exercise
    // this logic directly against hand-built GamepadState previous/current
    // pairs and a real TrajectoryEngine, without needing an actual
    // connected controller (GamepadBridge -- the part of this class that
    // genuinely can't be exercised headlessly -- is never touched by
    // either method below; see Tools/verify_gamepad_driver.cpp). poll()
    // itself is still the only normal caller in the running plugin.

    // See the class comment's "Button X/A/B" bullet. Edge-triggered off
    // `previous`/`current` (the same two snapshots dispatchAllChanges()
    // already compares) so each physical press fires exactly once,
    // regardless of how many ticks the button stays held.
    void driveObjectManagement (int& selectedObjectIndex, const GamepadState& previous, const GamepadState& current);

    // See the class comment's "Button Y / Left Shoulder / Right Shoulder"
    // bullet -- Free Throw / Orbit Shot / Slingshot, gamepad-driven.
    void driveThrowGesture (int selectedObjectIndex, const GamepadState& previous, const GamepadState& current);

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

    // See setLeftStickOverrideQuery() above.
    std::function<bool (const juce::String&)> isSourceOverridden;

    // --- Throw-gesture state (driveThrowGesture()) --------------------------
    enum class ThrowMode { None, FreeThrow, OrbitShot, Slingshot };
    ThrowMode activeThrowMode = ThrowMode::None;
    // Snapshotted when a throw-hold button is first pressed, so a
    // selection change made mid-hold (e.g. via driveObjectManagement()'s
    // own cycle button) can't retarget an aim already in progress -- the
    // object being aimed is whichever one was selected at press time,
    // exactly like the mouse gesture's own slingObjectIndex (see
    // PluginEditor::startSling()).
    int throwObjectIndex = -1;
    float throwMaxPullDistanceMeters = 2.0f;
};
