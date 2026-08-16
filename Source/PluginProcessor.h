#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "SoundObject.h"
#include "TrajectoryEngine.h"
#include "AmbisonicsEncoder.h"
#include "PropagationProcessor.h"
#include "GrainCloud.h"

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

    // Current write position in that object's ring buffer, for
    // GrainCloud::setRingBufferContext() -- only the message thread reads
    // this (via the editor's timer), only the audio thread writes it.
    int getGrainRingBufferWriteHead (int objectIndex) const
    {
        return grainRingBufferWriteHead[(size_t) objectIndex].load (std::memory_order_relaxed);
    }

    // Global cap on simultaneously active grains, summed across ALL
    // clouds -- each active grain costs a full Ambisonics encode pass, so
    // this bounds worst-case CPU independent of how many clouds/objects
    // are granulating. Enforced by the editor's timer via
    // GrainCloud::update()'s globalGrainBudget parameter.
    static constexpr int maxConcurrentGrainsGlobal = 32;

private:
    // BusesProperties is a protected nested type of juce::AudioProcessor --
    // only constructible via a method of the derived class, not via a free
    // function.
    static BusesProperties makeBusLayout();

    static constexpr int numLiveInputs = SAPOC_MAX_LIVE_INPUTS;
    // Each cloud's pool is sized to the full global cap since, in the
    // worst case, a single cloud could legitimately use all of it.
    static constexpr int grainPoolSizePerCloud = maxConcurrentGrainsGlobal;
    static constexpr float grainRingBufferSeconds = 2.0f;

    TrajectoryEngine trajectoryEngine { numLiveInputs, grainPoolSizePerCloud };
    AmbisonicsEncoder encoder;

    // Per-object persistent gain state for zipper-free ramping
    std::vector<std::vector<float>> previousGainsPerObject;

    // Per-object propagation delay/Doppler/air-absorption state (see
    // PropagationProcessor). Applied to the mono source signal before
    // AmbisonicsEncoder::encodeBlock().
    std::vector<PropagationProcessor> propagationPerObject;
    // Tracks each slot's active state from the previous block, so a
    // newly (re)activated object gets its PropagationProcessor reset
    // instead of inheriting stale delay/filter state from whatever
    // occupied that slot before.
    std::vector<bool> wasActiveLastBlock;
    // Scratch buffer for the propagated mono signal, sized once in
    // prepareToPlay -- never (re)allocated in processBlock.
    juce::AudioBuffer<float> propagationScratch;

    // --- GrainCloud audio-thread state (the GrainCloud objects themselves
    // -- settings, spawn timing, per-grain physics -- live in
    // TrajectoryEngine, see its class comment for why) ----------------------
    // Ring buffers, one per object slot, continuously fed that object's
    // live input every block regardless of whether its cloud is currently
    // enabled -- so turning granulation on always has recent history
    // available to read from immediately, no silent warm-up period.
    std::vector<juce::AudioBuffer<float>> grainRingBuffers; // mono, circular
    std::array<std::atomic<int>, numLiveInputs> grainRingBufferWriteHead {};

    // Audio-thread-owned per-(object, grain-pool-slot) playback state --
    // sample-accurate envelope/pitch progress, independent of the
    // coarser control-rate physics snapshot (see GrainCloud::Snapshot).
    struct GrainAudioState
    {
        int lastSeenGeneration = -1; // detects "this slot was respawned" even without an intervening inactive block
        int samplesPlayed = 0;
        std::vector<float> previousChannelGains;
    };
    std::vector<std::vector<GrainAudioState>> grainAudioState; // [objectIndex][grainPoolSlot]
    // Scratch for rendering one grain's windowed, pitch-shifted mono
    // signal at a time before encoding -- reused serially within a block,
    // sized once in prepareToPlay.
    juce::AudioBuffer<float> grainScratch;

    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpatialAudioPOCProcessor)
};
