#include "BinauralDecoder.h"
#include "AmbisonicsEncoder.h"
#include "SphericalHarmonicsUtils.h"
#include <cmath>

BinauralDecoder::BinauralDecoder() = default;

void BinauralDecoder::prepare (const HrtfDataset& dataset, double sampleRate, int maxBlockSize)
{
    prepared = false;
    convolvers.clear();
    decodeMatrix.clear();

    if (! dataset.isLoaded())
        return; // decode() will just clear its output -- see the class comment

    AmbisonicsEncoder shHelper;
    shHelper.setOrder (3); // fixed internal order every decoded mode uses, see AmbisonicsDecoder::ambisonicsOrderFor()
    const int numAmbiCh = shHelper.getNumChannels();

    const auto virtualDirs = SphericalHarmonicsUtils::fibonacciSphere (numVirtualSpeakers);
    const auto reWeights = SphericalHarmonicsUtils::maxReWeights (shHelper.getOrder());

    // Stage 1: ambisonics -> virtual array -- identical machinery to
    // AllRAD's own dVirtual stage (AmbisonicsDecoder.cpp's
    // buildAllRadMatrix()), see SphericalHarmonicsUtils.h's own comment on
    // why this is shared, not re-derived here.
    decodeMatrix.assign ((size_t) numVirtualSpeakers, std::vector<float> ((size_t) numAmbiCh, 0.0f));
    for (int m = 0; m < numVirtualSpeakers; ++m)
    {
        const float az = std::atan2 (virtualDirs[(size_t) m].y, virtualDirs[(size_t) m].x);
        const float el = std::asin (juce::jlimit (-1.0f, 1.0f, virtualDirs[(size_t) m].z));
        std::vector<float> coeffs;
        shHelper.computeShCoefficients (az, el, coeffs);
        SphericalHarmonicsUtils::applyMaxReWeights (coeffs, reWeights);
        decodeMatrix[(size_t) m] = coeffs;
    }

    // Stage 2: one L/R HRIR pair per virtual speaker, convolution engines
    // prepared and loaded now (message thread, not real-time-safe) so
    // decode() itself never allocates.
    convolvers.clear();
    convolvers.reserve ((size_t) numVirtualSpeakers);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 }; // mono per engine -- see the class comment

    for (int m = 0; m < numVirtualSpeakers; ++m)
    {
        std::vector<float> left, right;
        float delayLeft = 0.0f, delayRight = 0.0f; // unused -- see HrtfDataset.h's own comment on why
        dataset.getFilter (virtualDirs[(size_t) m], left, right, delayLeft, delayRight);

        juce::AudioBuffer<float> irLeft (1, (int) left.size());
        irLeft.copyFrom (0, 0, left.data(), (int) left.size());
        juce::AudioBuffer<float> irRight (1, (int) right.size());
        irRight.copyFrom (0, 0, right.data(), (int) right.size());

        convolvers.push_back (std::make_unique<VirtualSpeakerConvolvers>());
        auto& pair = *convolvers.back();
        pair.left.prepare (spec);
        pair.right.prepare (spec);
        // Stereo::no (mono IR/engine, matches ProcessSpec's numChannels=1
        // above), Trim::no (keep the full measured impulse response,
        // accuracy over a few silent-sample savings), Normalise::no
        // (CRITICAL: per-direction level differences -- e.g. head-shadow
        // attenuation for a source behind the listener -- are physically
        // meaningful content, not something to auto-normalize away).
        pair.left.loadImpulseResponse (std::move (irLeft), sampleRate,
                                        juce::dsp::Convolution::Stereo::no,
                                        juce::dsp::Convolution::Trim::no,
                                        juce::dsp::Convolution::Normalise::no);
        pair.right.loadImpulseResponse (std::move (irRight), sampleRate,
                                         juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::no,
                                         juce::dsp::Convolution::Normalise::no);
        // loadImpulseResponse() itself loads asynchronously (see JUCE's
        // own doc comment on Convolution::prepare()) -- the prepare()
        // call just above/already-done guarantees the IR just loaded is
        // the one actually active for the next process() call, so no
        // extra synchronization is needed here.
    }

    virtualSpeakerScratch.setSize (1, maxBlockSize);
    earScratchL.setSize (1, maxBlockSize);
    earScratchR.setSize (1, maxBlockSize);
    prepared = true;
}

void BinauralDecoder::decode (const juce::AudioBuffer<float>& ambiInput, juce::AudioBuffer<float>& stereoOutput, int numSamples)
{
    stereoOutput.clear();

    if (! prepared)
        return; // see the class comment -- silence, not garbage

    const int numAmbiCh = (int) decodeMatrix[0].size();
    float* outL = stereoOutput.getWritePointer (0);
    float* outR = stereoOutput.getWritePointer (1);
    float* scratch = virtualSpeakerScratch.getWritePointer (0);

    for (int m = 0; m < numVirtualSpeakers; ++m)
    {
        // Stage 1: this virtual speaker's own row of the decode matrix
        // applied to the incoming B-format bus -> one mono signal.
        std::fill (scratch, scratch + numSamples, 0.0f);
        for (int c = 0; c < numAmbiCh; ++c)
        {
            const float coeff = decodeMatrix[(size_t) m][(size_t) c];
            if (coeff == 0.0f)
                continue;
            const float* src = ambiInput.getReadPointer (c);
            for (int i = 0; i < numSamples; ++i)
                scratch[i] += coeff * src[i];
        }

        // Stage 2: convolve through this speaker's own L/R HRIR pair,
        // accumulate into the stereo output. Separate copies per ear --
        // see the header's own comment on why (ProcessContextReplacing
        // processes in place, and the same mono signal feeds two
        // different filters).
        auto& pair = *convolvers[(size_t) m];

        earScratchL.copyFrom (0, 0, virtualSpeakerScratch, 0, 0, numSamples);
        juce::dsp::AudioBlock<float> fullBlockL (earScratchL);
        auto blockL = fullBlockL.getSubBlock (0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctxL (blockL);
        pair.left.process (ctxL);
        const float* filteredL = earScratchL.getReadPointer (0);
        for (int i = 0; i < numSamples; ++i)
            outL[i] += filteredL[i];

        earScratchR.copyFrom (0, 0, virtualSpeakerScratch, 0, 0, numSamples);
        juce::dsp::AudioBlock<float> fullBlockR (earScratchR);
        auto blockR = fullBlockR.getSubBlock (0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctxR (blockR);
        pair.right.process (ctxR);
        const float* filteredR = earScratchR.getReadPointer (0);
        for (int i = 0; i < numSamples; ++i)
            outR[i] += filteredR[i];
    }
}
