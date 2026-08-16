#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::AudioProcessor::BusesProperties makeBusLayout()
    {
        return juce::AudioProcessor::BusesProperties()
            .withInput  ("Live Inputs", juce::AudioChannelSet::discreteChannels (SAPOC_MAX_LIVE_INPUTS), true)
            .withOutput ("Ambisonics", juce::AudioChannelSet::discreteChannels ((SAPOC_DEFAULT_AMBI_ORDER + 1) * (SAPOC_DEFAULT_AMBI_ORDER + 1)), true);
    }

    // Kartesisch (x=vorne, y=links, z=oben) -> Kugelkoordinaten fuer den Encoder.
    void cartesianToSpherical (juce::Vector3D<float> pos, float& azimuth, float& elevation, float& distance)
    {
        distance = juce::jmax (pos.length(), 0.001f);
        azimuth  = std::atan2 (pos.y, pos.x);
        elevation = std::asin (juce::jlimit (-1.0f, 1.0f, pos.z / distance));
    }
}

SpatialAudioPOCProcessor::SpatialAudioPOCProcessor()
    : juce::AudioProcessor (makeBusLayout())
{
    encoder.setOrder (SAPOC_DEFAULT_AMBI_ORDER);

    // Standard-Zuordnung: Input-Kanal i -> Objekt i, sinnvoller Default fuer
    // den POC. Kann spaeter per GUI/InputMapper frei umgemappt werden.
    for (int i = 0; i < numLiveInputs; ++i)
    {
        auto& obj = trajectoryEngine.getObject (i);
        obj.inputChannel = i;
        // Objekte im Kreis um den Ursprung verteilen, damit sie beim
        // ersten Start nicht alle uebereinander liegen.
        const float angle = juce::MathConstants<float>::twoPi * (float) i / (float) numLiveInputs;
        obj.position = { std::cos (angle), std::sin (angle), 0.0f };
    }

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
    // Fuer den POC: feste Layouts wie in makeBusLayout() definiert.
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::discreteChannels (numLiveInputs)
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::discreteChannels (encoder.getNumChannels());
}

void SpatialAudioPOCProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInCh    = juce::jmin (numLiveInputs, buffer.getNumChannels());

    // Eingangskanaele vor dem Ueberschreiben sichern -- Output-Buffer ist
    // derselbe Speicher wie Input (in-place), daher zuerst kopieren.
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

void SpatialAudioPOCProcessor::getStateInformation (juce::MemoryBlock&) { /* TODO: Objekt-/Trajektorien-Presets speichern */ }
void SpatialAudioPOCProcessor::setStateInformation (const void*, int)   { /* TODO */ }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpatialAudioPOCProcessor();
}
