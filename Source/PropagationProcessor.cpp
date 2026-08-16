#include "PropagationProcessor.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>

namespace
{
    // Simplified, artistic approximation of frequency-dependent air
    // absorption -- NOT the full ISO 9613-1 relaxation-frequency model
    // (that needs temperature/humidity/pressure-dependent relaxation
    // frequencies for O2 and N2 molecules, well beyond POC scope). Captures
    // the general, documented trends instead: absorption increases with
    // distance, and -- for the mid/high frequencies most affected -- peaks
    // around medium relative humidity rather than at the extremes (real,
    // if counterintuitive, behavior).
    float computeAirAbsorptionCutoffHz (float distanceMeters, const SceneSettings& scene)
    {
        constexpr float maxCutoffHz = 18000.0f; // effectively "no filtering" at zero distance
        constexpr float minCutoffHz = 300.0f;   // never fully mutes, keeps some low end audible

        // Peaks (~1.6x) around 50% RH, drops back to ~1.0x at the extremes.
        const float humidityDeviation = scene.relativeHumidity - 50.0f;
        const float humidityFactor = 1.0f + 0.6f * std::exp (-(humidityDeviation * humidityDeviation) / (2.0f * 25.0f * 25.0f));

        // Real relaxation effects are more complex; a simple linear nudge
        // is enough to make temperature audibly matter for the POC.
        const float temperatureFactor = 1.0f + 0.01f * (scene.temperature - 20.0f);

        // Minor effect vs. humidity/temperature; simple linear placeholder
        // around the 101.325 kPa sea-level reference.
        const float pressureFactor = 1.0f + 0.05f * ((scene.atmosphericPressure - 101.325f) / 101.325f);

        // Per-meter absorption coefficient -- tuned by ear/by eye for a
        // plausible-sounding falloff, not measured.
        const float absorptionCoefficient = 0.15f * humidityFactor * temperatureFactor * pressureFactor;

        const float cutoff = maxCutoffHz * std::exp (-absorptionCoefficient * juce::jmax (0.0f, distanceMeters));
        return juce::jlimit (minCutoffHz, maxCutoffHz, cutoff);
    }

    float computeDirectivityGain (SoundObject::DirectivityPattern pattern, Vec3 sourceOrientation, Vec3 objectPosition)
    {
        if (pattern == SoundObject::DirectivityPattern::Omni)
            return 1.0f;

        const float orientationLen = sourceOrientation.length();
        const float distance = objectPosition.length();
        if (orientationLen < 1.0e-6f || distance < 1.0e-6f)
            return 1.0f; // degenerate case (no orientation, or object at the listener position): fall back to omni

        const Vec3 orientationDir = sourceOrientation / orientationLen;
        const Vec3 toListenerDir  = (objectPosition * -1.0f) / distance; // from object to listener (origin)
        const float cosAngle = orientationDir.dot (toListenerDir);

        switch (pattern)
        {
            case SoundObject::DirectivityPattern::Omni:     return 1.0f;
            case SoundObject::DirectivityPattern::Cardioid: return juce::jmax (0.0f, (1.0f + cosAngle) * 0.5f);
            case SoundObject::DirectivityPattern::Figure8:  return std::abs (cosAngle);
        }
        return 1.0f;
    }
}

void PropagationProcessor::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate;
    const int bufSize = (int) (maxDelaySeconds * sampleRate) + 8;
    delayBuffer.assign ((size_t) bufSize, 0.0f);
    reset();
}

void PropagationProcessor::reset()
{
    std::fill (delayBuffer.begin(), delayBuffer.end(), 0.0f);
    writePos = 0;
    currentDelaySamples = 0.0f;
    previousTargetDelaySamples = -1.0f;
    smoothedDerivative = 0.0f;
    filterState = 0.0f;
}

