#include "GamepadDriver.h"
#include "SlingGesture.h"
#include "GestureActions.h"
#include <cmath>
#include <vector>

GamepadDriver::GamepadDriver (TrajectoryEngine& engineToControl) : engine (engineToControl) {}

void GamepadDriver::dispatchAxisIfChanged (CanonicalInputHub& hub, const juce::String& sourceId, float previous, float current,
                                            ParameterRegistry::Polarity polarity)
{
    constexpr float epsilon = 1.0e-4f;
    if (std::abs (current - previous) < epsilon)
        return;

    hub.dispatch ({ sourceId, CanonicalInputEvent::Kind::Continuous, current, polarity });
}

void GamepadDriver::dispatchButtonIfChanged (CanonicalInputHub& hub, const juce::String& sourceId, bool previous, bool current)
{
    if (previous == current)
        return;

    hub.dispatch ({ sourceId, CanonicalInputEvent::Kind::Button, current ? 1.0f : 0.0f, ParameterRegistry::Polarity::Unipolar });
}

void GamepadDriver::dispatchAllChanges (CanonicalInputHub& hub, const GamepadState& previous, const GamepadState& current)
{
    using Polarity = ParameterRegistry::Polarity;

    dispatchAxisIfChanged (hub, "Gamepad0.LeftStick.X", previous.leftStickX, current.leftStickX, Polarity::Bipolar);
    dispatchAxisIfChanged (hub, "Gamepad0.LeftStick.Y", previous.leftStickY, current.leftStickY, Polarity::Bipolar);
    dispatchAxisIfChanged (hub, "Gamepad0.RightStick.X", previous.rightStickX, current.rightStickX, Polarity::Bipolar);
    dispatchAxisIfChanged (hub, "Gamepad0.RightStick.Y", previous.rightStickY, current.rightStickY, Polarity::Bipolar);
    dispatchAxisIfChanged (hub, "Gamepad0.LeftTrigger", previous.leftTrigger, current.leftTrigger, Polarity::Unipolar);
    dispatchAxisIfChanged (hub, "Gamepad0.RightTrigger", previous.rightTrigger, current.rightTrigger, Polarity::Unipolar);

    dispatchButtonIfChanged (hub, "Gamepad0.ButtonA", previous.buttonA, current.buttonA);
    dispatchButtonIfChanged (hub, "Gamepad0.ButtonB", previous.buttonB, current.buttonB);
    dispatchButtonIfChanged (hub, "Gamepad0.ButtonX", previous.buttonX, current.buttonX);
    dispatchButtonIfChanged (hub, "Gamepad0.ButtonY", previous.buttonY, current.buttonY);
    dispatchButtonIfChanged (hub, "Gamepad0.LeftShoulder", previous.leftShoulder, current.leftShoulder);
    dispatchButtonIfChanged (hub, "Gamepad0.RightShoulder", previous.rightShoulder, current.rightShoulder);
    dispatchButtonIfChanged (hub, "Gamepad0.DpadUp", previous.dpadUp, current.dpadUp);
    dispatchButtonIfChanged (hub, "Gamepad0.DpadDown", previous.dpadDown, current.dpadDown);
    dispatchButtonIfChanged (hub, "Gamepad0.DpadLeft", previous.dpadLeft, current.dpadLeft);
    dispatchButtonIfChanged (hub, "Gamepad0.DpadRight", previous.dpadRight, current.dpadRight);
}

void GamepadDriver::releaseControlledObject()
{
    if (! juce::isPositiveAndBelow (lastControlledObjectIndex, engine.getNumObjects()))
        return;

    auto& obj = engine.getObject (lastControlledObjectIndex);
    if (obj.manualVelocityActive)
    {
        obj.manualVelocityActive = false;
        obj.manualVelocity = {};
    }
}

void GamepadDriver::setInertiaModeEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled == inertiaModeEnabled)
        return;

    inertiaModeEnabled = shouldBeEnabled;
    releaseControlledObject(); // don't leave the previous mode's object frozen mid-drive across the switch
}

