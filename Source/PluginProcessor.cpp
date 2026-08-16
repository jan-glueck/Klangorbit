#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Cartesian (x=front, y=left, z=up) -> spherical coordinates for the encoder.
    void cartesianToSpherical (Vec3 pos, float& azimuth, float& elevation, float& distance)
    {
        distance = juce::jmax (pos.length(), 0.001f);
        azimuth  = std::atan2 (pos.y, pos.x);
        elevation = std::asin (juce::jlimit (-1.0f, 1.0f, pos.z / distance));
    }
}

SpatialAudioPOCProcessor::BusesProperties SpatialAudioPOCProcessor::makeBusLayout()
{
    return BusesProperties()
        .withInput  ("Live Inputs", juce::AudioChannelSet::discreteChannels (SAPOC_MAX_LIVE_INPUTS), true)
        .withOutput ("Ambisonics", juce::AudioChannelSet::discreteChannels ((SAPOC_DEFAULT_AMBI_ORDER + 1) * (SAPOC_DEFAULT_AMBI_ORDER + 1)), true);
}

SpatialAudioPOCProcessor::SpatialAudioPOCProcessor()
    : juce::AudioProcessor (makeBusLayout())
{
    encoder.setOrder (SAPOC_DEFAULT_AMBI_ORDER);

    // Only object 0 starts active (input channel 0). Further objects are
    // added via the GUI (TrajectoryEngine::activateObject()) -- the input
    // channel assignment stays the simple 1:1 mapping of object index ==
    // channel index.
    trajectoryEngine.activateObject (0);

    previousGainsPerObject.resize ((size_t) numLiveInputs);
    for (auto& g : previousGainsPerObject)
        g.assign ((size_t) encoder.getNumChannels(), 0.0f);
}

SpatialAudioPOCProcessor::~SpatialAudioPOCProcessor() = default;

void SpatialAudioPOCProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    encoder.prepare (sampleRate, samplesPerBlock);

    for (auto& g : previousGainsPerObject)
        g.assign ((size_t) encoder.getNumChannels(), 0.0f);
}

void SpatialAudioPOCProcessor::releaseResources() {}

bool SpatialAudioPOCProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // For the POC: fixed layouts as defined in makeBusLayout().
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::discreteChannels (numLiveInputs)
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::discreteChannels (encoder.getNumChannels());
}

void SpatialAudioPOCProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInCh    = juce::jmin (numLiveInputs, buffer.getNumChannels());

    // Preserve the input channels before overwriting -- the output buffer
    // is the same memory as the input (in-place), so copy first.
    juce::AudioBuffer<float> inputCopy (numInCh, numSamples);
    for (int ch = 0; ch < numInCh; ++ch)
        inputCopy.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    buffer.clear();

    std::vector<TrajectoryEngine::Snapshot> snapshot;
    trajectoryEngine.getSnapshot (snapshot);

    for (int objIdx = 0; objIdx < trajectoryEngine.getNumObjects(); ++objIdx)
    {
        auto& obj = trajectoryEngine.getObject (objIdx);
        if (obj.inputChannel < 0 || obj.inputChannel >= numInCh)
            continue;

        if (objIdx >= (int) snapshot.size() || ! snapshot[(size_t) objIdx].active)
            continue;

        float azimuth, elevation, distance;
        cartesianToSpherical (snapshot[(size_t) objIdx].position, azimuth, elevation, distance);

        encoder.encodeBlock (inputCopy.getReadPointer (obj.inputChannel),
                              numSamples,
                              azimuth, elevation, distance,
                              obj.gain,
                              buffer,
                              previousGainsPerObject[(size_t) objIdx]);
    }
}

juce::AudioProcessorEditor* SpatialAudioPOCProcessor::createEditor()
{
    return new SpatialAudioPOCEditor (*this);
}

void SpatialAudioPOCProcessor::getStateInformation (juce::MemoryBlock&) { /* TODO: save object/trajectory presets */ }
void SpatialAudioPOCProcessor::setStateInformation (const void*, int)   { /* TODO */ }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpatialAudioPOCProcessor();
}
