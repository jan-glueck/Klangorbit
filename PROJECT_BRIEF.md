# Projekt-Briefing: Klangorbit

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
- macOS (zusaetzlich AU) und Windows (VST3 + Standalone; AU ist Apple-
  exklusiv, siehe README.md "Windows build")
- Live-Audio-Input: mehrere Mono-Kanaele (aktuell 8, konfigurierbar),
  je einem Objekt zugeordnet
- Ambisonics-Encoding: generisch ueber assoziierte Legendre-Polynome
  (ACN/SN3D, AmbiX-kompatibel), aktuell 3. Ordnung (16 Kanaele), Ordnung
  soll spaeter frei waehlbar sein

## Stand

Dieser Abschnitt beschrieb urspruenglich den Projektstart (Code-Geruest
vorhanden, aber nicht kompiliert/getestet, erster lauffaehiger Build als
Ziel) -- inzwischen weit ueberholt. Aktueller Stand: das Plugin baut und
laeuft (VST3/AU/Standalone auf macOS, VST3/Standalone auf Windows),
inklusive vollem Ambisonics-Signalpfad, Physik-Engine, granularer
Synthese, Controller-Mapping (Gamepad/MIDI/OSC), Preset-System und einer
automatisierten Testsuite (`Tools/verify_*`, `validate_presets`), die bei
jedem Push per GitHub Actions auf Windows und (bei Merges nach `main`)
macOS laeuft (`.github/workflows/`). Fuer den tatsaechlichen, laufend
gepflegten Funktionsumfang, Architektur und offene Punkte siehe
`README.md` (Signalfluss-Diagramm, "Known limitations / next steps") und
`CHANGELOG.md` (chronologisch, jede Aenderung mit Begruendung).

Projektstruktur, Preset-Format und Git-Workflow:

- `README.md` -- Architektur, Signalfluss, bekannte Einschraenkungen
- `CHANGELOG.md` -- Code-Versionierung (SemVer)
- `Presets/schema/README.md` -- Preset-Format, eigenes schemaVersion
  (unabhaengig von der Code-Version)
- `Docs/WORKFLOW.md` -- Branch-/Tag-/Release-Konventionen
- `Docs/experiments/README.md` -- Konvention fuer Sitzungslogs zu
  Klangfunden, getrennt von Presets

## Arbeitsweise

Bei Unklarheiten in Architekturfragen: nachfragen statt raten, besonders
bei allem, was die Preset-Schema-Version oder das Kanal-/Bus-Layout
betrifft -- das sind bewusste Entscheidungen, keine Zufallswerte. Bei
groesseren/riskanten Aenderungen: erst Optionen/Trade-offs vorschlagen,
erst nach explizitem Go-ahead umsetzen (siehe die Diskussions- vor
Umsetzungs-Historie im CHANGELOG fuer das etablierte Muster).