void GamepadDriver::driveSelectedObjectMovement (int selectedObjectIndex, double dt)
{
    if (selectedObjectIndex != lastControlledObjectIndex)
    {
        releaseControlledObject();
        lastControlledObjectIndex = selectedObjectIndex;
    }

    if (! juce::isPositiveAndBelow (selectedObjectIndex, engine.getNumObjects()))
        return; // nothing selected -- inert, matches ParameterRegistry::Scope::SelectedObject's own convention

    auto& obj = engine.getObject (selectedObjectIndex);
    if (obj.inputChannel < 0)
        return; // selected slot isn't an active object -- stay defensive

    if ((isSourceOverridden
         && (isSourceOverridden ("Gamepad0.LeftStick.X") || isSourceOverridden ("Gamepad0.LeftStick.Y")))
        || activeThrowMode != ThrowMode::None)
    {
        // Either a MappingEngine binding now explicitly claims (at least)
        // one axis -- hand full control to that mapping rather than also
        // applying built-in movement on top of it (see
        // setLeftStickOverrideQuery()'s own comment on why either axis
        // being claimed suppresses both, avoiding a half-working movement
        // feel from an axis rebound alone) -- or a throw-gesture button is
        // currently held (see driveThrowGesture()), which claims the left
        // stick for aiming instead of movement, same reasoning. Still
        // release any object this driver was previously holding, so it
        // doesn't drift forever at a stale velocity nobody updates anymore.
        releaseControlledObject();
        return;
    }

    const float shapedX = shapeAxis (lastState.leftStickX, deadzone, curveExponent);
    const float shapedY = shapeAxis (lastState.leftStickY, deadzone, curveExponent);

    // World-space mapping (deliberate, documented choice -- not yet
    // manually verified against real hardware in this environment, same
    // "reasonable default, unverified by eye" caveat as this project's
    // own camera drag/zoom sign convention, see README): stick "up"
    // (leftStickY > 0) pushes the object further away (SoundObject's own
    // +X = front), stick "right" (leftStickX > 0) moves the object right,
    // which is -Y in this project's own y=left-positive convention.
    const Vec3 shapedStick { shapedY, -shapedX, 0.0f };
    const bool sticksActive = shapedStick.length() > 1.0e-4f;
    const float fdt = (float) dt;

    if (! inertiaModeEnabled)
    {
        // Rate control (default): stick directly SETS velocity every
        // tick; TrajectoryEngine::integrate()'s Manual case integrates
        // position from it using its own dt. A centered stick sets
        // manualVelocity to exactly {0,0,0}, so the object stops EXACTLY
        // where it is and does not drift or spring back, per the
        // project's own explicit requirement.
        if (sticksActive && obj.mode != SoundObject::Mode::Manual)
            obj.mode = SoundObject::Mode::Manual; // "grab" the object, same as a mouse drag's beginDrag()

        if (obj.mode == SoundObject::Mode::Manual)
        {
            obj.manualVelocityActive = true;
            obj.manualVelocity = shapedStick * maxSpeed;
        }
    }
    else
    {
        // Inertia mode (opt-in, off by default): nudge the object's REAL
        // Impulse-mode velocity while the stick is held past the
        // deadzone, then simply stop nudging once it's centered --
        // Impulse mode's own EXISTING damping/dragCoefficient physics
        // (TrajectoryEngine::integrate()'s Impulse case) then decelerates
        // it naturally: "movement continues after release, with
        // braking," reusing real physics rather than a separate,
        // scripted inertia model.
        if (sticksActive)
        {
            obj.mode = SoundObject::Mode::Impulse;
            obj.velocity += shapedStick * inertiaAcceleration * fdt;
        }
    }
}

void GamepadDriver::driveObjectManagement (int& selectedObjectIndex, const GamepadState& previous, const GamepadState& current)
{
    // Edge-triggered: only the down-transition of each press fires, exactly
    // once, regardless of how many ticks the button stays held (matching
    // an ordinary UI button click, not a rate control like the stick).
    // D-pad Left/Right (not Button X -- moved here so cycling is
    // bidirectional and shares the D-pad's own "navigate a list" feel;
    // Button X no longer has a built-in behavior, still available for a
    // MappingEngine binding like any other raw source). D-pad Up/Down
    // independently drive camera zoom, see
    // PluginEditor::updateGamepadCamera().
    const bool cycleNextPressed = current.dpadRight && ! previous.dpadRight;
    const bool cyclePrevPressed = current.dpadLeft && ! previous.dpadLeft;
    const bool addPressed    = current.buttonA && ! previous.buttonA;
    const bool removePressed = current.buttonB && ! previous.buttonB;

    if ((cycleNextPressed || cyclePrevPressed) && engine.getNumActiveObjects() > 0)
    {
        // Scans forward or backward from the current selection (wrapping),
        // landing on the next ACTIVE slot in that direction -- starting
        // from -1 (nothing selected), a forward cycle naturally lands on
        // slot 0 first, no special-casing needed (unchanged from before
        // this became bidirectional).
        const int count = engine.getNumObjects();
        const int step = cycleNextPressed ? 1 : -1;
        int idx = selectedObjectIndex;
        for (int s = 0; s < count; ++s)
        {
            idx = ((idx + step) % count + count) % count; // wrap into [0, count) for either sign of step
            if (engine.getObject (idx).inputChannel >= 0)
            {
                selectedObjectIndex = idx;
                break;
            }
        }
    }

    if (addPressed)
    {
        // Mirrors PluginEditor::addObjectClicked() exactly (same
        // findNextInactiveObject() + activateObject() + immediate
        // selection), just triggered by a gamepad button instead of a
        // toolbar click -- works with no editor open, per this driver's
        // own "keeps working in the background" requirement.
        const int idx = engine.findNextInactiveObject();
        if (idx >= 0)
        {
            engine.activateObject (idx);
            selectedObjectIndex = idx;
        }
    }

    if (removePressed && juce::isPositiveAndBelow (selectedObjectIndex, engine.getNumObjects()))
    {
        // Mirrors PluginEditor::removeObjectClicked().
        engine.deactivateObject (selectedObjectIndex);
        selectedObjectIndex = -1;
    }
}

