# Experiment-Log

Presets speichern Startzustaende, keine Klangeindruecke oder den Weg
dorthin. Fuer ein Tool, das auf spielerisches Explorieren von
Parameterraeumen ausgelegt ist, ist der Weg oft interessanter als der
Endzustand -- deshalb ein separates, informelles Log.

## Konvention

Ein File pro Sitzung: `Docs/experiments/YYYY-MM-DD_kurzer-titel.md`.
Kein festes Formular, aber sinnvoll sind:

- Welches Preset/welche Ausgangskonfiguration (Verweis auf Presets/-Datei
  oder grob beschrieben, wenn improvisiert)
- Was veraendert wurde (Parameter, Live-Eingriffe, Reihenfolge)
- Klangliche Beobachtung, auch subjektiv/vage -- besser unpraezise notiert
  als gar nicht
- Ob es sich lohnt, daraus ein Preset in `Presets/factory/` zu machen

## Scratch-Ordner

`Docs/experiments/scratch/` ist gitignored -- fuer Rohnotizen waehrend
einer Sitzung, bevor du entscheidest, ob etwas ins richtige Log gehoert.
Verhindert, dass jede halbfertige Notiz einen Commit erzeugt.
