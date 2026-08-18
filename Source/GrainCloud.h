#pragma once
#include <vector>
#include <juce_core/juce_core.h>
#include "Grain.h"

/**
    Owns a fixed pool of Grains for one parent SoundObject and updates
    their movement at control rate (like TrajectoryEngine), spawning new
    grains at settings.grainRate while settings.enabled by activating pool
    slots -- no allocation after construction.

    Threading: update() is called from the message thread (same timer as
    TrajectoryEngine::update(), see PluginEditor::timerCallback()).
    getSnapshot() returns a lock-free copy for the audio thread, same
    double-buffered pattern as TrajectoryEngine.

    Does NOT read/write the actual audio ring buffer or render any audio
    itself -- it only decides *when*, *where*, and with *which playback
    parameters* a grain should play; PluginProcessor owns the ring buffer
    and does the actual sample-accurate rendering from the snapshot this
    class publishes (see GrainCloud::Snapshot).
*/
class GrainCloud
{
public:
    // poolSize: number of simultaneous grains this cloud can ever have
    // active, independent of (but should be >=) settings.maxConcurrentGrains.
    explicit GrainCloud (int poolSize);

    GrainCloudSettings& getSettings() { return settings; }
    const GrainCloudSettings& getSettings() const { return settings; }

    // Resets settings to defaults and deactivates all grains -- e.g. when
    // the parent SoundObject is deactivated (see TrajectoryEngine::
    // deactivateObject()), so reactivating that slot later doesn't resume
    // with stale settings/grains from whatever used it before.
    void reset();

    // parentPosition/parentVelocity: current state of the owning SoundObject,
    // as resolved by the caller (from TrajectoryEngine).
    // globalGrainBudget: remaining number of grains allowed to spawn this
    // tick across ALL clouds combined; decremented as this cloud spawns
    // (see PluginProcessor, enforces the global maxConcurrentGrains cap).
    void update (double dtSeconds, Vec3 parentPosition, Vec3 parentVelocity,
                 int& globalGrainBudget, juce::Random& rng);

    int getPoolSize() const { return (int) grains.size(); }
    int getNumActiveGrains() const;

    struct Snapshot
    {
        bool active = false;
        Vec3 position;
        Vec3 velocity;             // current movement velocity, m/s -- see GrainDoppler.h (optional per-cloud Doppler pitch shift)
        float ageFraction = 0.0f; // age / lifetimeSeconds, 0..1, for GUI fade-out
        int spawnGeneration = 0;
        int bufferReadStartSample = 0;
        float playbackRate = 1.0f;
        int grainLengthSamples = 0;
    };
    void getSnapshot (std::vector<Snapshot>& out) const;

private:
    void spawnGrain (int slot, Vec3 parentPosition, int ringBufferWriteHead, double sampleRate, juce::Random& rng);
    void updateGrain (Grain& g, float fdt, Vec3 parentPosition, Vec3 parentVelocity);
    Vec3 computeSiblingForce (const Grain& g) const;

    GrainCloudSettings settings;
    std::vector<Grain> grains;
    double timeSinceLastSpawn = 0.0;

    mutable juce::CriticalSection snapshotLock;
    std::vector<Snapshot> snapshotBuffer;

public:
    // Called by PluginProcessor's control-rate update path, which knows the
    // current audio-thread ring-buffer write head and sample rate -- kept
    // as a separate entry point rather than a constructor/settings field
    // since both change every call (write head advances every audio
    // block, sample rate only at prepare()), unlike parentPosition/
    // globalGrainBudget which update() already takes directly.
    void setRingBufferContext (int writeHeadSample, double sampleRate);

private:
    int ringBufferWriteHeadSample = 0;
    double ringBufferSampleRate = 48000.0;
};
