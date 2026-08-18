#pragma once
#include <cmath>
#include <juce_core/juce_core.h>

/**
    Renders numSamples of one grain's windowed, pitch-shifted playback from
    a circular ring buffer, advancing samplesPlayed in place. Real-time-safe
    (no allocation, no locking). Shared between PluginProcessor (the real
    audio path) and Tools/verify_grain_cloud.cpp (DSP correctness tests),
    so the tests exercise the exact same code the plugin runs, not a
    reimplementation of it.

    ringBufferData/ringBufferSize: the circular buffer to read from.
    bufferReadStartSample: absolute (not wrapped) sample index in the ring
        buffer's write-position timeline where this grain started reading.
    playbackRate: pitch ratio, 1 = normal speed.
    grainLengthSamples: total length of this grain's envelope, in samples.
    samplesPlayed: how many samples of this grain have already been
        rendered in previous calls; advanced by this call, up to
        grainLengthSamples.
    destBlock: receives numSamples of windowed output (0 once the grain's
        envelope is exhausted).
*/
inline void renderGrainBlock (const float* ringBufferData, int ringBufferSize,
                               int bufferReadStartSample, float playbackRate, int grainLengthSamples,
                               int& samplesPlayed, float* destBlock, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (samplesPlayed >= grainLengthSamples)
        {
            destBlock[i] = 0.0f;
            continue;
        }

        const float readPosF = (float) bufferReadStartSample + (float) samplesPlayed * playbackRate;
        const int idx0raw = (int) std::floor (readPosF);
        const float frac = readPosF - (float) idx0raw;
        const int idx0 = ((idx0raw % ringBufferSize) + ringBufferSize) % ringBufferSize;
        const int idx1 = (idx0 + 1) % ringBufferSize;
        const float sample = ringBufferData[idx0] * (1.0f - frac) + ringBufferData[idx1] * frac;

        // Hann window over [0, grainLengthSamples): zero at both ends, peak
        // in the middle -- the only shape implemented so far
        // (GrainWindowShape is deliberately extensible).
        //
        // Denominator is (grainLengthSamples - 1), NOT grainLengthSamples:
        // that makes t reach EXACTLY 1.0 (envelope exactly 0.0) on the
        // LAST rendered sample, matching the silent sample that follows it
        // once samplesPlayed reaches grainLengthSamples above. With a
        // plain "/ grainLengthSamples" denominator, the last rendered
        // sample's t falls just short of 1.0, leaving a small residual
        // envelope value that then jumps to a hard 0.0 on the next call --
        // a real, audible discontinuity for short grains (the residual is
        // ~(pi/grainLengthSamples)^2, e.g. ~10% of peak amplitude at
        // grainLengthSamples=100, i.e. a ~2ms grain at 48kHz). This is
        // rate-independent (grainLengthSamples is fixed in OUTPUT samples
        // regardless of playbackRate -- see Grain.h), so it affects every
        // grain, not specifically pitch-shifted ones; extreme pitchJitter
        // just makes short/dense grain clouds a common way to notice it.
        // See Tools/verify_grain_cloud.cpp for the before/after measurement.
        const float denom = (grainLengthSamples > 1) ? (float) (grainLengthSamples - 1) : 1.0f;
        const float t = (float) samplesPlayed / denom;
        const float envelope = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * t);

        destBlock[i] = sample * envelope;
        ++samplesPlayed;
    }
}
