#include "AmbisonicsDecoder.h"
#include "SpeakerLayouts.h"
#include "SphericalHarmonicsUtils.h"
#include "VBAP.h"
#include <cmath>

namespace
{
    // Maps a decoder Mode to its speaker table -- SpeakerLayouts.h's own
    // entries are ordered to match juce::AudioChannelSet's internal
    // channel order for the corresponding named layout EXACTLY (verified
    // against JUCE/modules/juce_audio_basics/buffers/juce_AudioChannelSet.cpp),
    // so decodeMatrix's row order can be used directly as the output
    // buffer's channel order with no remapping step.
    std::vector<SpeakerLayouts::Speaker> speakersFor (AmbisonicsDecoder::Mode mode)
    {
        using Mode = AmbisonicsDecoder::Mode;
        switch (mode)
        {
            case Mode::Quad:         return SpeakerLayouts::quad();
            case Mode::Surround5_1:  return SpeakerLayouts::surround5point1();
            case Mode::Surround7_1:  return SpeakerLayouts::surround7point1();
            case Mode::Atmos5_1_2:   return SpeakerLayouts::atmos5point1point2();
            case Mode::Atmos5_1_4:   return SpeakerLayouts::atmos5point1point4();
            case Mode::Atmos7_1_2:   return SpeakerLayouts::atmos7point1point2();
            case Mode::Atmos7_1_4:   return SpeakerLayouts::atmos7point1point4();
            // Raw/Stereo/Binaural/circular modes have no table here -- raw
            // isn't decoded at all; Stereo uses stereoPair() directly in
            // buildStereoMatrix(); Binaural never calls into this class's
            // own decode machinery at all (see the class comment);
            // Octophonic/CircularArray use their own tables directly in
            // buildCircularMatrix() (mode-matching, not AllRAD -- see the
            // class comment) -- none of them go through this AllRAD-only
            // helper.
            case Mode::AmbisonicsRawOrder1:
            case Mode::AmbisonicsRawOrder2:
            case Mode::AmbisonicsRawOrder3:
            case Mode::Stereo:
            case Mode::Binaural:
            case Mode::Octophonic:
            case Mode::CircularArray: return {};
        }
        return {};
    }

    // fibonacciSphere()/maxReWeights()/applyMaxReWeights() used to live
    // here -- moved to SphericalHarmonicsUtils.h so BinauralDecoder can
    // reuse the identical virtual-array point distribution and decode
    // weighting instead of a second, easy-to-drift-out-of-sync copy of
    // the same math (see that header's own class comment).
}

int AmbisonicsDecoder::numOutputChannels (Mode mode, int circularSpeakerCount)
{
    switch (mode)
    {
        case Mode::AmbisonicsRawOrder1: return 4;
        case Mode::AmbisonicsRawOrder2: return 9;
        case Mode::AmbisonicsRawOrder3: return 16;
        case Mode::Stereo:              return 2;
        case Mode::Binaural:            return 2;
        case Mode::Quad:                return 4;
        case Mode::Surround5_1:         return 6;
        case Mode::Surround7_1:         return 8;
        case Mode::Atmos5_1_2:          return 8;
        case Mode::Atmos5_1_4:          return 10;
        case Mode::Atmos7_1_2:          return 10;
        case Mode::Atmos7_1_4:          return 12;
        case Mode::Octophonic:          return 8;
        case Mode::CircularArray:       return juce::jlimit (minCircularSpeakers, maxCircularSpeakers, circularSpeakerCount);
    }
    return 2;
}

int AmbisonicsDecoder::ambisonicsOrderFor (Mode mode)
{
    switch (mode)
    {
        case Mode::AmbisonicsRawOrder1: return 1;
        case Mode::AmbisonicsRawOrder2: return 2;
        case Mode::AmbisonicsRawOrder3: return 3;
        // Every decoded mode: always encode at the internal maximum for
        // best decode quality, regardless of the target speaker count.
        case Mode::Stereo:
        case Mode::Binaural:
        case Mode::Quad:
        case Mode::Surround5_1:
        case Mode::Surround7_1:
        case Mode::Atmos5_1_2:
        case Mode::Atmos5_1_4:
        case Mode::Atmos7_1_2:
        case Mode::Atmos7_1_4:
        case Mode::Octophonic:
        case Mode::CircularArray:       return 3;
    }
    return 3;
}

