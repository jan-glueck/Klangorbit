#pragma once
#include <vector>
#include <juce_core/juce_core.h>
#include "DelayTap.h"

/**
    One object's multi-tap delay -- a genuinely separate spatial sound
    source from GrainCloud (feature/delay-instance, Branch 3), with real
    feedback: taps read from a single recirculating buffer (owned by
    PluginProcessor, see its DelayAudioState-equivalent members) that
    already contains its own decayed past repeats, so echoes actually
    die away over time instead of each tap independently re-reading a
    dry, non-recirculating signal the way grains do.

    Like GrainCloud, this class only decides *where* (spatially) and
    *when* (delay time) each tap reads from -- it does NOT touch the
    actual audio buffer or do any audio-thread rendering itself; see
    DelayRenderer.h/PluginProcessor for the real-time-safe read/write
    functions and the audio-thread-owned per-object buffer/filter state.

    Unlike GrainCloud, there's no pool/spawning here: numTaps taps exist
    continuously whenever enabled, each just orbiting the parent at a
    fixed phase offset -- much simpler control-rate state than grains'
    spawn/lifetime/movement-mode machinery.

    Threading: update() is called from the message thread (same timer as
    GrainCloud::update(), see KlangorbitProcessor::timerCallback()).
    getSnapshot() returns a lock-free copy for the audio thread, same
    double-buffered pattern as GrainCloud/TrajectoryEngine.
*/
class DelayInstance
{
public:
    DelayInstance();

    DelayInstanceSettings& getSettings() { return settings; }
    const DelayInstanceSettings& getSettings() const { return settings; }

    // Resets settings to defaults and clears tap state -- e.g. when the
    // parent SoundObject is deactivated, so reactivating that slot later
    // doesn't resume with stale settings/taps from whatever used it
    // before (same role as GrainCloud::reset()).
    void reset();

    // parentPosition: current position of the owning SoundObject, as
    // resolved by the caller (from TrajectoryEngine) -- taps orbit this.
    void update (double dtSeconds, Vec3 parentPosition, double sampleRate);

    int getNumTaps() const { return (int) snapshotBuffer.taps.size(); }

    struct Snapshot
    {
        bool enabled = false;
        std::vector<DelayTap> taps;
        // The recirculation length in samples (== settings.delayTime *
        // sampleRate) -- NOT a per-tap value, since every tap reads from
        // the SAME recirculating buffer; this is what the audio-thread
        // write pass feeds back from (see DelayRenderer.h).
        float feedbackDelaySamples = 0.0f;
        float feedbackGain = 0.0f;
        float dampingCutoffHz = 20000.0f; // see DelayInstance::dampingCutoffHzFor()
        // Published through each DelayTap entry's own modifierGainMultiplier/
        // modifierFilters fields (feature/modifier-stack) -- see DelayTap.h.
    };
    void getSnapshot (Snapshot& out) const;

    // Maps feedbackDamping (0..1, UI-facing) onto a VoiceFilter LowPass
    // cutoff -- log interpolation from (effectively undamped) at 0 down
    // to a dark 500 Hz at 1. Public/static/pure so Tools/verify_* and
    // PluginProcessor's own damping-filter setup can use the exact same
    // mapping rather than risking two independent copies drifting apart.
    static float dampingCutoffHzFor (float feedbackDamping);

private:
    void resyncSeedIfChanged();
    // Rebuilds `taps` (size + each one's own delaySamples, including
    // delayTimeJitter) whenever numTaps/delayTime/delayTimeJitter/seed
    // have changed since the last check -- same lazy-resync pattern as
    // GrainCloud::resyncSeedIfChanged(), called once at the top of
    // update() rather than scattered at every settings-mutation call
    // site (GrainCloudSettings-style plain struct, no setter hooks).
    void resyncTapConfigIfChanged();

    DelayInstanceSettings settings;
    SeededRandom rng;
    juce::int64 lastAppliedSeed = 0;
    bool seedInitialized = false;

    int lastAppliedNumTaps = -1;
    float lastAppliedDelayTime = -1.0f;
    float lastAppliedDelayTimeJitter = -1.0f;
    bool tapConfigInitialized = false;

    std::vector<DelayTap> taps; // control-rate authoritative state
    // Parallel to `taps` -- each tap's own multiple of settings.delayTime
    // (normally exactly i+1, jittered by delayTimeJitter if set),
    // recomputed only by resyncTapConfigIfChanged(). Kept separately
    // rather than re-deriving it from a previously-computed delaySamples
    // value, since that value depends on sampleRate (known only inside
    // update(), not here) -- storing the dimensionless multiple directly
    // avoids any placeholder-sample-rate roundtrip.
    std::vector<float> tapMultiples;
    // Cached copy of settings.modifiers as of the last resample, purely
    // so resyncTapConfigIfChanged() can detect an in-place edit to the
    // stack itself (Modifier::operator==, see its own comment) -- taps
    // have no per-spawn "redecide" moment the way a grain does, so this
    // is the only way an edited Gain/TimeOffset/Filter value ever
    // reaches an already-existing tap.
    std::vector<Modifier> lastAppliedModifiers;
    float orbitPhase = 0.0f;

    mutable juce::CriticalSection snapshotLock;
    Snapshot snapshotBuffer;
};
