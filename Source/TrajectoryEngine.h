#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include "SoundObject.h"
#include "SceneSettings.h"
#include "GrainCloud.h"

/**
    Updates positions of all SoundObjects. Runs at control rate
    (e.g. 60-120 Hz), NOT on the audio thread -- AmbisonicsEncoder reads the
    latest position via a snapshot/atomic-style access.

    Threading: update() is called from the message thread or a dedicated
    timer. getSnapshot() returns a copy for the audio thread (lock-free via
    double-buffered state).

    Object capacity vs. active objects: the size of objects is fixed
    (= number of live input channels, see SAPOC_MAX_LIVE_INPUTS -- the audio
    bus cannot be reconfigured at runtime, see README). How many of them
    actually "exist" is purely a matter of inputChannel >= 0 (active) vs.
    < 0 (inactive/placeholder) -- this convention already existed before
    (encoder/snapshot/PresetManager already evaluate it),
    activateObject()/deactivateObject() just set it in a controlled way
    from the GUI.

    Also owns one GrainCloud per object slot (see GrainCloud.h/Grain.h) --
    housed here rather than in PluginProcessor because a GrainCloud is,
    architecturally, part of an object's non-audio-thread scene state
    (control-rate-updated, preset-serialized) exactly like the SoundObject
    itself, even though PluginProcessor still owns the actual per-object
    ring buffers and does the sample-accurate grain rendering (audio-thread
    concerns that don't belong here). Keeping GrainCloud here also means
    PresetManager -- which only ever depended on TrajectoryEngine, not on
    the full plugin class -- can save/load grain cloud settings without
    growing a new dependency.
*/
class TrajectoryEngine
{
public:
    // grainPoolSizePerCloud: see GrainCloud -- how many simultaneous grains
    // a single cloud can ever have active. Defaulted so existing callers
    // (Tools/validate_presets etc.) that only care about SoundObjects don't
    // need to know about grain pool sizing.
    explicit TrajectoryEngine (int maxObjects, int grainPoolSizePerCloud = 32);

    // Call this e.g. from a juce::Timer.
    // dtSeconds: time since the last call.
    void update (double dtSeconds);

    // Object management -- only call from the message thread.
    SoundObject& getObject (int index);
    int getNumObjects() const { return (int) objects.size(); }

    // Activates/deactivates an object (see class comment). Returns false if
    // index is invalid or the target state is already reached.
    // deactivateObject() resets the object to a clean starting state (as
    // when loading a preset).
    bool activateObject (int index);
    bool deactivateObject (int index);
    int getNumActiveObjects() const;
    // Returns the index of the first inactive object, or -1 if all are active.
    int findNextInactiveObject() const;

    SceneSettings& getSceneSettings() { return sceneSettings; }
    const SceneSettings& getSceneSettings() const { return sceneSettings; }

    // One GrainCloud per object slot, always present. Call from the
    // message thread (like getObject()); GrainCloud::update() itself is
    // driven from the editor's timer (see PluginEditor::timerCallback()),
    // not from here, since it needs per-object ring-buffer context
    // (write head, sample rate) that only PluginProcessor's audio thread
    // knows.
    GrainCloud& getGrainCloud (int objectIndex) { return *grainClouds[(size_t) objectIndex]; }
    int getNumGrainClouds() const { return (int) grainClouds.size(); }

    // Manual interaction (mouse/MIDI)
    void beginDrag (int objectIndex);
    void dragTo (int objectIndex, Vec3 newPosition);
    void endDrag (int objectIndex);

    // "Throws" an object: sets Mode=Impulse and an initial velocity
    void throwObject (int objectIndex, Vec3 initialVelocity);

    // Starts orbit motion around a point
    void startOrbit (int objectIndex, Vec3 center, float radius, float angularSpeed);

    // Activates n-body attraction/repulsion towards all other "Attracted"/Orbit objects
    void setAttraction (int objectIndex, float strength);

    // Lock-free snapshot for the audio thread: position + velocity per object.
    struct Snapshot { Vec3 position, velocity; bool active; };
    void getSnapshot (std::vector<Snapshot>& out) const;

private:
    void integrate (SoundObject& obj, double dt);
    void applyBoundary (SoundObject& obj);
    Vec3 computeAttractionForce (const SoundObject& obj) const;

    std::vector<SoundObject> objects;
    SceneSettings sceneSettings;

    // unique_ptr, not a plain vector<GrainCloud>: GrainCloud holds a
    // juce::CriticalSection (for its own audio-thread snapshot), which is
    // neither copyable nor movable, so a vector<GrainCloud> could never
    // grow/reallocate. Only the pointers move here, never the
    // CriticalSection itself.
    std::vector<std::unique_ptr<GrainCloud>> grainClouds;

    // Double buffering for lock-free access from the audio thread
    mutable juce::CriticalSection snapshotLock;
    std::vector<Snapshot> snapshotBuffer;

    static constexpr float gravityLikeConstant = 1.0f;
};