void PropagationProcessor::process (const float* sourceBlock,
                                     float* destBlock,
                                     int numSamples,
                                     Vec3 objectPosition,
                                     const SoundObject& obj,
                                     const SceneSettings& scene,
                                     float& directivityGainOut)
{
    if (delayBuffer.empty() || numSamples <= 0)
    {
        directivityGainOut = 1.0f;
        return;
    }

    const float distanceMeters = juce::jmax (objectPosition.length(), 0.001f);

    // Wind shifts the effective speed of sound in the propagation
    // direction (source -> listener): a tailwind speeds up arrival, a
    // headwind slows it down -- see SceneSettings::windVector.
    const Vec3 propagationDir = (objectPosition * -1.0f) / distanceMeters;
    const float windComponent = scene.windVector.dot (propagationDir);
    const float effectiveSpeedOfSound = juce::jmax (1.0f, scene.speedOfSound + windComponent);

    const int bufSize = (int) delayBuffer.size();
    const float maxDelaySamples = (float) bufSize - 8.0f;

    const float targetDelaySamples = juce::jlimit (0.0f, maxDelaySamples,
                                                      distanceMeters / effectiveSpeedOfSound * (float) sampleRate);

    if (previousTargetDelaySamples < 0.0f) // first block since prepare()/reset()
    {
        currentDelaySamples = targetDelaySamples;
        previousTargetDelaySamples = targetDelaySamples;
    }

    const float blockSeconds = (float) numSamples / (float) sampleRate;

    // Smooth the raw per-block delay derivative (the thing that drives the
    // audible pitch shift) to avoid clicks on sudden direction reversals
    // (e.g. a bounce off the room boundary).
    const float rawDerivative = targetDelaySamples - previousTargetDelaySamples;
    const float smoothingCoeff = obj.dopplerSmoothing > 0.0f
        ? std::exp (-blockSeconds / obj.dopplerSmoothing) : 0.0f;
    smoothedDerivative = smoothingCoeff * smoothedDerivative + (1.0f - smoothingCoeff) * rawDerivative;

    // dopplerFactor scales only this "AC" (pitch-shift-driving) component.
    const float dopplerContribution = obj.dopplerFactor * smoothedDerivative;

    // Slow, dopplerFactor-independent correction toward the true target, so
    // absolute latency can't drift away from the real distance over time
    // even when dopplerFactor != 1. Rate-limited (not proportional) so it
    // never contributes a significant pitch artifact of its own -- see
    // maxDriftRatePerSecond.
    const float maxDriftThisBlock = maxDriftRatePerSecond * (float) numSamples;
    const float driftContribution = juce::jlimit (-maxDriftThisBlock, maxDriftThisBlock,
                                                     targetDelaySamples - currentDelaySamples);

    const float delayAtBlockStart = currentDelaySamples;
    const float delayAtBlockEnd = juce::jlimit (0.0f, maxDelaySamples,
                                                  currentDelaySamples + dopplerContribution + driftContribution);

    const int blockStartWritePos = writePos;

    for (int i = 0; i < numSamples; ++i)
    {
        const int absoluteWriteIndex = blockStartWritePos + i;
        delayBuffer[(size_t) (absoluteWriteIndex % bufSize)] = sourceBlock[i];

        // Linearly interpolate the delay itself across the block, so the
        // read position moves smoothly (this smooth movement IS the
        // Doppler pitch shift -- see class comment).
        const float t = (numSamples > 1) ? (float) i / (float) (numSamples - 1) : 0.0f;
        const float delaySamples = delayAtBlockStart + (delayAtBlockEnd - delayAtBlockStart) * t;

        const float readPosF = (float) absoluteWriteIndex - delaySamples;
        const int readIndex0 = (int) std::floor (readPosF);
        const float frac = readPosF - (float) readIndex0;
        const int idx0 = ((readIndex0 % bufSize) + bufSize) % bufSize;
        const int idx1 = (idx0 + 1) % bufSize;

        destBlock[i] = delayBuffer[(size_t) idx0] * (1.0f - frac) + delayBuffer[(size_t) idx1] * frac;
    }

    writePos = (blockStartWritePos + numSamples) % bufSize;
    currentDelaySamples = delayAtBlockEnd;
    previousTargetDelaySamples = targetDelaySamples;

    // --- Air absorption: simplified one-pole lowpass -----------------------
    const float cutoffHz = computeAirAbsorptionCutoffHz (distanceMeters, scene);
    const float filterCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoffHz / (float) sampleRate);
    for (int i = 0; i < numSamples; ++i)
    {
        filterState += filterCoeff * (destBlock[i] - filterState);
        destBlock[i] = filterState;
    }

    directivityGainOut = computeDirectivityGain (obj.directivityPattern, obj.sourceOrientation, objectPosition);
}
