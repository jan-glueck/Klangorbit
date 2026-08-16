# Preset-/Szenen-Format

Presets speichern eine komplette Objekt-Konfiguration (Positionen,
Bewegungsmodi, Physikparameter, Input-Zuordnung) als JSON. Eigenes
`schemaVersion`-Feld, UNABHAENGIG von der App-Version (CHANGELOG.md) --
weil sich das Datenformat seltener aendert als der Code, und alte Presets
auch nach Code-Umbauten weiter laden sollen.

## Warum getrennt vom Code-Versioning

Wenn `schemaVersion` unveraendert bleibt, garantiert das: jedes Preset mit
dieser Nummer laedt mit jeder App-Version, die dieses Schema unterstuetzt.
Aendert sich das JSON-Format (neues Pflichtfeld, umbenannter Key etc.),
wird `schemaVersion` hochgezaehlt UND eine Migrationsfunktion ergaenzt
(`migrateSchemaV1toV2()` etc.) -- alte Presets sollen nie stillschweigend
falsch interpretiert werden, sondern entweder korrekt migriert oder mit
klarer Fehlermeldung abgelehnt werden.

## Feldreferenz (schemaVersion 1)

Alle Felder ausser den mit "Pflichtfeld" markierten sind optional -- fehlen
sie, gilt der Code-Default (siehe `Source/SoundObject.h`/`Source/SceneSettings.h`).
Das ist bewusst so: neue optionale Felder mit sinnvollem Default sind KEIN
Grund fuer eine schemaVersion-Erhoehung (siehe "Warum getrennt vom
Code-Versioning" oben) -- alte Presets wie `orbit_pair_demo.json` laden
unveraendert weiter, sie nutzen einfach die Defaults fuer alles Neue.

```jsonc
{
  "schemaVersion": 1,
  "name": "orbit_pair_demo",

  // Optional, szenenweite Parameter. Fehlt der Block komplett, gelten die
  // SceneSettings-Defaults.
  "scene": {
    "roomSize": 5.0,                  // Meter, Radius der kugelfoermigen Grenze; <= 0 = keine Grenze
    "boundaryBehavior": "reflect",    // reflect | wrap | absorb
    "globalField": [0.0, 0.0, 0.0],   // konstante Kraft/Masse (wie Wind/Gravitation), wirkt nur auf impulse/attracted
    "timeScale": 1.0                  // Zeitraffer (>1) / Zeitlupe (<1) fuer die gesamte Physik
  },

  "objects": [
    {
      "id": 0,                        // Pflichtfeld, 0-basiert
      "inputChannel": 0,
      "position": [1.0, 0.0, 0.0],    // Pflichtfeld, x=vorne, y=links, z=oben, Meter
      "mode": "orbit",                // Pflichtfeld, static | manual | orbit | impulse | attracted

      "orbitCenter": [0.0, 0.0, 0.0],
      "orbitRadius": 1.5,
      "orbitAngularSpeed": 0.8,       // rad/s
      "attractionStrength": 0.0,      // negativ = abstossend
      "mass": 1.0,
      "damping": 0.02,
      "gain": 1.0,

      // Traegheit/Bewegungsgrenzen
      "maxVelocity": 6.0,             // <= 0 = unbegrenzt
      "dragCoefficient": 0.0,         // kraftbasierte, geschwindigkeitsproportionale Bremse, zusaetzlich zu damping
      "restitution": 0.6,             // Elastizitaet beim Abprall an scene.roomSize (Reflect-Modus), 0..1
      "velocitySnapThreshold": 0.01,  // Geschwindigkeit darunter wird hart auf 0 gesetzt

      // n-Body-Verfeinerung (gilt, wenn DIESES Objekt als Quelle auf andere wirkt)
      "forceExponent": 2.0,           // 2 = klassisches inverses Quadratgesetz
      "minDistance": 0.05,            // Softening gegen harte Kraft-Spruenge bei kleiner Distanz
      "maxRange": 0.0,                // <= 0 = unbegrenzte Reichweite, sonst Cutoff-Radius
      "attractionPulseRate": 0.0,     // Hz, 0 = keine Modulation von attractionStrength
      "attractionPulseDepth": 0.0,    // 0..1

      // Orbit-Erweiterungen
      "orbitPlaneNormal": [0.0, 0.0, 1.0], // Default = bisherige x/y-Ebene
      "orbitEccentricity": 0.0,       // 0 = Kreis, <1 = Ellipse (vereinfachte Naeherung, kein echter Kepler-Orbit)
      "orbitDecay": 0.0,              // m/s, Radiusaenderung ueber Zeit
      "orbitReferenceObjectId": -1    // -1 = orbitCenter (fixer Punkt), sonst id eines anderen Objekts
    }
  ]
}
```

## Ablage

- `Presets/factory/` -- eingecheckte, kuratierte Beispiel-Szenen. Diese sind
  Teil des Repos, jede Aenderung geht durch normale Commits.
- `Presets/user/` -- eigene, unaufgeraeumte Experimente. Wird NICHT
  automatisch committet (siehe .gitignore-Kommentar); wenn ein
  User-Preset gut genug ist, bewusst nach `factory/` verschieben.

## Bezug zu Docs/experiments/

Presets speichern NUR den Endzustand (Startkonfiguration). Wenn ein
Preset im Zusammenspiel mit Live-Input oder manueller Interaktion einen
interessanten Klang erzeugt hat, der sich nicht rein aus der JSON-Datei
rekonstruiert (z.B. weil du waehrend der Wiedergabe live eingegriffen
hast), gehoert die Beschreibung dieses Fundes in ein Experiment-Log, nicht
ins Preset -- siehe Docs/experiments/README.md.
