# Changelog

Format nach [Keep a Changelog](https://keepachangelog.com/de/1.0.0/),
Versionierung nach [SemVer](https://semver.org/lang/de/) -- solange die
Major-Version 0 ist, gilt: jede Minor-Version (0.X.0) kann Presets brechen
(siehe Presets/schema/), Patch-Versionen (0.X.Y) nicht.

## [Unreleased]
### Hinzugefuegt
- PresetManager: Laden/Speichern von Szenen im Preset-JSON-Format
  (schemaVersion 1), zwei Buttons in der Editor-Toolbar. Unbekannte
  schemaVersion oder kaputtes JSON werden mit Fehlermeldung abgelehnt.
- `Tools/validate_presets`: CLI-Tool, prueft alle Presets in einem Ordner
  ueber denselben Codepfad wie die GUI (Vorbereitung fuer CI, siehe
  Docs/WORKFLOW.md).
- Dynamische Objektzahl: Start mit nur 1 aktivem Objekt statt allen 8,
  "+ Objekt"/"- Objekt entfernen" in der Toolbar
  (`TrajectoryEngine::activateObject()`/`deactivateObject()`).
- Bewegungsphysik erweitert: `maxVelocity`, `dragCoefficient`,
  `velocitySnapThreshold`, `restitution` pro Objekt; kugelfoermige
  Raumgrenze `SceneSettings::roomSize` mit `reflect`/`wrap`/`absorb`.
- n-Body-Verfeinerung: `forceExponent`, `minDistance`, `maxRange` jetzt pro
  Objekt statt globaler Konstante; periodische Attraktions-Modulation
  (`attractionPulseRate`/`-Depth`).
- Orbit-Erweiterungen: `orbitPlaneNormal` (geneigte Bahnebene),
  `orbitEccentricity` (vereinfachte Ellipse), `orbitDecay`,
  `orbitReferenceObjectId` (Orbit um ein anderes Objekt).
- Szene-weite Parameter: `globalField` (konstante Kraft, wie Wind/
  Gravitation), `timeScale` (Zeitraffer/Zeitlupe).
- `ParameterPanel`: Seitenpanel im Editor, zeigt/editiert alle Parameter
  des ausgewaehlten Objekts sowie Szene-Parameter direkt auf der Engine.
- Preset-Schema um alle obigen Felder erweitert (alle optional mit
  Code-Default, siehe Presets/schema/README.md) -- **kein** schemaVersion-
  Bump noetig, da rein additiv; `orbit_pair_demo.json` laedt unveraendert
  weiter (per `validate_presets` und manuellem Save/Load-Roundtrip
  verifiziert).

### Geaendert
- Klick auf ein Objekt (ohne Ziehen) waehlt es fuer das Parameter-Panel
  aus, ohne mehr dessen Bewegungsmodus zu beeinflussen -- vorher wurde
  jeder Klick sofort in `Mode::Manual` uebersetzt (`beginDrag()` in
  `mouseDown()`), was ein laufendes Orbit/Impulse bei jedem Selektions-
  Klick auf `Static` zurueckgesetzt haette.

### Behoben
- Erster lauffaehiger Build (VST3 + Standalone): eigener `Vec3`-Typ statt
  `juce::Vector3D` (das im `juce_opengl`-Modul liegt und eine unnoetige
  OpenGL-Abhaengigkeit eingezogen haette), `BusesProperties`-Konstruktion
  ueber Memberfunktion statt freier Funktion (Zugriffsschutz), Shadow-Field-
  Warnung im Editor behoben.

## [0.1.0] - POC-Grundgeruest
### Hinzugefuegt
- TrajectoryEngine: Static/Manual/Orbit/Impulse/Attracted-Modi
- AmbisonicsEncoder: generische SH-Berechnung, Ordnung 0-7, SN3D/ACN
- PluginProcessor: 8 Mono-Inputs -> Ambisonics-Bus (Ordnung 3 = 16 Kan.)
- 2D-Editor: Maus-Drag, Wurf-Geste, Orbit per Doppelklick
### Bekannte Einschraenkungen
- Kein Dopplereffekt, keine frequenzabhaengige Distanzdaempfung
- Keine 3D-Interaktion, kein MIDI-Mapping
- Keine Preset-Persistenz (State-Save ist Stub)
