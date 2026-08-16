#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "SoundObject.h"

/**
    Aktualisiert Positionen aller SoundObjects. Laeuft auf Control-Rate
    (z.B. 60-120 Hz), NICHT im Audio-Thread -- der AmbisonicsEncoder liest
    per Snapshot/atomarem Zugriff die jeweils letzte Position.

    Threading: update() wird vom Message-Thread oder einem eigenen Timer
    aufgerufen. getSnapshot() liefert eine Kopie fuer den Audio-Thread
    (lock-free über doppelt gepufferten Zustand).
*/
class TrajectoryEngine
{
public:
    explicit TrajectoryEngine (int maxObjects);

    // Ruft man z.B. aus einem juce::Timer heraus auf.
    // dtSeconds: Zeit seit letztem Aufruf.
    void update (double dtSeconds);

    // Objekt-Verwaltung -- nur vom Message-Thread aus aufrufen.
    SoundObject& getObject (int index);
    int getNumObjects() const { return (int) objects.size(); }

    // Manuelle Interaktion (Maus/MIDI)
    void beginDrag (int objectIndex);
    void dragTo (int objectIndex, Vec3 newPosition);
    void endDrag (int objectIndex);

    // "Wirft" ein Objekt: setzt Mode=Impulse und Anfangsgeschwindigkeit
    void throwObject (int objectIndex, Vec3 initialVelocity);

    // Startet Orbit-Bewegung um einen Punkt
    void startOrbit (int objectIndex, Vec3 center, float radius, float angularSpeed);

    // Aktiviert n-Body Attraktion/Repulsion zu allen anderen "Attracted"/Orbit-Objekten
    void setAttraction (int objectIndex, float strength);

    // Lock-freier Snapshot fuer den Audio-Thread: Position + Geschwindigkeit je Objekt.
    struct Snapshot { Vec3 position, velocity; bool active; };
    void getSnapshot (std::vector<Snapshot>& out) const;

private:
    void integrate (SoundObject& obj, double dt);
    Vec3 computeAttractionForce (const SoundObject& obj) const;

    std::vector<SoundObject> objects;

    // Doppelpufferung fuer lock-freien Zugriff vom Audio-Thread
    mutable juce::CriticalSection snapshotLock;
    std::vector<Snapshot> snapshotBuffer;

    static constexpr float minDistance = 0.05f; // vermeidet Division durch ~0 bei Attraktion
    static constexpr float gravityLikeConstant = 1.0f;
};
