#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SoundObject.h"
#include "TrajectoryEngine.h"
#include "AmbisonicsEncoder.h"

/**
    Input:  N mono channels (N = SAPOC_MAX_LIVE_INPUTS, configurable), each
            assigned to one SoundObject.
    Output: Ambisonics B-format, channel count = (order+1)^2, order
            currently set via the SAPOC_DEFAULT_AMBI_ORDER constant
            (runtime switching is prepared, see AmbisonicsEncoder::setOrder(),
            but bus size is fixed per instance in the plugin context -- for
            runtime order changes, the standalone case is easier since no
            host bus contract exists there).

    No decoding here -- output is emitted as raw B-format and processed
    further in a DAW/with external tools (SPARTA, IEM Suite).
*/
class SpatialAudioPOCProcessor : public juce::AudioProcessor
{
public:
    SpatialAudioPOCProcessor();
    ~SpatialAudioPOCProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Spatial Audio POC"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Access for the editor (GUI reads/writes directly on the engine)
    TrajectoryEngine& getTrajectoryEngine() { return trajectoryEngine; }
    int getNumLiveInputs() const { return numLiveInputs; }

private:
    // BusesProperties is a protected nested type of juce::AudioProcessor --
    // only constructible via a method of the derived class, not via a free
    // function.
    static BusesProperties makeBusLayout();

    static constexpr int numLiveInputs = SAPOC_MAX_LIVE_INPUTS;

    TrajectoryEngine trajectoryEngine { numLiveInputs };
    AmbisonicsEncoder encoder;

    // Per-object persistent gain state for zipper-free ramping
    std::vector<std::vector<float>> previousGainsPerObject;

    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialAudioPOCProcessor)
};
