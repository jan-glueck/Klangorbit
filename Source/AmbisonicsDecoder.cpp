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
    // so this order can be used directly as the output buffer's channel
    // order with no remapping step. Used by AmbisonicsDecoder::
    // setupDirectPan() (for the 7 modes with a real SpeakerLayouts table)
    // and by lfeChannelIndexFor(); Octophonic/CircularArray build their own
    // speaker list directly from SpeakerLayouts::octophonic()/
    // circularArray() instead (not in this table, see below), and
    // buildStereoMatrix() uses SpeakerLayouts::stereoPair() directly.
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
            case Mode::AmbisonicsRawOrder1:
            case Mode::AmbisonicsRawOrder2:
            case Mode::AmbisonicsRawOrder3:
            case Mode::AmbisonicsRawOrder4:
            case Mode::AmbisonicsRawOrder5:
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
        case Mode::AmbisonicsRawOrder4: return 25;
        case Mode::AmbisonicsRawOrder5: return 36;
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
        case Mode::AmbisonicsRawOrder4: return 4;
        case Mode::AmbisonicsRawOrder5: return 5;
        // Every decoded/direct-pan mode: always encode at the internal maximum for
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
        // AudioChannelSet::ambisonic(order) (ACN/SN3D), NOT discreteChannels()
        // -- this REPLACES an earlier discreteChannels()-based declaration
        // that matched the plugin's own original raw-Ambisonics bus (see
        // PluginProcessor's makeBusLayout()) but, confirmed by reading
        // JUCE's own VST3<->SpeakerArrangement conversion
        // (juce_VST3Common.h: getVst3SpeakerArrangement()/getChannelType()),
        // can NEVER actually be negotiated as a wider VST3 output bus: no
        // VST3 speaker bit exists for a generic "channel N" beyond
        // discreteChannel0 (kSpeakerM), so getBusArrangement() can't
        // report such a bus's layout to a host and no host can construct
        // a matching setBusArrangements() request for it either --
        // isBusesLayoutSupported() accepting this identity was therefore
        // never reachable via real VST3 bus negotiation to begin with (see
        // the CHANGELOG's "VST3 output stuck at Stereo..." follow-up
        // entry). AudioChannelSet::ambisonic(order) IS representable:
        // orders 1-4 round-trip via individual per-channel ACN speaker
        // bits (kSpeakerACN0..24, all individually defined), and order 5
        // via VST3's own dedicated kAmbi5thOrderACN whole-bus arrangement
        // constant -- both confirmed present in JUCE's own conversion
        // tables. Channel COUNT is unaffected (numOutputChannels() returns
        // the same 4/9/16/25/36 regardless, independent of this method) --
        // only the bus's reported IDENTITY changes, so nothing that reads
        // audio by channel index is affected.
        case Mode::AmbisonicsRawOrder1: return juce::AudioChannelSet::ambisonic (1);
        case Mode::AmbisonicsRawOrder2: return juce::AudioChannelSet::ambisonic (2);
        case Mode::AmbisonicsRawOrder3: return juce::AudioChannelSet::ambisonic (3);
        case Mode::AmbisonicsRawOrder4: return juce::AudioChannelSet::ambisonic (4);
        case Mode::AmbisonicsRawOrder5: return juce::AudioChannelSet::ambisonic (5);
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
    return mode == Mode::AmbisonicsRawOrder1 || mode == Mode::AmbisonicsRawOrder2 || mode == Mode::AmbisonicsRawOrder3
        || mode == Mode::AmbisonicsRawOrder4 || mode == Mode::AmbisonicsRawOrder5;
}

bool AmbisonicsDecoder::usesDirectPan (Mode mode)
{
    switch (mode)
    {
        case Mode::Quad:
        case Mode::Octophonic:
        case Mode::CircularArray:
        case Mode::Surround5_1:
        case Mode::Surround7_1:
        case Mode::Atmos5_1_2:
        case Mode::Atmos5_1_4:
        case Mode::Atmos7_1_2:
        case Mode::Atmos7_1_4:
            return true;
        case Mode::Stereo:
        case Mode::Binaural:
        case Mode::AmbisonicsRawOrder1:
        case Mode::AmbisonicsRawOrder2:
        case Mode::AmbisonicsRawOrder3:
        case Mode::AmbisonicsRawOrder4:
        case Mode::AmbisonicsRawOrder5:
            return false;
    }
    return false;
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

    decodeMatrix.clear();
    setupDirectPan (newMode); // clears its own state (leaving computeDirectPanGains() correctly inert) unless usesDirectPan(newMode)

    // Raw passthrough, Binaural, and every direct-pan mode: decodeMatrix
    // stays empty (the caller must not call decode() for any of these --
    // see that method's own guard and the class comment). Binaural's
    // actual HRTF decode lives entirely in BinauralDecoder, prepared
    // separately by the caller (KlangorbitProcessor) when this mode
    // becomes active.
    if (isRawPassthrough (mode) || mode == Mode::Binaural || usesDirectPan (mode))
        return;

    if (mode == Mode::Stereo)
        buildStereoMatrix();
}

