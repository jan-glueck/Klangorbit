#pragma once
#include <juce_core/juce_core.h>

/**
    Zustand eines einzelnen Klangobjekts im Raum.

    Position in kartesischen Koordinaten (Meter, rechtshaendig):
        x = vorne/hinten (vorne positiv)
        y = links/rechts (links positiv)
        z = oben/unten   (oben positiv)

    Wird von der TrajectoryEngine (Control-Rate, ~60-120 Hz) aktualisiert
    und vom AmbisonicsEncoder (Audio-Rate) gelesen. Kein Audio-Datum selbst,
    nur Metadaten -- das eigentliche Signal kommt separat ueber den
    zugeordneten Input-Kanalindex (inputChannel).
*/
struct SoundObject
{
    int id = -1;

    // Welcher Live-Input-Kanal (0-basiert) speist dieses Objekt.
    // -1 = kein Input zugeordnet (Objekt stumm / nur Platzhalter).
    int inputChannel = -1;

    juce::Vector3D<float> position   { 1.0f, 0.0f, 0.0f }; // Startposition: 1m vorne
    juce::Vector3D<float> velocity   { 0.0f, 0.0f, 0.0f };
    float mass = 1.0f; // fuer n-Body-Attraktion/Repulsion

    // Bewegungsmodus, von der TrajectoryEngine ausgewertet
    enum class Mode
    {
        Static,       // bleibt an position stehen (z.B. per Maus gezogen)
        Manual,       // wird gerade per Maus/MIDI live bewegt, keine Physik
        Orbit,        // kreist um orbitCenter mit orbitRadius/orbitSpeed
        Impulse,      // wurde "angestossen", bewegt sich frei mit velocity + Kraeftefeld
        Attracted     // unterliegt n-Body-Kraeften zu anderen Objekten/Punkten
    };
    Mode mode = Mode::Static;

    // Parameter fuer Orbit-Modus
    juce::Vector3D<float> orbitCenter { 0.0f, 0.0f, 0.0f };
    float orbitRadius = 1.0f;
    float orbitAngularSpeed = 1.0f; // rad/s
    float orbitPhase = 0.0f;        // aktueller Winkel, wird fortgeschrieben

    // Fuer Attraction/Repulsion: Staerke, Vorzeichen negativ = abstossend
    float attractionStrength = 0.0f;

    // Reibung/Daempfung fuer Impulse-Modus, 0 = keine Daempfung, 1 = sofort stehen
    float damping = 0.02f;

    float gain = 1.0f; // manuelles Objekt-Gain, zusaetzlich zur Distanzdaempfung
};
