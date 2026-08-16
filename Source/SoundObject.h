#pragma once
#include <juce_core/juce_core.h>
#include "Vec3.h"

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

    Vec3 position   { 1.0f, 0.0f, 0.0f }; // Startposition: 1m vorne
    Vec3 velocity   { 0.0f, 0.0f, 0.0f };
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
    Vec3 orbitCenter { 0.0f, 0.0f, 0.0f };
    float orbitRadius = 1.0f;
    float orbitAngularSpeed = 1.0f; // rad/s
    float orbitPhase = 0.0f;        // aktueller Winkel, wird fortgeschrieben

    // Fuer Attraction/Repulsion: Staerke, Vorzeichen negativ = abstossend.
    // Gilt, wenn DIESES Objekt als Quelle auf andere wirkt (siehe auch
    // forceExponent/minDistance/maxRange/attractionPulse* unten -- alle
    // ebenfalls Eigenschaften der Quelle, nicht des angezogenen Objekts).
    float attractionStrength = 0.0f;

    // Reibung/Daempfung fuer Impulse-Modus, 0 = keine Daempfung, 1 = sofort stehen.
    // Einfacher multiplikativer Decay pro Simulationsschritt (schnell, aber
    // schrittraten-abhaengig). Fuer eine physikalisch konsistentere,
    // geschwindigkeitsproportionale Bremse siehe dragCoefficient.
    float damping = 0.02f;

    float gain = 1.0f; // manuelles Objekt-Gain, zusaetzlich zur Distanzdaempfung

    // --- Traegheit / Bewegungsgrenzen ---------------------------------
    // <= 0 = unbegrenzt.
    float maxVelocity = 6.0f;
    // Echte, geschwindigkeitsproportionale Bremskraft (F = -dragCoefficient * velocity),
    // zusaetzlich zu damping. 0 = aus.
    float dragCoefficient = 0.0f;
    // Elastizitaet beim Abprall an der Raumgrenze (SceneSettings::roomSize,
    // Reflect-Modus). 0 = die nach aussen zeigende Geschwindigkeitskomponente
    // wird entfernt (Objekt gleitet hoechstens noch tangential an der Wand),
    // 1 = perfekt elastischer Abprall.
    float restitution = 0.6f;
    // Geschwindigkeiten unterhalb dieses Betrags werden hart auf 0 gesetzt.
    // Ohne das naehert sich ein gedaempftes Objekt der Ruhe nur asymptotisch
    // an (kommt rechnerisch nie ganz zum Stillstand).
    float velocitySnapThreshold = 0.01f;

    // --- n-Body-Verfeinerung (gilt, wenn dieses Objekt als Quelle wirkt) ---
    // Exponent im Kraftgesetz, 2 = klassisches inverses Quadratgesetz
    // (Standardverhalten, unveraendert gegenueber frueheren Versionen).
    float forceExponent = 2.0f;
    // Softening-Radius, verhindert harte Kraft-Spruenge bei sehr kleiner
    // Distanz (ersetzt die vorherige globale Konstante gleichen Namens).
    float minDistance = 0.05f;
    // Cutoff-Radius, jenseits dessen diese Quelle keine Kraft mehr ausuebt.
    // <= 0 = unbegrenzte Reichweite.
    float maxRange = 0.0f;
    // Periodische Modulation von attractionStrength: effektive Staerke =
    // attractionStrength * (1 + attractionPulseDepth * sin(Phase)).
    // attractionPulseRate = 0 (Default) => keine Modulation.
    float attractionPulseRate = 0.0f;  // Hz
    float attractionPulseDepth = 0.0f; // 0..1
    float attractionPulsePhase = 0.0f; // Laufzeitzustand, kein Startparameter

    // --- Orbit-Erweiterungen ------------------------------------------
    // Normalenvektor der Umlaufbahn-Ebene, Default {0,0,1} = bisheriges
    // Verhalten (Kreis/Ellipse in der x/y-Ebene).
    Vec3 orbitPlaneNormal { 0.0f, 0.0f, 1.0f };
    // 0 = Kreisbahn, <1 = Ellipse. Vereinfachte Naeherung (fixe Halbachsen
    // orbitRadius/orbitRadius*(1-e), keine Fokuspunkt-basierte Kepler-Bahn
    // mit variabler Winkelgeschwindigkeit) -- fuer den POC bewusst einfach
    // gehalten.
    float orbitEccentricity = 0.0f;
    // Radiusaenderung pro Sekunde waehrend Orbit-Modus, 0 = stabile Bahn.
    float orbitDecay = 0.0f;
    // -1 = orbitCenter ist ein fixer Punkt (bisheriges Verhalten). Sonst id
    // eines anderen SoundObject, um das herum kreisen wird (z.B. Mond-um-
    // Planet-Hierarchien).
    int orbitReferenceObjectId = -1;
};
