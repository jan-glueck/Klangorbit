#include "GamepadDriver.h"
#include <cmath>

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

    if (isSourceOverridden
        && (isSourceOverridden ("Gamepad0.LeftStick.X") || isSourceOverridden ("Gamepad0.LeftStick.Y")))
    {
        // A MappingEngine binding now explicitly claims (at least) one
        // axis -- hand full control to that mapping rather than also
        // applying built-in movement on top of it. See
        // setLeftStickOverrideQuery()'s own comment on why either axis
        // being claimed suppresses both (avoids a half-working movement
        // feel from an axis rebound alone). Still release any object
        // this driver was previously holding, so it doesn't drift
        // forever at a stale velocity nobody updates anymore.
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
            if (obj.mode != SoundObject::Mode::Impulse && obj.mode != SoundObject::Mode::Attracted)
                obj.mode = SoundObject::Mode::Impulse;
            obj.velocity += shapedStick * inertiaAcceleration * fdt;
        }
    }
}

void GamepadDriver::poll (CanonicalInputHub& hub, int selectedObjectIndex, double dt)
{
    const GamepadState current = bridge.poll();
    dispatchAllChanges (hub, lastState, current);
    lastState = current;

    driveSelectedObjectMovement (selectedObjectIndex, dt);
}
