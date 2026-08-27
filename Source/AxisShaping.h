#pragma once
#include <cmath>
#include <juce_core/juce_core.h>

/**
    Deadzone rescale + exponential response curve for a bipolar (-1..1)
    continuous control axis -- e.g. a gamepad stick (see GamepadDriver),
    but not inherently gamepad-specific: a future MIDI/OSC driver mapping
    a continuous controller to movement could reuse this same shaping, so
    it lives in its own small header rather than as a GamepadDriver-only
    private method.

    deadzone: fraction of full deflection near center treated as exactly
    zero, rescaled so there's no jump at the boundary -- a magnitude just
    past the deadzone starts near 0 output, not deadzone's own nonzero
    fraction. Conventional range ~0.05-0.10 (5-10%).

    curveExponent: 1.0 = linear response; higher values give finer control
    near center and reserve full output for a more deliberate, near-full
    deflection (output = sign(input) * rescaled^curveExponent).
*/
inline float shapeAxis (float raw, float deadzone, float curveExponent)
{
    const float magnitude = std::abs (raw);
    if (magnitude <= deadzone)
        return 0.0f;

    const float rescaled = (magnitude - deadzone) / juce::jmax (1.0e-6f, 1.0f - deadzone);
    const float curved = std::pow (juce::jlimit (0.0f, 1.0f, rescaled), curveExponent);
    return (raw < 0.0f ? -curved : curved);
}
