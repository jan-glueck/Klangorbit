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
    outputGain = 1.0f;

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

    // Calibration prep: this decode matrix's own gain, per virtual speaker,
    // toward a spread of test source directions -- computed now (decodeMatrix
    // is already final) so the stage-2 loop below can accumulate each
    // virtual speaker's IR contribution into a combined per-test-direction
    // impulse response as it goes, without a second dataset.getFilter() pass.
    // See calibrateOutputGain()'s own comment for what this feeds into.
    constexpr int numCalibrationDirs = 32;
    const auto calibrationDirs = SphericalHarmonicsUtils::fibonacciSphere (numCalibrationDirs);
    std::vector<std::vector<float>> calibrationGains (numCalibrationDirs, std::vector<float> ((size_t) numVirtualSpeakers, 0.0f)); // [dirIdx][virtualIdx]
    for (int d = 0; d < numCalibrationDirs; ++d)
    {
        const float az = std::atan2 (calibrationDirs[(size_t) d].y, calibrationDirs[(size_t) d].x);
        const float el = std::asin (juce::jlimit (-1.0f, 1.0f, calibrationDirs[(size_t) d].z));
        std::vector<float> coeffs;
        shHelper.computeShCoefficients (az, el, coeffs);
        SphericalHarmonicsUtils::applyMaxReWeights (coeffs, reWeights);

        for (int m = 0; m < numVirtualSpeakers; ++m)
        {
            float gain = 0.0f;
            for (int c = 0; c < numAmbiCh; ++c)
                gain += decodeMatrix[(size_t) m][(size_t) c] * coeffs[(size_t) c];
            calibrationGains[(size_t) d][(size_t) m] = gain;
        }
    }
    const int filterLength = dataset.getFilterLength();
    std::vector<std::vector<double>> combinedLeft (numCalibrationDirs, std::vector<double> ((size_t) filterLength, 0.0));
    std::vector<std::vector<double>> combinedRight (numCalibrationDirs, std::vector<double> ((size_t) filterLength, 0.0));

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

        // Accumulate this virtual speaker's own IR, scaled by its
        // per-test-direction decode gain, into that direction's combined
        // impulse response -- see calibrateOutputGain()'s comment.
        for (int d = 0; d < numCalibrationDirs; ++d)
        {
            const double gain = (double) calibrationGains[(size_t) d][(size_t) m];
            if (gain == 0.0)
                continue;
            auto& cl = combinedLeft[(size_t) d];
            auto& cr = combinedRight[(size_t) d];
            for (int i = 0; i < filterLength && i < (int) left.size(); ++i)
                cl[(size_t) i] += gain * (double) left[(size_t) i];
            for (int i = 0; i < filterLength && i < (int) right.size(); ++i)
                cr[(size_t) i] += gain * (double) right[(size_t) i];
        }

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
        // meaningful content, not something to auto-normalize away; the
        // SUM's overall output level is instead calibrated separately, see
        // calibrateOutputGain() below and outputGain's own comment in the
        // header).
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

    outputGain = calibrateOutputGain (combinedLeft, combinedRight);

    virtualSpeakerScratch.setSize (1, maxBlockSize);
    earScratchL.setSize (1, maxBlockSize);
    earScratchR.setSize (1, maxBlockSize);
    prepared = true;
}

float BinauralDecoder::calibrateOutputGain (const std::vector<std::vector<double>>& combinedLeft,
                                             const std::vector<std::vector<double>>& combinedRight)
{
    // Practical loudness calibration, same recipe as
    // AmbisonicsDecoder::calibrateDecodeMatrix() (see its own comment):
    // measure this decoder's total two-ear output energy for a spread of
    // test source directions (the combined per-direction impulse responses
    // built alongside the convolvers in prepare(), above) and scale by one
    // constant so the average comes out to roughly unit energy -- simpler
    // and more robust than an analytical derivation that would have to
    // account for numVirtualSpeakers, the max-rE taper, AND whatever
    // absolute measurement level a given HRTF dataset happens to have
    // baked in (which genuinely differs between real measured datasets,
    // e.g. SADIE II vs KEMAR -- reported as "Binaural is louder than
    // Stereo, and SADIE II louder still").
    double totalEnergy = 0.0;
    const size_t numDirs = combinedLeft.size();
    for (size_t d = 0; d < numDirs; ++d)
    {
        double energy = 0.0;
        for (double v : combinedLeft[d]) energy += v * v;
        for (double v : combinedRight[d]) energy += v * v;
        totalEnergy += energy;
    }

    const double avgEnergy = totalEnergy / (double) juce::jmax ((size_t) 1, numDirs);
    if (avgEnergy < 1.0e-12)
        return 1.0f; // degenerate (shouldn't happen for a real dataset) -- leave uncalibrated rather than divide by ~0

    return (float) (1.0 / std::sqrt (avgEnergy));
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

    // Practical loudness calibration -- see outputGain's own comment in the
    // header and calibrateOutputGain()'s comment in prepare(), above.
    for (int i = 0; i < numSamples; ++i)
    {
        outL[i] *= outputGain;
        outR[i] *= outputGain;
    }
}
