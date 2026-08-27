#pragma once
#include <memory>

/**
    Raw, unshaped snapshot of one connected gamepad's current state --
    plain values as read straight from the hardware/driver, no deadzone or
    response-curve shaping applied (see GamepadDriver for that). Sticks
    are bipolar (-1..1 per axis), triggers are unipolar (0..1), buttons
    are boolean.
*/
struct GamepadState
{
    bool connected = false;

    float leftStickX = 0.0f, leftStickY = 0.0f;
    float rightStickX = 0.0f, rightStickY = 0.0f;
    float leftTrigger = 0.0f, rightTrigger = 0.0f;

    bool buttonA = false, buttonB = false, buttonX = false, buttonY = false;
    bool leftShoulder = false, rightShoulder = false;
    bool dpadUp = false, dpadDown = false, dpadLeft = false, dpadRight = false;
};

/**
    Thin bridge to Apple's GameController framework (macOS-only; this
    project targets macOS exclusively, see PROJECT_BRIEF.md). PIMPL'd
    (see Impl, defined only in GamepadBridge.mm) specifically so this
    header stays plain C++ -- no Objective-C types leak out, so any file
    can #include this without itself needing to be compiled as
    Objective-C++, and without pulling in <GameController/GameController.h>.

    Exactly one gamepad at a time: poll() always reports whichever
    controller is first in GCController.controllers that exposes an
    extendedGamepad profile (Apple's modern, full-featured profile: dual
    sticks, 4 face buttons, 2 shoulder buttons, 2 analog triggers, D-pad).
    Deliberately not architected around a hard single-controller
    assumption though -- a later multi-controller extension would mean
    widening this bridge's own interface (e.g. poll(int controllerIndex))
    and GamepadDriver's sourceId scheme (already "Gamepad0.*", ready for
    "Gamepad1.*" etc.), not restructuring either from scratch. Multi-
    controller support itself is explicitly NOT part of this class.

    No connect/disconnect notification plumbing -- poll() just re-checks
    GCController.controllers itself on every call, which is simple, avoids
    any Objective-C observer lifetime management, and is cheap enough at
    control rate (~90Hz, see GamepadDriver/KlangorbitProcessor's timer).
*/
class GamepadBridge
{
public:
    GamepadBridge();
    ~GamepadBridge();

    // Safe to call from any thread that isn't concurrently destroying
    // this object -- see GamepadDriver::poll()'s own comment on where
    // this is actually called from (KlangorbitProcessor's own timer, the
    // message thread).
    GamepadState poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
