#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SoundObject.h"
#include "TrajectoryEngine.h"
#include "AmbisonicsEncoder.h"

/**
    Input:  N Mono-Kanaele (N = SAPOC_MAX_LIVE_INPUTS, konfigurierbar), je
            einem SoundObject zugeordnet.
    Output: Ambisonics B-Format, Kanalzahl = (order+1)^2, Ordnung aktuell
            per Konstante SAPOC_DEFAULT_AMBI_ORDER (Laufzeit-Wechsel ist
            vorbereitet, siehe AmbisonicsEncoder::setOrder(), aber Bus-Groesse
            ist im Plugin-Kontext fix pro Instanz -- fuer Ordnungswechsel im
            Standalone-Fall einfacher, da dort kein Host-Bus-Vertrag existiert).

    Kein Decoding hier -- Output wird als rohes B-Format ausgegeben und in
    einer DAW/mit externen Tools (SPARTA, IEM Suite) weiterverarbeitet.
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

    // Zugriff fuer den Editor (GUI liest/schreibt direkt auf die Engine)
    TrajectoryEngine& getTrajectoryEngine() { return trajectoryEngine; }
    int getNumLiveInputs() const { return numLiveInputs; }

private:
    // BusesProperties ist ein geschuetztes Nested-Type von juce::AudioProcessor --
    // nur ueber eine Methode der abgeleiteten Klasse konstruierbar, nicht ueber
    // eine freie Funktion.
    static BusesProperties makeBusLayout();

    static constexpr int numLiveInputs = SAPOC_MAX_LIVE_INPUTS;

    TrajectoryEngine trajectoryEngine { numLiveInputs };
    AmbisonicsEncoder encoder;

    // Pro Objekt persistenter Gain-Zustand fuer Zipper-freies Ramping
    std::vector<std::vector<float>> previousGainsPerObject;

    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialAudioPOCProcessor)
};