juce::AudioChannelSet AmbisonicsDecoder::outputChannelSetFor (Mode mode, int circularSpeakerCount)
{
    switch (mode)
    {
        // discreteChannels(), NOT the named ambisonic() set -- this matches
        // the plugin's pre-existing raw-Ambisonics output bus declaration
        // exactly (see PluginProcessor's original makeBusLayout()), which
        // real user setups (Reaper routing, Max/MSP vst~/mcs.vst~ channel
        // counts) are already built around. Switching to the named set
        // would have the same channel count but a different AudioChannelSet
        // identity, breaking isBusesLayoutSupported() for those existing
        // sessions.
        case Mode::AmbisonicsRawOrder1: return juce::AudioChannelSet::discreteChannels (4);
        case Mode::AmbisonicsRawOrder2: return juce::AudioChannelSet::discreteChannels (9);
        case Mode::AmbisonicsRawOrder3: return juce::AudioChannelSet::discreteChannels (16);
        case Mode::Stereo:              return juce::AudioChannelSet::stereo();
        // Binaural also declares a plain stereo() bus -- deliberately the
        // SAME identity as Mode::Stereo (both are, from the host's
        // perspective, "this plugin outputs 2 channels"; which one is
        // actually active is this plugin's own Output Format state, not
        // something the bus layout itself needs to distinguish -- same
        // reasoning as the existing Octophonic/CircularArray(8) overlap
        // noted above). Safe post the isBusesLayoutSupported() fix: only
        // the current mode's own layout is ever accepted, so two modes
        // sharing one layout can't cause host-negotiation ambiguity.
        case Mode::Binaural:            return juce::AudioChannelSet::stereo();
        case Mode::Quad:                return juce::AudioChannelSet::quadraphonic();
        case Mode::Surround5_1:         return juce::AudioChannelSet::create5point1();
        case Mode::Surround7_1:         return juce::AudioChannelSet::create7point1();
        case Mode::Atmos5_1_2:          return juce::AudioChannelSet::create5point1point2();
        case Mode::Atmos5_1_4:          return juce::AudioChannelSet::create5point1point4();
        case Mode::Atmos7_1_2:          return juce::AudioChannelSet::create7point1point2();
        case Mode::Atmos7_1_4:          return juce::AudioChannelSet::create7point1point4();
        // No JUCE-named "regular circular array" layout exists -- both use
        // discreteChannels(), see this method's own comment on the
        // resulting (accepted) bus-layout overlap between the two.
        case Mode::Octophonic:          return juce::AudioChannelSet::discreteChannels (8);
        case Mode::CircularArray:       return juce::AudioChannelSet::discreteChannels (
                                             juce::jlimit (minCircularSpeakers, maxCircularSpeakers, circularSpeakerCount));
    }
    return juce::AudioChannelSet::stereo();
}

bool AmbisonicsDecoder::isRawPassthrough (Mode mode)
{
    return mode == Mode::AmbisonicsRawOrder1 || mode == Mode::AmbisonicsRawOrder2 || mode == Mode::AmbisonicsRawOrder3;
}

int AmbisonicsDecoder::lfeChannelIndexFor (Mode mode)
{
    const auto speakers = speakersFor (mode);
    for (int i = 0; i < (int) speakers.size(); ++i)
        if (speakers[(size_t) i].isLfe)
            return i;
    return -1;
}

void AmbisonicsDecoder::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    lfeFilterState = 0.0f;
}

void AmbisonicsDecoder::setMode (Mode newMode)
{
    mode = newMode;
    lfeFilterState = 0.0f; // don't carry a stale filter state into a differently-scaled signal

    if (isRawPassthrough (mode))
    {
        decodeMatrix.clear(); // unused in this mode -- see the class comment
        return;
    }

    if (mode == Mode::Binaural)
    {
        // No decode matrix here -- see the class comment. The caller
        // (KlangorbitProcessor) is responsible for calling
        // BinauralDecoder::prepare() when Binaural becomes active
        // (equivalent role to this method for every other decoded mode).
        decodeMatrix.clear();
        return;
    }

    if (mode == Mode::Stereo)
        buildStereoMatrix();
    else if (mode == Mode::Octophonic || mode == Mode::CircularArray)
        buildCircularMatrix (mode);
    else
        buildAllRadMatrix (mode);
}

void AmbisonicsDecoder::setCircularArraySpeakerCount (int n)
{
    const int clamped = juce::jlimit (minCircularSpeakers, maxCircularSpeakers, n);
    if (clamped == circularSpeakerCount)
        return;

    circularSpeakerCount = clamped;
    if (mode == Mode::CircularArray)
        buildCircularMatrix (mode); // take effect immediately if already active
}

