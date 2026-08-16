# Workflow: Branches, Tags, Releases

## Branches

- `main` -- muss immer bauen und starten. Nichts Kaputtes wird hier
  committet, auch nicht zwischenzeitlich.
- `experiment/<kurzname>` -- fuer riskante Aenderungen an Physik/Encoder,
  bei denen unklar ist, ob sie sich lohnen (z.B. `experiment/plummer-potential`,
  `experiment/doppler`). Wird gegen `main` gemergt, wenn es funktioniert
  und den Code nicht schlechter macht -- sonst einfach liegen gelassen
  oder geloescht. Kein Zwang, jeden Branch aufzuraeumen.
- Feature-Branches fuer klar umrissene Arbeit (`feature/3d-view`,
  `feature/midi-mapping`) wie ueblich, gegen `main` mergen wenn fertig.

Kein strikter PR-Zwang bei einem Einzelprojekt -- aber: Merge nach `main`
nur wenn's baut und die README/CHANGELOG-Eintraege stimmen.

## Tags / Versionen

Git-Tag pro Release, z.B. `v0.2.0`, entsprechend CHANGELOG.md. Waehrend
0.x.y (POC-Phase):

- Patch (0.1.0 -> 0.1.1): Bugfix, kein Verhaltensunterschied fuer Presets
- Minor (0.1.x -> 0.2.0): neues Feature, kann Preset-`schemaVersion`
  hochzaehlen (siehe Presets/schema/README.md) -- IMMER im CHANGELOG
  vermerken, ob Presets betroffen sind
- Major (1.0.0): erst wenn's kein POC mehr ist, sondern benutzbar sein soll

## Commits

Keine strikte Commit-Message-Konvention noetig fuer ein Einzelprojekt,
aber sinnvoll: Praefix, wenn's um Presets/Schema oder Experimente geht,
damit sie beim Durchsuchen der Historie auffindbar sind, z.B.
`presets: add orbit_pair_demo`, `schema: bump to v2, add dampingCurve field`.

## Woher weiss ich, ob ein Preset noch laedt?

Kurzer Sanity-Check vor jedem Release: alle Dateien in `Presets/factory/`
einmal laden. Sobald es einen Preset-Loader gibt, das als simples Script
(`Tools/validate_presets.py` o.ae.) ergaenzen, das schemaVersion gegen die
vom Code unterstuetzte Range prueft und mit Exit-Code fehlschlaegt, falls
nicht -- dann laesst sich das auch in CI haengen, sobald es eine gibt.
