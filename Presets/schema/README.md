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

```jsonc
{
  "schemaVersion": 1,
  "name": "orbit_pair_demo",
  "objects": [
    {
      "id": 0,
      "inputChannel": 0,
      "position": [1.0, 0.0, 0.0],   // x=vorne, y=links, z=oben, Meter
      "mode": "orbit",                // static | manual | orbit | impulse | attracted
      "orbitCenter": [0.0, 0.0, 0.0],
      "orbitRadius": 1.5,
      "orbitAngularSpeed": 0.8,       // rad/s
      "attractionStrength": 0.0,      // negativ = abstossend
      "mass": 1.0,
      "damping": 0.02,
      "gain": 1.0
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
