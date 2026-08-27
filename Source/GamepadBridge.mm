#include "GamepadBridge.h"
#import <GameController/GameController.h>

// No persistent Objective-C state needed -- poll() re-reads
// GCController.controllers fresh every call, see the header's own
// comment on why. The Impl struct exists purely so the header can stay
// plain C++ (PIMPL) -- nothing to put in it here.
struct GamepadBridge::Impl {};

GamepadBridge::GamepadBridge() : impl (std::make_unique<Impl>()) {}
GamepadBridge::~GamepadBridge() = default;

GamepadState GamepadBridge::poll()
{
    GamepadState state;

    GCController* found = nil;
    for (GCController* c in [GCController controllers])
    {
        if (c.extendedGamepad != nil) // only the modern, full-featured profile is supported -- see the header's own comment
        {
            found = c;
            break;
        }
    }

    if (found == nil)
        return state; // connected stays false, everything else stays at its zero/false default

    GCExtendedGamepad* gamepad = found.extendedGamepad;
    state.connected = true;

    // Apple's own axis convention (GCControllerAxisInput): -1..1, positive
    // = right (X) / up (Y) -- read as-is, no sign flip here. GamepadDriver
    // is where any world-space mapping convention gets decided, not here;
    // this bridge only ever reports the hardware's own raw values.
    state.leftStickX = gamepad.leftThumbstick.xAxis.value;
    state.leftStickY = gamepad.leftThumbstick.yAxis.value;
    state.rightStickX = gamepad.rightThumbstick.xAxis.value;
    state.rightStickY = gamepad.rightThumbstick.yAxis.value;

    state.leftTrigger = gamepad.leftTrigger.value;
    state.rightTrigger = gamepad.rightTrigger.value;

    state.buttonA = gamepad.buttonA.isPressed;
    state.buttonB = gamepad.buttonB.isPressed;
    state.buttonX = gamepad.buttonX.isPressed;
    state.buttonY = gamepad.buttonY.isPressed;
    state.leftShoulder = gamepad.leftShoulder.isPressed;
    state.rightShoulder = gamepad.rightShoulder.isPressed;

    state.dpadUp = gamepad.dpad.up.isPressed;
    state.dpadDown = gamepad.dpad.down.isPressed;
    state.dpadLeft = gamepad.dpad.left.isPressed;
    state.dpadRight = gamepad.dpad.right.isPressed;

    return state;
}