void AmbisonicsDecoder::buildStereoMatrix()
{
    // Plain 2-point Ambisonics decode (SH sampling decoder), NOT AllRAD --
    // two points don't benefit from AllRAD's virtual-array/VBAP-remap
    // machinery (there's no "irregular array coloration" problem to solve
    // with only 2, symmetric speakers), and NOT HRTF-based either (see
    // the class comment on why Stereo and Binaural are separate).
    AmbisonicsEncoder shHelper;
    shHelper.setOrder (3);
    const int numAmbiCh = shHelper.getNumChannels();

    const auto pair = SpeakerLayouts::stereoPair();
    decodeMatrix.assign (2, std::vector<float> ((size_t) numAmbiCh, 0.0f));

    for (int spk = 0; spk < 2; ++spk)
    {
        const auto& dir = pair[(size_t) spk].direction;
        const float az = std::atan2 (dir.y, dir.x);
        const float el = std::asin (juce::jlimit (-1.0f, 1.0f, dir.z));

        std::vector<float> coeffs;
        shHelper.computeShCoefficients (az, el, coeffs);
        decodeMatrix[(size_t) spk] = coeffs;
    }

    calibrateDecodeMatrix (numAmbiCh);
}

void AmbisonicsDecoder::buildCircularMatrix (Mode targetMode)
{
    // Plain mode-matching decode (a direct SH-sampling decode straight to
    // the real speakers), deliberately NOT AllRAD/VBAP -- see the class
    // comment for the reasoning. AllRAD's virtual-array-plus-remap step
    // exists to avoid coloration on an IRREGULAR array; Octophonic and
    // CircularArray are, by construction, perfectly regular (evenly
    // spaced on a horizontal ring), so there is no irregularity for
    // AllRAD's extra machinery to correct for, and a plain sampling
    // decode straight to the real array is standard practice for a
    // regular array (equivalent to how buildStereoMatrix() already
    // handles the 2-speaker case above, generalized to N).
    //
    // Alias-free note: an N-speaker regular ring can exactly represent
    // circular (horizontal) Ambisonics content up to order floor((N-1)/2)
    // -- a standard result from circular-harmonic sampling theory, the
    // same reasoning behind AllRAD's own virtual-array density choices.
    // The encoder here is always fixed at order 3 for every decoded mode
    // (see ambisonicsOrderFor()), which needs N >= 7 for a fully
    // alias-free horizontal decode. Octophonic (8) clears that. A small
    // CircularArray (4/5/6 speakers) will still decode correctly overall,
    // just with a bit more spatial "blur" than a wider array would give
    // for the same order-3 source content -- an inherent property of a
    // small ring, not a decoder bug (documented in the UI/README).
    AmbisonicsEncoder shHelper;
    shHelper.setOrder (3);
    const int numAmbiCh = shHelper.getNumChannels();

    const auto speakers = (targetMode == Mode::Octophonic)
        ? SpeakerLayouts::octophonic()
        : SpeakerLayouts::circularArray (circularSpeakerCount, 0.0f); // 0 deg start = front, see circularArray()'s own comment

    const int numCh = (int) speakers.size();
    decodeMatrix.assign ((size_t) numCh, std::vector<float> ((size_t) numAmbiCh, 0.0f));

    for (int spk = 0; spk < numCh; ++spk)
    {
        const auto& dir = speakers[(size_t) spk].direction;
        const float az = std::atan2 (dir.y, dir.x); // elevation is always 0 -- horizontal-only array, see the class comment
        std::vector<float> coeffs;
        shHelper.computeShCoefficients (az, 0.0f, coeffs);
        decodeMatrix[(size_t) spk] = coeffs;
    }

    calibrateDecodeMatrix (numAmbiCh);
}

