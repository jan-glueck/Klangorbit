# Spatial Audio POC

Objektbasierter Ambisonics-Encoder mit Trajektorien-/Physik-Engine.
Kein Decoding -- Output ist rohes Ambisonics-B-Format (ACN/SN3D, AmbiX-kompatibel),
weiterverarbeitbar in SPARTA (AmbiBIN/AmbiDEC) oder IEM Plugin Suite.

## Signalfluss

```
Live-Input (bis zu 8 Mono-Kanaele)
        |
        v
[SoundObject 0..7]  <-- Position/Bewegung von TrajectoryEngine (Control-Rate, ~90 Hz)
        |
        v
[AmbisonicsEncoder]  -- generische SH-Berechnung (Legendre-Rekursion),
        |                beliebige Ordnung, aktuell 3. Ordnung = 16 Kanaele
        v
Ambisonics-Output (16 Kanaele bei Ordnung 3)
        |
        v
DAW / SPARTA / IEM Suite -> Decoding (binaural oder Lautsprecher)
```

## Build

Voraussetzung: CMake >= 3.22, Xcode Command Line Tools (macOS).

```bash
# JUCE als Submodule holen (empfohlen, sonst laedt CMake es bei jedem clean build neu)
git submodule add https://github.com/juce-framework/JUCE.git JUCE

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

Ergebnis: `SpatialAudioPOC.vst3` und die Standalone-App im Build-Verzeichnis
(`SpatialAudioPOC_artefacts/`). VST3 landet zusaetzlich automatisch im
System-Plugin-Ordner (`COPY_PLUGIN_AFTER_BUILD TRUE`).

## Testen mit Reaper + SPARTA/IEM

1. Plugin/Standalone starten, Live-Input (Mikrofon oder Audio-Interface-Kanal)
   an Input 0 anschliessen.
2. Ambisonics-Output (16 Kanaele) auf einen Bus mit AmbiBIN (SPARTA) oder
   dem IEM BinauralDecoder routen.
3. Objekt 0 im 2D-Fenster mit der Maus ziehen -> Positionsaenderung sollte
   sich in der binauralen Wiedergabe als Richtungsaenderung zeigen.
4. Doppelklick auf ein Objekt startet eine Orbit-Bewegung um den Ursprung
   (Demo fuer den Trajektorien-Modus).
5. Objekt schnell ziehen und loslassen -> Wurf-Geste, Objekt bewegt sich
   danach frei weiter und wird durch `damping`/`dragCoefficient` abgebremst
   und an der Raumgrenze (`roomSize`) reflektiert/gewrappt/absorbiert.
6. Objekt anklicken (ohne zu ziehen) waehlt es aus -- Parameter erscheinen
   im Panel rechts. "+ Objekt" aktiviert das naechste freie Objekt (Start
   ist immer nur Objekt 0 aktiv), "- Objekt entfernen" deaktiviert das
   ausgewaehlte.

## Objekte, Bewegungsphysik und Parameter-Panel

- **Objektzahl ist dynamisch.** Start ist immer nur Objekt 0 aktiv (nicht
  mehr alle `SAPOC_MAX_LIVE_INPUTS`). "+ Objekt"/"- Objekt entfernen" in
  der Toolbar aktivieren/deaktivieren einzelne der max. 8 Objekt-Slots
  (`TrajectoryEngine::activateObject()`/`deactivateObject()`). Der
  Audio-Bus selbst bleibt fix bei 8 Kanaelen (siehe Bus-Layout-Einschraenkung
  oben) -- "hinzufuegen/entfernen" ist rein eine Frage von
  `SoundObject::inputChannel >= 0`, dieselbe Konvention, die Encoder/Snapshot/
  Preset-Speicherung schon vorher genutzt haben.
- **Objekte bremsen jetzt aus, statt endlos zu gleiten.** `maxVelocity`
  deckelt die Geschwindigkeit, `dragCoefficient` ist eine echte,
  geschwindigkeitsproportionale Bremskraft (zusaetzlich zum bisherigen
  `damping`), `velocitySnapThreshold` stoppt sehr langsame Restbewegung
  hart statt sie asymptotisch nie ganz ausklingen zu lassen.
- **Raumgrenze.** `SceneSettings::roomSize` (kugelfoermig um den Ursprung,
  im 2D-Fenster als rote Referenzlinie sichtbar) mit drei Verhaltensweisen
  (`reflect` mit `restitution` pro Objekt / `wrap` / `absorb`).
- **n-Body-Verfeinerung:** `forceExponent`, `minDistance` und `maxRange`
  jetzt pro Objekt (vorher globale Konstante), periodische Modulation der
  Attraktionsstaerke ueber `attractionPulseRate`/`-Depth`.
- **Orbit-Erweiterungen:** geneigte Bahnebene (`orbitPlaneNormal`),
  elliptische Bahnen (`orbitEccentricity`, vereinfachte Naeherung, siehe
  unten), schrumpfende/wachsende Bahnen (`orbitDecay`), Orbit um ein
  anderes, selbst bewegtes Objekt statt nur um einen fixen Punkt
  (`orbitReferenceObjectId`).
- **Globales Feld & Zeitraffer:** `SceneSettings::globalField` (konstante
  Kraft/Masse, wie Wind/Gravitation, wirkt auf Impulse/Attracted-Objekte)
  und `timeScale` (Zeitraffer/Zeitlupe fuer die gesamte Physik).
- **Parameter-Panel** (rechts im Editor-Fenster): zeigt/editiert alle
  Parameter des in der 2D-Ansicht ausgewaehlten Objekts sowie die
  Szene-Parameter. Schreibt direkt auf die Engine, keine Preset-Datei
  noetig zum Ausprobieren. Vollstaendige Feldreferenz inkl. Defaults in
  `Presets/schema/README.md`.

## Projektstruktur

```
SpatialAudioPOC/
  CMakeLists.txt
  CHANGELOG.md          <- Code-Versionierung (SemVer)
  Source/                <- C++ Code
  Tools/
    validate_presets.cpp  <- CLI-Tool, prueft Presets/factory/*.json (siehe Docs/WORKFLOW.md)
  Presets/
    schema/README.md    <- Preset-Format, eigenes schemaVersion
    factory/             <- kuratierte, eingecheckte Szenen
    user/                 <- eigene Experimente, nicht committet (.gitignore)
  Docs/
    WORKFLOW.md          <- Branch-/Tag-/Release-Konventionen
    experiments/          <- Sitzungslogs zu interessanten Klangfunden
      scratch/             <- Rohnotizen, nicht committet
  JUCE/                   <- Submodule, nicht eingecheckt
```

Grundprinzip: **Code-Version** (CHANGELOG/SemVer), **Preset-Schema-Version**
(Presets/schema/) und **Experiment-Log** (Docs/experiments/) sind drei
getrennte Versionierungen, weil sie sich unabhaengig voneinander aendern --
ein Preset soll auch nach einem Code-Refactor noch laden, und ein
interessanter Klangfund ist oft nicht 1:1 in einem Preset abbildbar (z.B.
wenn er aus Live-Eingriffen entstand). Details in den jeweiligen READMEs,
Branch-/Release-Ablauf in Docs/WORKFLOW.md.

## Bekannte Einschraenkungen / naechste Schritte

- **Nur 2D-Interaktion.** Maus bewegt Objekte in der x/y-Ebene (Hoehe z fix
  bei 0). 3D-Ansicht/-Interaktion ist als naechster Schritt vorgesehen,
  gleiche Datenbasis (TrajectoryEngine/SoundObject sind bereits 3D).
- **Kein MIDI-Mapping.** `InputMapper`-Modul aus der Architektur-Skizze ist
  noch nicht implementiert; MIDI-CC auf Objektparameter fehlt.
- **Ambisonics-Ordnung ist fest pro Instanz.** `AmbisonicsEncoder::setOrder()`
  existiert, aber der Output-Bus wird beim Prepare/Konstruktor fixiert
  (VST3-Busse sind zur Laufzeit nicht trivial umkonfigurierbar). Fuer
  Laufzeit-Ordnungswechsel: im Standalone-Fall einfacher umsetzbar als im
  Plugin-Kontext, da kein Host-Bus-Vertrag existiert -- ggf. Standalone
  zuerst dafuer erweitern.
- **Distanzdaempfung ist rein Gain-basiert (1/r-Gesetz).** Keine
  frequenzabhaengige Luftabsorption (Hochtonverlust ueber Distanz).
  Fuer akkurate physikalische Modellierung als naechstes ein
  einfacher One-Pole-Tiefpass pro Objekt, Cutoff abhaengig von Distanz.
- **Kein Dopplereffekt.** Geschwindigkeit ist im Snapshot bereits
  verfuegbar (`TrajectoryEngine::Snapshot::velocity`), wird aber im
  Encoder noch nicht in eine Pitch-/Delay-Modulation umgesetzt.
- **n-Body-Attraktion ist ungetestet bei vielen gleichzeitig aktiven
  Attraktoren** -- inverses Quadratgesetz kann bei sehr kleinen Distanzen
  trotz `minDistance`-Clamp zu harten Sprüngen fuehren. `forceExponent`
  (< 2 = weicher/weitreichender) und `maxRange` (Cutoff-Radius) geben jetzt
  Werkzeuge dagegen an die Hand, sind aber kein Ersatz fuer ein echtes
  weicheres Kraftgesetz (z.B. Plummer-Potential), falls das noetig wird.
- **Kollision zwischen Objekten ist nicht implementiert.** Objekte
  durchdringen sich; ein `collisionRadius`/`onCollision`-Mechanismus
  (bounce/merge/trigger) ist ein moeglicher naechster Schritt, aber
  bewusst nicht Teil dieser Aenderung -- "merge" wuerfe z.B. die Frage auf,
  was mit der festen Input-Kanal-Zuordnung eines verschmolzenen Objekts
  passiert, das ist eine eigene Architekturentscheidung.
- **Keine audio-reaktive Kopplung.** Parameter wie Attraktionsstaerke oder
  Orbit-Geschwindigkeit koennten vom Eingangspegel des jeweiligen Objekts
  moduliert werden (`attractionModulatedByAmplitude` o.ae.) -- braucht einen
  neuen Datenpfad vom Audio-Thread (Pegel-/Envelope-Follower pro Kanal)
  zurueck zum Message-Thread, existiert noch nicht.
- **`orbitEccentricity` ist eine vereinfachte Naeherung**, keine
  fokuspunktbasierte Kepler-Bahn (feste Halbachsen statt variabler
  Winkelgeschwindigkeit nach Keplers zweitem Gesetz) -- fuer den POC
  bewusst einfach gehalten.
- **DAW-Session-Persistenz fehlt weiterhin.** `getStateInformation`/
  `setStateInformation` sind noch Stubs -- die Szene wird NICHT automatisch
  im Host-Projekt gespeichert/wiederhergestellt. Bewusst nicht mit dem
  Preset-JSON kurzgeschlossen: der Host kann `setStateInformation` von
  einem beliebigen Thread aufrufen, `TrajectoryEngine::getObject()` ist das
  aber nicht (siehe Klassenkommentar, "nur vom Message-Thread aus"). Ohne
  zusaetzliche Synchronisierung der Objektliste selbst (aktuell nur der
  Audio-Thread-Snapshot ist gelockt) waere das ein Race. Preset-Laden ueber
  die GUI ist davon nicht betroffen (laeuft immer auf dem Message-Thread).
- **Preset laden/speichern ist implementiert.** `PresetManager`
  (`Source/PresetManager.h/.cpp`) liest/schreibt Szenen im schemaVersion-1-
  Format (siehe `Presets/schema/README.md`), ueber zwei Buttons in der
  Editor-Toolbar. Laden ersetzt die komplette Szene; nicht unterstuetzte
  `schemaVersion` oder kaputtes JSON werden mit Fehlermeldung abgelehnt statt
  stillschweigend interpretiert. `Tools/validate_presets` prueft alle
  Presets in einem Ordner ueber denselben Codepfad (fuer CI vorbereitet,
  siehe `Docs/WORKFLOW.md`). Der Standard-Ordner im Dateidialog
  (`Presets/user/`) ist nur ein Komfort-Default fuer lokale Dev-Builds aus
  diesem Checkout (absoluter Pfad zur Build-Zeit via CMake) -- nicht
  portabel auf ein an anderer Stelle installiertes Plugin.
