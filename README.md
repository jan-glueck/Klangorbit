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
   danach frei weiter und wird durch `damping` abgebremst.

## Projektstruktur

```
SpatialAudioPOC/
  CMakeLists.txt
  CHANGELOG.md          <- Code-Versionierung (SemVer)
  Source/                <- C++ Code
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
  trotz `minDistance`-Clamp zu harten Sprüngen fuehren. Bei Bedarf
  weicheres Kraftgesetz (z.B. Plummer-Potential) nachruesten.
- **Keine Persistenz.** `getStateInformation`/`setStateInformation` sind
  Stubs -- Szene/Objektkonfiguration wird beim Neuladen nicht gespeichert.
