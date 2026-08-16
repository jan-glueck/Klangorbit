#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "SoundObject.h"
#include "SceneSettings.h"

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
*/
class TrajectoryEngine
{
public:
    explicit TrajectoryEngine (int maxObjects);

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

    // Double buffering for lock-free access from the audio thread
    mutable juce::CriticalSection snapshotLock;
    std::vector<Snapshot> snapshotBuffer;

    static constexpr float gravityLikeConstant = 1.0f;
};
