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
    // spawnBudget: number of NEW grains this cloud may spawn this tick --
    // THIS cloud's own fair share of the scene-wide maxConcurrentGrains
    // budget for this tick, already computed by the caller (see
    // PluginProcessor::timerCallback()'s per-cloud proportional split, not
    // a value shared/decremented across other clouds' update() calls the
    // way it once was). Decremented as this cloud spawns, so the caller
    // can see how much of its own allotment actually got used.
    void update (double dtSeconds, Vec3 parentPosition, Vec3 parentVelocity,
                 int& spawnBudget);

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
        // Per-grain filter state (feature/grain-filter-integration), only
        // meaningful while GrainCloudSettings::filter.enabled -- see
        // Grain::filterCutoffHz/filterResonance. PluginProcessor's audio-
        // rate GrainAudioState::filter reads these each tick (no RNG
        // involved on that side, see VoiceFilter.h's resetWithValues()/
        // setCutoffAndResonance()).
        float filterCutoffHz = 2000.0f;
        float filterResonance = 0.707f;
        // Modifier stack outcome (feature/modifier-stack) -- see
        // Grain::modifierGainMultiplier/modifierFilters' own comment.
        // PluginProcessor's audio-rate GrainAudioState::stackFilters
        // reads modifierFilters each tick (no RNG involved on that
        // side, same split as the single dedicated filter above).
        float modifierGainMultiplier = 1.0f;
        ModifierStack::FilterBankState modifierFilters;
    };
    void getSnapshot (std::vector<Snapshot>& out) const;

private:
    void spawnGrain (int slot, Vec3 parentPosition, int ringBufferWriteHead, double sampleRate);

    // Re-seeds `rng` from settings.seed whenever that field has changed
    // since the last check (preset just loaded, or user hit "New Seed") --
    // called once at the top of update() rather than scattered at every
    // settings-mutation call site, since GrainCloudSettings is a plain
    // struct PresetManager/ParameterPanel write into directly (see
    // PresetManager.cpp's grainCloudSettingsFromVar()), with no setter
    // hook to piggyback on.
    void resyncSeedIfChanged();
    void updateGrain (Grain& g, float fdt, Vec3 parentPosition, Vec3 parentVelocity);
    Vec3 computeSiblingForce (const Grain& g) const;

    // Voice stealing (see GrainCloudSettings's class comment): picks the
    // oldest active, not-already-stolen-from grain to make room for a
    // pending spawn once maxConcurrentGrains is reached. Returns -1 if
    // every active grain is already mid-steal (or the pool is empty).
    int findOldestStealableSlot() const;
    // Shortens g's remaining life to a short forced fade-out instead of an
    // abrupt cut, reusing the existing Hann-envelope-reaches-exact-zero
    // guarantee every grain's natural ending already relies on -- no new
    // audio-thread code needed, see its own comment in GrainCloud.cpp.
    void beginVoiceSteal (Grain& g);

    // Shared by both GrainGenerationMode::Emitter's immediate spawn and
    // Group's own per-grain pending-countdown resolution (see
    // pendingGroupSpawnCountdowns below) -- finds a free pool slot (or
    // voice-steals the oldest active one) and actually spawns into it.
    // Returns false (does nothing else) if spawnBudget is already
    // exhausted, or if voice-stealing was started but the stolen slot
    // isn't free yet this tick -- the caller decides what "try again
    // later" means for its own case (Emitter caps timeSinceLastSpawn;
    // Group just leaves the countdown at <= 0 so it retries next tick).
    bool trySpawnOneGrain (Vec3 parentPosition, int& spawnBudget);

    GrainCloudSettings settings;
    std::vector<Grain> grains;
    double timeSinceLastSpawn = 0.0;

    // GrainGenerationMode::Group only -- one entry per grain still
    // waiting to actually spawn within the current burst, counting down
    // in seconds (see GrainCloudSettings::groupStartDelay). Resolved
    // (decremented, then spawned once <= 0) once per update() tick,
    // independently of the interval timer that TRIGGERS a new burst.
    std::vector<float> pendingGroupSpawnCountdowns;

    // This cloud's own randomization source (grain spawn timing, pitch/
    // position jitter, movement-mode randomization -- everything that
    // used to draw from PluginProcessor's single shared, unseeded
    // juce::Random). lastAppliedSeed tracks which settings.seed value
    // rng was last re-seeded from, see resyncSeedIfChanged().
    SeededRandom rng;
    juce::int64 lastAppliedSeed = 0;
    bool seedInitialized = false;

    mutable juce::CriticalSection snapshotLock;
    std::vector<Snapshot> snapshotBuffer;

public:
    // Called by PluginProcessor's control-rate update path, which knows the
    // current audio-thread ring-buffer write head and sample rate -- kept
    // as a separate entry point rather than a constructor/settings field
    // since both change every call (write head advances every audio
    // block, sample rate only at prepare()), unlike parentPosition/
    // spawnBudget which update() already takes directly.
    void setRingBufferContext (int writeHeadSample, double sampleRate);

private:
    int ringBufferWriteHeadSample = 0;
    double ringBufferSampleRate = 48000.0;
};
