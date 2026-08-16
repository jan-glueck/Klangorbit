# Projekt-Briefing: Spatial Audio POC

## Idee

Objektbasiertes Tool zur Klanggestaltung im Raum, fuer Experimentalmusik/
elektroakustische Komposition. Kernidee: Klangquellen sind Objekte mit
Position im 3D-Raum, die sich nach Trajektorien bewegen lassen -- manuell,
als Umlaufbahn (Orbit), oder physikalisch (Impuls/Wurf, n-Body-Attraktion/
Repulsion zwischen Objekten). Positionen werden in Ambisonics-B-Format
encodiert.

Kein VR/AR in dieser Phase -- 2D/3D-Desktop-Oberflaeche mit Maus/MIDI.
Kein Decoding im Tool selbst -- Output ist rohes Ambisonics-B-Format,
Decoding (binaural oder Lautsprecher) passiert extern in einer DAW mit
SPARTA oder IEM Plugin Suite.

## Technischer Rahmen

- JUCE (C++), Ziel: VST3-Plugin + Standalone-App aus derselben Codebasis
- macOS
- Live-Audio-Input: mehrere Mono-Kanaele (aktuell 8, konfigurierbar),
  je einem Objekt zugeordnet
- Ambisonics-Encoding: generisch ueber assoziierte Legendre-Polynome
  (ACN/SN3D, AmbiX-kompatibel), aktuell 3. Ordnung (16 Kanaele), Ordnung
  soll spaeter frei waehlbar sein

## Stand

Code-Geruest existiert (Source/, siehe README.md fuer Architektur-Skizze
und Signalfluss-Diagramm), ist aber NICHT kompiliert oder getestet --
das war bisher ausserhalb einer Build-Umgebung entstanden. Erste Aufgabe
ist ein lauffaehiger Build.

Projektstruktur, Preset-Format und Git-Workflow sind bereits festgelegt:

- `README.md` -- Architektur, Signalfluss, bekannte Einschraenkungen
- `CHANGELOG.md` -- Code-Versionierung (SemVer)
- `Presets/schema/README.md` -- Preset-Format, eigenes schemaVersion
  (unabhaengig von der Code-Version)
- `Docs/WORKFLOW.md` -- Branch-/Tag-/Release-Konventionen
- `Docs/experiments/README.md` -- Konvention fuer Sitzungslogs zu
  Klangfunden, getrennt von Presets

## Erste Schritte fuer Claude Code

1. Git-Repo initialisieren, JUCE als Submodule einbinden (siehe
   Build-Abschnitt in README.md)
2. Ersten Build versuchen, Fehler beheben
3. Gegen die "Testen mit Reaper + SPARTA/IEM"-Anleitung in README.md
   pruefen, ob der Signalfluss grundsaetzlich funktioniert
4. Danach nach Ansage weiterarbeiten -- offene Punkte stehen unter
   "Bekannte Einschraenkungen" in README.md (u.a. 3D-Interaktion,
   MIDI-Mapping, Dopplereffekt, Laufzeit-Ordnungswechsel)

Bei Unklarheiten in Architekturfragen: nachfragen statt raten, besonders
bei allem, was die Preset-Schema-Version oder das Kanal-/Bus-Layout
betrifft -- das sind bewusste Entscheidungen, keine Zufallswerte.
