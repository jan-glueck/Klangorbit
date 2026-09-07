#pragma once
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>
#include "HrtfDataset.h"

/**
    Decodes the summed Ambisonics B-format bus down to 2-channel binaural
    (headphone) audio, via the standard "dense virtual loudspeaker array +
    per-direction HRTF convolution" technique -- the same two-stage shape
    AmbisonicsDecoder's own AllRAD path already uses for Quad/5.1/7.1/
    Atmos (decode to a dense virtual array first, see
    SphericalHarmonicsUtils.h), except the second stage convolves each
    virtual speaker's own signal through that direction's measured HRIR
    (via HrtfDataset) and sums to L/R, instead of VBAP-remapping to a
    handful of real speakers. Lives entirely separately from
    AmbisonicsDecoder, per that class's own "Binaural is NOT part of this
    class" boundary (needs an HRTF dataset + convolution, a genuinely
    different dependency shape) -- AmbisonicsDecoder only knows enough
    about Mode::Binaural to represent it as a selectable format (channel
    count, bus layout); this class does the actual decoding, invoked
    directly from KlangorbitProcessor::processBlock() in place of
    AmbisonicsDecoder::decode() when that mode is active.

    Two-stage per virtual speaker, per block, in decode():
      1. Apply that speaker's own row of the ambisonics->virtual-speaker
         decode matrix (built once in prepare(), identical machinery to
         AllRAD's own dVirtual stage) to the incoming B-format bus, giving
         a mono signal for that one virtual direction.
      2. Convolve that mono signal through the direction's own L and R
         juce::dsp::Convolution engines (impulse responses loaded from
         HrtfDataset::getFilter() once in prepare(), never per block) and
         accumulate into the stereo output.

    CPU cost: numVirtualSpeakers (50) x 2 juce::dsp::Convolution instances
    run every block, all decode-once-per-block (not per active object --
    every SoundObject is already summed into one shared B-format bus
    before any decode mode runs, see PluginProcessor::processBlock()'s own
    per-object loop). This is NOT profiled on real hardware in this
    environment -- the project's own CPU meter (toolbar) is how to check
    it on real hardware; 50 was chosen to match AllRAD's own already-
    proven virtual-array density rather than guessing a smaller number,
    but is easy to tune down later (a single constant) if it proves too
    costly. Interaural delay (HrtfDataset::getFilter()'s own
    outDelayLeftSamples/outDelayRightSamples) is NOT applied -- a
    disclosed, deliberate v1 simplification, see HrtfDataset.h's own
    comment for the reasoning and what a future enhancement would add.

    Threading: prepare() allocates (builds the decode matrix, loads 100
    impulse responses) and must only be called from the message thread,
    same rule AmbisonicsDecoder::setMode()'s own buildAllRadMatrix()
    already follows. decode() is real-time-safe (no allocation) and is
    the only method ever called from processBlock()/the audio thread.
*/
class BinauralDecoder
{
public:
    BinauralDecoder();

    // 50 -- matches AllRAD's own numVirtual constant (AmbisonicsDecoder.cpp)
    // for the same "dense enough that the virtual array's own precision
    // isn't the limiting factor" reasoning; see the class comment on why
    // this is a deliberately reused, not independently re-tuned, value.
    static constexpr int numVirtualSpeakers = 50;

    // Builds the decode matrix and loads all 2*numVirtualSpeakers
    // convolution engines from `dataset` at the given sample rate/block
    // size. Call whenever the active HRTF dataset changes (a different
    // bundled dataset selected, or a new custom SOFA file loaded) or from
    // prepareToPlay(). A no-op (decode() then just clears its output)
    // if `dataset` isn't currently loaded. NOT real-time-safe -- message
    // thread only, see the class comment.
    void prepare (const HrtfDataset& dataset, double sampleRate, int maxBlockSize);

    // Real-time-safe. ambiInput must have at least as many channels as
    // the decode matrix's own row width (order-3 Ambisonics, 16 channels
    // -- see AmbisonicsDecoder::ambisonicsOrderFor(Mode::Binaural), always
    // the fixed internal order every decoded mode uses). stereoOutput
    // must have at least 2 channels; only channels 0/1 are written
    // (cleared first, then accumulated into -- matches
    // AmbisonicsDecoder::decode()'s own "caller owns the buffer, we don't
    // assume it's already silent" contract). If prepare() was never
    // successfully called (or the dataset it was called with wasn't
    // loaded), stereoOutput is just cleared -- silence, not garbage or a
    // crash.
    void decode (const juce::AudioBuffer<float>& ambiInput, juce::AudioBuffer<float>& stereoOutput, int numSamples);

private:
    // See its own comment (BinauralDecoder.cpp) -- practical loudness
    // calibration, called once from prepare().
    static float calibrateOutputGain (const std::vector<std::vector<double>>& combinedLeft,
                                       const std::vector<std::vector<double>>& combinedRight);

    // decodeMatrix[virtualSpeakerIndex][ambiChannel].
    std::vector<std::vector<float>> decodeMatrix;

    // Practical loudness calibration, computed once in prepare() -- see its
    // own comment there. Applied as a single multiply on the final L/R sum
    // in decode(). Without this, summing numVirtualSpeakers (50) un-
    // normalized HRIR convolutions (Normalise::no is deliberate, see
    // loadImpulseResponse() below -- that flag is about not flattening each
    // IR's own physically-meaningful directional level, a different thing
    // from calibrating the SUM's overall output level) came out
    // substantially louder than every other decode mode, and inconsistently
    // so between HRTF datasets whose own absolute measurement level differs
    // (e.g. SADIE II measured louder than KEMAR).
    float outputGain = 1.0f;

    struct VirtualSpeakerConvolvers
    {
        juce::dsp::Convolution left, right;
    };
    // unique_ptr, not a plain value -- juce::dsp::Convolution is neither
    // copyable nor movable (JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
    // deletes its copy ctor, which also suppresses the implicit move
    // ctor), so a plain std::vector<VirtualSpeakerConvolvers> can't
    // reallocate/resize -- the indirection sidesteps that regardless of
    // the pointee's own movability.
    std::vector<std::unique_ptr<VirtualSpeakerConvolvers>> convolvers; // size numVirtualSpeakers once prepared

    // Reused scratch, sized once in prepare() -- never (re)allocated in
    // decode(). virtualSpeakerScratch holds one virtual speaker's decoded
    // mono signal (the decode-matrix stage's output); earScratchL/R hold a
    // COPY of it per ear before each convolution call, since
    // juce::dsp::ProcessContextReplacing processes in place and the same
    // mono signal is fed through two DIFFERENT (L/R) filters per speaker,
    // so it can't be convolved destructively in one shared buffer twice.
    juce::AudioBuffer<float> virtualSpeakerScratch, earScratchL, earScratchR;

    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BinauralDecoder)
};
