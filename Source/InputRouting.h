#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "SoundObject.h"

/**
    Resolves and applies the input routing matrix (feature/input-
    routing-matrix) -- which live input channel(s) actually feed a given
    object's live signal. Pure, header-only, no dependency beyond
    `juce::AudioBuffer`/`juce::FloatVectorOperations` -- same
    independently-testable-without-a-plugin-build convention as
    `DelayRenderer.h`/`GrainRenderer.h`.

    `SoundObject::routedInputChannels` is the one piece of state this
    operates on -- see its own comment for the "empty means the old 1:1
    default, non-empty fully replaces it" rule. Everywhere else in the
    codebase, `SoundObject::inputChannel >= 0` stays the sole "is this
    object active" flag, completely unchanged by this feature --
    `resolveChannels()` below still returns empty for an inactive
    object, so `KlangorbitProcessor::processBlock()`'s own existing
    activity gating needs no changes either.
*/
namespace InputRouting
{
    // Which channels actually feed `obj` right now: empty if `obj` is
    // inactive (`inputChannel < 0`), `{ obj.inputChannel }` if
    // `routedInputChannels` is empty (the original 1:1 default), else
    // `routedInputChannels` verbatim.
    inline std::vector<int> resolveChannels (const SoundObject& obj)
    {
        if (obj.inputChannel < 0)
            return {};
        if (! obj.routedInputChannels.empty())
            return obj.routedInputChannels;
        return { obj.inputChannel };
    }

    // Sums every resolved channel's samples into `dst` -- does NOT
    // clear `dst` first (caller's responsibility, matching how the
    // grain/delay/propagation call sites already treat their own
    // buffers). Each channel index is bounds-checked individually
    // against `numInputChannels`, not just against `channels` itself
    // -- a host-negotiated narrower input bus silently drops an
    // out-of-range routed channel's contribution rather than reading
    // out of bounds.
    inline void mixRoutedInput (float* dst, const juce::AudioBuffer<float>& input,
                                  const std::vector<int>& channels, int numInputChannels, int numSamples)
    {
        for (int ch : channels)
            if (ch >= 0 && ch < numInputChannels)
                juce::FloatVectorOperations::add (dst, input.getReadPointer (ch), numSamples);
    }
}