void GamepadDriver::driveThrowGesture (int selectedObjectIndex, const GamepadState& previous, const GamepadState& current)
{
    if (activeThrowMode == ThrowMode::None)
    {
        // Not currently aiming -- start a fresh aim only on an actual
        // button-down edge (not merely "is held"), so a button already
        // held from before this object became selected can't retroactively
        // start an aim it was never meant to. If more than one throw
        // button is somehow pressed on the very same tick, Free Throw
        // wins, then Orbit Shot, then Slingshot -- a deterministic
        // fallback for an unsupported combination, not a meaningful
        // priority order.
        ThrowMode pressed = ThrowMode::None;
        if (current.buttonY && ! previous.buttonY)                       pressed = ThrowMode::FreeThrow;
        else if (current.leftShoulder && ! previous.leftShoulder)        pressed = ThrowMode::OrbitShot;
        else if (current.rightShoulder && ! previous.rightShoulder)      pressed = ThrowMode::Slingshot;

        // Nothing selected -- there is nothing to aim, so don't enter an
        // aim state at all (matches PluginEditor::mouseDown()'s own
        // "Shift+click only starts a sling if an object was actually hit"
        // gate).
        if (pressed != ThrowMode::None && juce::isPositiveAndBelow (selectedObjectIndex, engine.getNumObjects()))
        {
            activeThrowMode = pressed;
            throwObjectIndex = selectedObjectIndex;
        }
        return;
    }

    // Currently aiming -- check whether the SPECIFIC button that started
    // this aim is still held (not just "some throw button or other").
    bool stillHeld = false;
    switch (activeThrowMode)
    {
        case ThrowMode::FreeThrow:  stillHeld = current.buttonY; break;
        case ThrowMode::OrbitShot:  stillHeld = current.leftShoulder; break;
        case ThrowMode::Slingshot:  stillHeld = current.rightShoulder; break;
        case ThrowMode::None:       break;
    }

    if (stillHeld)
        return; // still aiming -- the object's rest position is left untouched throughout, same as the mouse gesture while pulling

    // Released -- fire, using THIS tick's stick deflection as the final
    // aim. Deliberately a DIRECT analog mapping (push the stick the way
    // you want it to launch), not the mouse gesture's pull-BACK-then-
    // release metaphor -- that metaphor only makes sense with a visible
    // cursor being dragged away from the object, which a gamepad stick has
    // no equivalent of; "push this way to throw this way" is the natural
    // reading for an analog stick instead (see the class comment).
    const float shapedX = shapeAxis (current.leftStickX, deadzone, curveExponent);
    const float shapedY = shapeAxis (current.leftStickY, deadzone, curveExponent);
    // Same world-space stick convention driveSelectedObjectMovement() uses
    // (see its own comment): stick "up" -> +X (front), stick "right" -> -Y
    // (this project's y=left-positive convention).
    const Vec3 aimDirection { shapedY, -shapedX, 0.0f };
    const Vec3 pullVector = aimDirection * throwMaxPullDistanceMeters;

    const ThrowMode firedMode = activeThrowMode;
    const int objectIndex = throwObjectIndex;
    activeThrowMode = ThrowMode::None;
    throwObjectIndex = -1;

    // Below this deflection, fire nothing at all -- same
    // "an accidental tap shouldn't launch a near-zero-velocity throw"
    // reasoning as the mouse gesture's own minPullDistanceMeters gate
    // (SlingGesture.h). The object index is still re-checked here too
    // (rather than trusted from the snapshot above) in case it was
    // deactivated mid-aim by some other path.
    if (pullVector.length() < SlingGesture::minPullDistanceMeters
        || ! juce::isPositiveAndBelow (objectIndex, engine.getNumObjects()))
        return;

    switch (firedMode)
    {
        case ThrowMode::FreeThrow:  GestureActions::fire (engine, GestureActions::LaunchMode::FreeThrow, objectIndex, pullVector); break;
        case ThrowMode::OrbitShot:  GestureActions::fire (engine, GestureActions::LaunchMode::OrbitShot, objectIndex, pullVector); break;
        case ThrowMode::Slingshot:  GestureActions::fire (engine, GestureActions::LaunchMode::Slingshot, objectIndex, pullVector); break;
        case ThrowMode::None:       break;
    }
}

void GamepadDriver::poll (CanonicalInputHub& hub, int& selectedObjectIndex, double dt)
{
    const GamepadState previous = lastState;
    const GamepadState current = bridge.poll();
    dispatchAllChanges (hub, previous, current);
    lastState = current;

    driveObjectManagement (selectedObjectIndex, previous, current);
    driveThrowGesture (selectedObjectIndex, previous, current);
    driveSelectedObjectMovement (selectedObjectIndex, dt);
}