void AmbisonicsDecoder::buildAllRadMatrix (Mode targetMode)
{
    AmbisonicsEncoder shHelper;
    shHelper.setOrder (3);
    const int numAmbiCh = shHelper.getNumChannels();

    const auto realSpeakers = speakersFor (targetMode);
    const int numRealCh = (int) realSpeakers.size();

    // LFE-free direction list for the VBAP triangulation/remap -- LFE
    // carries no directional content (see this class's own comment and
    // decode()'s bass-management handling), so it must never participate
    // in panning geometry.
    std::vector<Vec3> realDirs;
    std::vector<int> realDirToFullIndex; // realDirs[i] came from realSpeakers[realDirToFullIndex[i]]
    for (int i = 0; i < numRealCh; ++i)
    {
        if (realSpeakers[(size_t) i].isLfe) continue;
        realDirs.push_back (realSpeakers[(size_t) i].direction);
        realDirToFullIndex.push_back (i);
    }

    constexpr int numVirtual = 50;
    const auto virtualDirs = SphericalHarmonicsUtils::fibonacciSphere (numVirtual);

    // Ambisonics -> virtual array: a max-rE-weighted SH sampling decode
    // (decode coefficients == encode coefficients for a real SH basis,
    // reusing AmbisonicsEncoder rather than re-deriving/duplicating the
    // same associated-Legendre-polynomial math -- the max-rE taper is
    // applied on top, see maxReWeights()'s own comment on why it's
    // required for this two-stage decode to stay directional at all).
    const auto reWeights = SphericalHarmonicsUtils::maxReWeights (shHelper.getOrder());
    std::vector<std::vector<float>> dVirtual (numVirtual, std::vector<float> ((size_t) numAmbiCh, 0.0f));
    for (int m = 0; m < numVirtual; ++m)
    {
        const float az = std::atan2 (virtualDirs[(size_t) m].y, virtualDirs[(size_t) m].x);
        const float el = std::asin (juce::jlimit (-1.0f, 1.0f, virtualDirs[(size_t) m].z));
        std::vector<float> coeffs;
        shHelper.computeShCoefficients (az, el, coeffs);
        SphericalHarmonicsUtils::applyMaxReWeights (coeffs, reWeights);
        dVirtual[(size_t) m] = coeffs;
    }

    // Virtual array -> real (LFE-free) speakers, via VBAP.
    const auto regions = VBAP::triangulate (realDirs);
    const int numRealDirs = (int) realDirs.size();
    std::vector<std::vector<float>> gVbap ((size_t) numRealDirs, std::vector<float> ((size_t) numVirtual, 0.0f)); // [realDirIdx][virtualIdx]
    for (int m = 0; m < numVirtual; ++m)
    {
        const auto gains = VBAP::computeGains (virtualDirs[(size_t) m], realDirs, regions);
        for (int r = 0; r < numRealDirs; ++r)
            gVbap[(size_t) r][(size_t) m] = gains[(size_t) r];
    }

    // Combine into one ambisonics -> real-speakers matrix: D_AllRAD = G_vbap * D_virtual.
    decodeMatrix.assign ((size_t) numRealCh, std::vector<float> ((size_t) numAmbiCh, 0.0f)); // LFE row(s), if any, stay all-zero
    for (int r = 0; r < numRealDirs; ++r)
    {
        const int fullIdx = realDirToFullIndex[(size_t) r];
        for (int c = 0; c < numAmbiCh; ++c)
        {
            float sum = 0.0f;
            for (int m = 0; m < numVirtual; ++m)
                sum += gVbap[(size_t) r][(size_t) m] * dVirtual[(size_t) m][(size_t) c];
            decodeMatrix[(size_t) fullIdx][(size_t) c] = sum;
        }
    }

    // Per-speaker density compensation: the ACN-0 (W/omni) coefficient is
    // direction-independent (identically 1.0 for any source direction), so
    // its row-0 contribution above is effectively "how much of the dense,
    // uniform virtual array's total surface this real speaker's VBAP
    // regions cover" -- which is uneven for an irregular layout (e.g. in
    // 5.1, C is flanked closely by L/R on both sides, so it structurally
    // covers a much narrower azimuth span than L or R do, even though nothing
    // about the actual source direction favors L/R). Left uncorrected, that
    // coverage imbalance dominates the whole decode (the constant omni term
    // swamps the comparatively small direction-dependent terms once summed
    // over ~50 virtual points), making the decoder barely directional at
    // all -- confirmed empirically while building this class. Dividing each
    // real speaker's row by its own omni response equalizes that coverage
    // bias while preserving the row's relative directional shape, which is
    // what actually carries the panning information.
    for (int r = 0; r < numRealDirs; ++r)
    {
        const int fullIdx = realDirToFullIndex[(size_t) r];
        const float omniResponse = decodeMatrix[(size_t) fullIdx][0];
        if (omniResponse > 1.0e-6f)
            for (auto& v : decodeMatrix[(size_t) fullIdx])
                v /= omniResponse;
    }

    calibrateDecodeMatrix (numAmbiCh);
}

