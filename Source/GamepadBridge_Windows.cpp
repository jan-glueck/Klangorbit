#include "GamepadBridge.h"
#include <juce_core/juce_core.h> // juce::jlimit()

// windows.h's own min/max macros (without NOMINMAX) would otherwise break
// every std::min/std::max call in any header transitively included below
// (juce_core's own, xinput.h's, etc.) -- defined explicitly here rather
// than relying on JUCE's own headers to have set this up first, since
// C++ include guards mean whichever #include <windows.h> comes first in
// this translation unit is the one that actually decides these macros.
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>

// No persistent state needed -- poll() re-scans the XInput slots fresh
// every call, see the header's own comment. The Impl struct exists
// purely so the header can stay platform-neutral (PIMPL), matching
// GamepadBridge.mm's own empty Impl for the same reason -- nothing to
// put in it here.
struct GamepadBridge::Impl {};

GamepadBridge::GamepadBridge() : impl (std::make_unique<Impl>()) {}
GamepadBridge::~GamepadBridge() = default;

GamepadState GamepadBridge::poll()
{
    GamepadState state;

    // XInput has a small, fixed number of controller "slots" (0-3), not a
    // real enumeration -- unlike GCController.controllers on macOS, an
    // unplugged slot simply reports ERROR_DEVICE_NOT_CONNECTED rather
    // than not existing. Scan in slot order and use the first slot that's
    // actually connected, mirroring "first in GCController.controllers"
    // on the macOS side closely enough for this class's single-controller
    // scope (see the header's own comment).
    XINPUT_STATE xState {};
    DWORD userIndex = 0;
    bool found = false;

    for (; userIndex < XUSER_MAX_COUNT; ++userIndex)
    {
        if (XInputGetState (userIndex, &xState) == ERROR_SUCCESS)
        {
            found = true;
            break;
        }
    }

    if (! found)
        return state; // connected stays false, everything else stays at its zero/false default

    state.connected = true;

    const XINPUT_GAMEPAD& pad = xState.Gamepad;

    // XInput's own axis convention already matches GCControllerAxisInput's
    // (-1..1, positive = right (X) / up (Y)) -- read as-is, no sign flip,
    // same "report the hardware's own raw values, let GamepadDriver decide
    // any world-space mapping" contract as GamepadBridge.mm. Thumbstick
    // range is technically -32768..32767 (one extra negative step) --
    // dividing by 32767 for both signs (not 32768) keeps +1/-1 symmetric
    // and only clips the single most-negative raw value, never a value a
    // deadzone/response curve downstream would treat differently anyway.
    constexpr float thumbScale = 1.0f / 32767.0f;
    state.leftStickX  = juce::jlimit (-1.0f, 1.0f, (float) pad.sThumbLX * thumbScale);
    state.leftStickY  = juce::jlimit (-1.0f, 1.0f, (float) pad.sThumbLY * thumbScale);
    state.rightStickX = juce::jlimit (-1.0f, 1.0f, (float) pad.sThumbRX * thumbScale);
    state.rightStickY = juce::jlimit (-1.0f, 1.0f, (float) pad.sThumbRY * thumbScale);

    // Triggers are 0..255 on XInput vs. GCControllerButtonInput's 0..1 --
    // rescale to match, no deadzone applied here either (same raw-value
    // contract as above).
    state.leftTrigger  = (float) pad.bLeftTrigger / 255.0f;
    state.rightTrigger = (float) pad.bRightTrigger / 255.0f;

    state.buttonA = (pad.wButtons & XINPUT_GAMEPAD_A) != 0;
    state.buttonB = (pad.wButtons & XINPUT_GAMEPAD_B) != 0;
    state.buttonX = (pad.wButtons & XINPUT_GAMEPAD_X) != 0;
    state.buttonY = (pad.wButtons & XINPUT_GAMEPAD_Y) != 0;
    state.leftShoulder  = (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    state.rightShoulder = (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;

    state.dpadUp    = (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    state.dpadDown  = (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    state.dpadLeft  = (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    state.dpadRight = (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

    return state;
}
