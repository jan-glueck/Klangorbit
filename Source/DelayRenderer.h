#pragma once
#include <cmath>
#include "VoiceFilter.h"

/**
    Real-time-safe read/write for one object's recirculating delay-line
    buffer (feature/delay-instance) -- shared between PluginProcessor
    (the real audio path) and Tools/verify_delay_instance.cpp (DSP
    correctness tests), same "tests exercise the exact code the plugin
    runs" convention as GrainRenderer.h's renderGrainBlock().

    Architecture: ONE circular buffer per object stores
        y[n] = input[n] + feedbackGain * damped(y[n - feedbackDelaySamples])
    i.e. the buffer already contains its own decayed past repeats, purely
    as a side effect of its own recursive definition -- reading it at
    n - D, n - 2D, n - 3D, ... (ordinary multiples of the SAME delay)
    naturally yields progressively quieter, darker copies, each one
    `feedbackGain` times the previous and filtered `damped()` every time
    through the loop. Multiple taps are therefore just multiple READ
    offsets into this one buffer, not independent delay/feedback paths.
*/

// Writes numSamples of inputBlock into the circular delayBufferData,
// applying feedback recirculation. delayBufferData/delayBufferSize: the
// circular buffer (same one readDelayTapBlock() below reads from).
// writePos: persistent, absolute (wrapped internally), owned by the
// CALLER (one int per object, audio-thread-private -- no other thread
// ever touches the buffer or this position) and advanced by this call.
// feedbackDelaySamples: the recirculation length, may be fractional
// (linearly interpolated read). dampingFilter: persistent per-object
// VoiceFilter (LowPass), owned by the caller across blocks; this
// function only ever calls processSample() on it, never touches RNG --
// see VoiceFilter.h's own comment on that split. feedbackGain: 0 = no
// recirculation at all (a single, non-repeating echo per tap).
inline void writeDelayLineBlock (float* delayBufferData, int delayBufferSize, int& writePos,
                                   const float* inputBlock, int numSamples,
                                   float feedbackDelaySamples, float feedbackGain,
                                   VoiceFilter& dampingFilter)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double readPos = (double) writePos - (double) feedbackDelaySamples;
        const int idx0raw = (int) std::floor (readPos);
        const float frac = (float) (readPos - (double) idx0raw);
        const int idx0 = ((idx0raw % delayBufferSize) + delayBufferSize) % delayBufferSize;
        const int idx1 = (idx0 + 1) % delayBufferSize;
        const float feedbackSample = delayBufferData[idx0] * (1.0f - frac) + delayBufferData[idx1] * frac;

        const float damped = dampingFilter.processSample (feedbackSample);
        delayBufferData[writePos] = inputBlock[i] + feedbackGain * damped;

        writePos = (writePos + 1) % delayBufferSize;
    }
}

// Reads one tap's continuous (non-windowed, no envelope -- unlike grains)
// output for this block. writePosAfterBlock: the write position AFTER
// writeDelayLineBlock() has already written this entire block (i.e. this
// function runs in a SEPARATE pass, after writing, same "dry buffer fully
// filled first, consumers read it afterward" convention as
// grainRingBuffers/GrainCloud). tapDelaySamples: this tap's own delay,
// may be fractional (linearly interpolated read); constant for the
// whole block (no per-grain pitch/envelope concept here -- a delay tap
// is a straight 1:1-rate readback).
inline void readDelayTapBlock (const float* delayBufferData, int delayBufferSize,
                                 int writePosAfterBlock, float tapDelaySamples,
                                 float* destBlock, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Sample i in this block was written at absolute position
        // (writePosAfterBlock - numSamples + i) -- the oldest-to-newest
        // order writeDelayLineBlock() just wrote them in.
        const double writtenAt = (double) (writePosAfterBlock - numSamples + i);
        const double readPos = writtenAt - (double) tapDelaySamples;
        const int idx0raw = (int) std::floor (readPos);
        const float frac = (float) (readPos - (double) idx0raw);
        const int idx0 = ((idx0raw % delayBufferSize) + delayBufferSize) % delayBufferSize;
        const int idx1 = (idx0 + 1) % delayBufferSize;
        destBlock[i] = delayBufferData[idx0] * (1.0f - frac) + delayBufferData[idx1] * frac;
    }
}