void AmbisonicsDecoder::calibrateDecodeMatrix (int numAmbiCh)
{
    // Practical loudness calibration, not a strict SAD-theory derivation:
    // encode test plane waves from many directions, measure this matrix's
    // total output energy for each, and scale the whole matrix by one
    // constant so the AVERAGE comes out to roughly unit energy. Simpler
    // and more robust than getting an exact analytical normalization
    // constant right for every combination of Ambisonics order/virtual
    // array density/real speaker count -- matches this project's existing
    // "practical approximation, documented as such" approach elsewhere
    // (e.g. the simplified air-absorption model in PropagationProcessor).
    AmbisonicsEncoder shHelper;
    shHelper.setOrder (3);

    const auto testDirs = SphericalHarmonicsUtils::fibonacciSphere (32);
    double totalEnergy = 0.0;

    for (auto& dir : testDirs)
    {
        const float az = std::atan2 (dir.y, dir.x);
        const float el = std::asin (juce::jlimit (-1.0f, 1.0f, dir.z));
        std::vector<float> coeffs;
        shHelper.computeShCoefficients (az, el, coeffs);

        for (auto& row : decodeMatrix)
        {
            float out = 0.0f;
            for (int c = 0; c < numAmbiCh; ++c)
                out += row[(size_t) c] * coeffs[(size_t) c];
            totalEnergy += (double) (out * out);
        }
    }

    const double avgEnergy = totalEnergy / (double) testDirs.size();
    if (avgEnergy < 1.0e-9)
        return; // degenerate (shouldn't happen for a real layout) -- leave the matrix as-is rather than divide by ~0

    const float scale = (float) (1.0 / std::sqrt (avgEnergy));
    for (auto& row : decodeMatrix)
        for (auto& v : row)
            v *= scale;
}

void AmbisonicsDecoder::decode (const juce::AudioBuffer<float>& ambiBuffer, juce::AudioBuffer<float>& destBuffer, int numSamples)
{
    if (decodeMatrix.empty())
        return; // raw passthrough mode -- caller shouldn't be calling decode() at all, see the class comment

    // Clamped defensively to whatever buffers were actually handed in, not
    // just this mode's nominal channel counts: a host that hasn't yet
    // renegotiated its bus layout after a mode switch (see
    // PluginProcessor::setDecoderMode()'s own comment on that being
    // best-effort/host-dependent) could still call this with a buffer
    // sized for the PREVIOUS mode for a block or two -- writing/reading
    // out of bounds in that window would be a real crash, not just a
    // glitch.
    const int numOut = juce::jmin ((int) decodeMatrix.size(), destBuffer.getNumChannels());
    const int numAmbiCh = juce::jmin ((int) decodeMatrix[0].size(), ambiBuffer.getNumChannels());
    if (numOut <= 0 || numAmbiCh <= 0)
        return;

    for (int o = 0; o < numOut; ++o)
    {
        float* dst = destBuffer.getWritePointer (o);
        std::fill (dst, dst + numSamples, 0.0f);

        for (int c = 0; c < numAmbiCh; ++c)
        {
            const float coeff = decodeMatrix[(size_t) o][(size_t) c];
            if (coeff == 0.0f)
                continue;
            const float* src = ambiBuffer.getReadPointer (c);
            for (int i = 0; i < numSamples; ++i)
                dst[i] += coeff * src[i];
        }
    }

    const int lfeIdx = lfeChannelIndexFor (mode);
    if (lfeIdx < 0 || lfeIdx >= destBuffer.getNumChannels())
        return;

    float* lfeDst = destBuffer.getWritePointer (lfeIdx);
    if (! bassManagementEnabled)
    {
        std::fill (lfeDst, lfeDst + numSamples, 0.0f); // decodeMatrix's LFE row is already all-zero, but stay explicit/defensive
        return;
    }

    // Simple one-pole low-pass of the W channel (ambisonics channel 0, the
    // omnidirectional pressure component) as a practical LFE proxy --
    // Ambisonics carries no dedicated LFE signal of its own, W is the
    // closest thing to "the overall signal level" to derive one from.
    // Cutoff ~120Hz, a conventional subwoofer crossover point. Same
    // one-pole exp(-2*pi*fc/fs) idiom as PropagationProcessor's own
    // air-absorption filter and SoundObject::orbitRadiusNoiseSmoothing.
    const float* w = ambiBuffer.getReadPointer (0);
    const float coeff = std::exp (-2.0f * juce::MathConstants<float>::pi * 120.0f / (float) sampleRate);
    for (int i = 0; i < numSamples; ++i)
    {
        lfeFilterState = coeff * lfeFilterState + (1.0f - coeff) * w[i];
        lfeDst[i] = lfeFilterState;
    }
}
