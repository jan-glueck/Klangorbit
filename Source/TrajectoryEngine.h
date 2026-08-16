#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "SoundObject.h"
#include "SceneSettings.h"

/**
    Aktualisiert Positionen aller SoundObjects. Laeuft auf Control-Rate
    (z.B. 60-120 Hz), NICHT im Audio-Thread -- der AmbisonicsEncoder liest
    per Snapshot/atomarem Zugriff die jeweils letzte Position.

    Threading: update() wird vom Message-Thread oder einem eigenen Timer
    aufgerufen. getSnapshot() liefert eine Kopie fuer den Audio-Thread
    (lock-free über doppelt gepufferten Zustand).

    Objekt-Kapazitaet vs. aktive Objekte: die Groesse von objects ist fix
    (= Anzahl Live-Input-Kanaele, siehe SAPOC_MAX_LIVE_INPUTS -- der Audio-
    Bus kann zur Laufzeit nicht umkonfiguriert werden, siehe README). Wie
    viele davon tatsaechlich "existieren" ist rein eine Frage von
    inputChannel >= 0 (aktiv) vs. < 0 (inaktiv/Platzhalter) -- diese
    Konvention gab es schon vorher (Encoder/Snapshot/PresetManager werten
    sie bereits aus), activateObject()/deactivateObject() setzen sie nur
    kontrolliert von der GUI aus.
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

    // Aktiviert/deaktiviert ein Objekt (siehe Klassenkommentar). Liefert
    // false, wenn index ungueltig ist oder der Zielzustand schon erreicht
    // ist. deactivateObject() setzt das Objekt auf einen sauberen
    // Ausgangszustand zurueck (wie beim Preset-Laden).
    bool activateObject (int index);
    bool deactivateObject (int index);
    int getNumActiveObjects() const;
    // Liefert den Index des ersten inaktiven Objekts, oder -1 wenn alle aktiv sind.
    int findNextInactiveObject() const;

    SceneSettings& getSceneSettings() { return sceneSettings; }
    const SceneSettings& getSceneSettings() const { return sceneSettings; }

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
    void applyBoundary (SoundObject& obj);
    Vec3 computeAttractionForce (const SoundObject& obj) const;

    std::vector<SoundObject> objects;
    SceneSettings sceneSettings;

    // Doppelpufferung fuer lock-freien Zugriff vom Audio-Thread
    mutable juce::CriticalSection snapshotLock;
    std::vector<Snapshot> snapshotBuffer;

    static constexpr float gravityLikeConstant = 1.0f;
};