void AmbisonicsDecoder::setCircularArraySpeakerCount (int n)
{
    const int clamped = juce::jlimit (minCircularSpeakers, maxCircularSpeakers, n);
    if (clamped == circularSpeakerCount)
        return;

    circularSpeakerCount = clamped;
    if (mode == Mode::CircularArray)
        setupDirectPan (mode); // take effect immediately if already active
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

void AmbisonicsDecoder::setupDirectPan (Mode newMode)
{
    directPanRealDirs.clear();
    directPanRealDirToFullIndex.clear();
    directPanRegions.clear();
    directPanNumFullChannels = 0;

    if (! usesDirectPan (newMode))
        return;

    // Octophonic/CircularArray build their own speaker list directly from
    // SpeakerLayouts (neither is in speakersFor()'s own table -- see its
    // comment); every other direct-pan mode (Quad/Surround5_1/Surround7_1/
    // Atmos*) already has a real SpeakerLayouts table there.
    std::vector<SpeakerLayouts::Speaker> speakers;
    if (newMode == Mode::Octophonic)
        speakers = SpeakerLayouts::octophonic();
    else if (newMode == Mode::CircularArray)
        speakers = SpeakerLayouts::circularArray (circularSpeakerCount, 0.0f); // 0 deg start = front, see circularArray()'s own comment
    else
        speakers = speakersFor (newMode);

    directPanNumFullChannels = (int) speakers.size();

    // LFE-free direction list for the VBAP triangulation -- LFE carries no
    // directional content (see the class comment and applyLfeFilterDirect()),
    // so it must never participate in panning geometry. Same
    // realDirs/realDirToFullIndex shape the old buildAllRadMatrix() used.
    for (int i = 0; i < (int) speakers.size(); ++i)
    {
        if (speakers[(size_t) i].isLfe) continue;
        directPanRealDirs.push_back (speakers[(size_t) i].direction);
        directPanRealDirToFullIndex.push_back (i);
    }

    directPanRegions = VBAP::triangulate (directPanRealDirs);
}

std::vector<float> AmbisonicsDecoder::computeDirectPanGains (Vec3 direction) const
{
    std::vector<float> fullGains ((size_t) juce::jmax (0, directPanNumFullChannels), 0.0f);
    if (directPanRealDirs.empty())
        return fullGains; // ! usesDirectPan(getMode()) -- correctly inert, see this method's own header comment

    const float len = direction.length();
    const Vec3 unitDir = (len > 1.0e-6f) ? (direction / len) : Vec3 { 1.0f, 0.0f, 0.0f }; // degenerate zero-length input -- pick an arbitrary but stable direction rather than dividing by ~0

    const auto gains = VBAP::computeGains (unitDir, directPanRealDirs, directPanRegions);
    for (size_t i = 0; i < gains.size() && i < directPanRealDirToFullIndex.size(); ++i)
        fullGains[(size_t) directPanRealDirToFullIndex[i]] = gains[i];
    return fullGains;
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

    // Silence any destination channels beyond this mode's own numOut, up
    // to the buffer's actual full width -- normally a no-op (VST3/
    // Standalone always negotiate a bus that matches numOut exactly, see
    // KlangorbitProcessor::isBusesLayoutSupported()), but for AU the
    // negotiated bus can be WIDER than the currently active mode's own
    // channel count: Logic fixes the output channel count once, at
    // insertion, and switching to a mode needing FEWER channels works
    // within that fixed width rather than requesting a bus change (see
    // KlangorbitProcessor::setDecoderMode()'s own AU-specific comment).
    // Without this, those extra channels would keep whatever was last
    // written there -- destBuffer is JUCE's shared in-place input/output
    // buffer, not fresh memory.
    for (int o = numOut; o < destBuffer.getNumChannels(); ++o)
    {
        float* dst = destBuffer.getWritePointer (o);
        std::fill (dst, dst + numSamples, 0.0f);
    }

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
    // closest thing to "the overall signal level" to derive one from. See
    // applyLfeLowPass()/applyLfeFilterDirect() for the shared filter this
    // and the direct-pan modes' own LFE both go through.
    applyLfeLowPass (ambiBuffer.getReadPointer (0), lfeDst, numSamples);
}

void AmbisonicsDecoder::applyLfeLowPass (const float* monoInput, float* lfeOutput, int numSamples)
{
    // Cutoff ~120Hz, a conventional subwoofer crossover point. Same
    // one-pole exp(-2*pi*fc/fs) idiom as PropagationProcessor's own
    // air-absorption filter and SoundObject::orbitRadiusNoiseSmoothing.
    const float coeff = std::exp (-2.0f * juce::MathConstants<float>::pi * 120.0f / (float) sampleRate);
    for (int i = 0; i < numSamples; ++i)
    {
        lfeFilterState = coeff * lfeFilterState + (1.0f - coeff) * monoInput[i];
        lfeOutput[i] = lfeFilterState;
    }
}

void AmbisonicsDecoder::applyLfeFilterDirect (const float* monoInput, float* lfeOutput, int numSamples)
{
    // See this method's own header comment -- the usesDirectPan()-mode
    // equivalent of decode()'s own W-channel-derived LFE handling just
    // above, sharing the same filter/state, fed a dry per-object sum
    // (PluginProcessor's own lfeDrySumScratch) instead of the shared
    // Ambisonics bus's W channel (which these modes never populate).
    if (! bassManagementEnabled)
    {
        std::fill (lfeOutput, lfeOutput + numSamples, 0.0f);
        return;
    }

    applyLfeLowPass (monoInput, lfeOutput, numSamples);
}
