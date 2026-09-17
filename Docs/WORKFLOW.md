# Workflow: branches, tags, releases

## Branches

- `main` -- must always build and start. Nothing broken gets committed
  here, not even temporarily.
- `experiment/<short-name>` -- for risky changes to physics/encoder where
  it's unclear whether they're worth it (e.g. `experiment/plummer-potential`,
  `experiment/doppler`). Gets merged into `main` if it works and doesn't
  make the code worse -- otherwise just left as is or deleted. No
  obligation to clean up every branch.
- Feature branches for clearly scoped work (`feature/3d-view`,
  `feature/midi-mapping`) as usual, merge into `main` when done.

No strict PR requirement for a solo project -- but: only merge into `main`
if it builds and the README/CHANGELOG entries are correct.

## Tags / versions

Git tag per release, e.g. `v0.2.0`, matching CHANGELOG.md. During 0.x.y
(POC phase):

- Patch (0.1.0 -> 0.1.1): bugfix, no behavior change for presets
- Minor (0.1.x -> 0.2.0): new feature, may bump the preset
  `schemaVersion` (see Presets/schema/README.md) -- ALWAYS note in the
  CHANGELOG whether presets are affected
- Major (1.0.0): only once it's no longer a POC but meant to be usable

## Commits

No strict commit message convention needed for a solo project, but useful:
a prefix when it's about presets/schema or experiments, so they're
findable when browsing the history, e.g. `presets: add orbit_pair_demo`,
`schema: bump to v2, add dampingCurve field`.

## How do I know whether a preset still loads?

Quick sanity check before every release: load every file in
`Presets/factory/` once. Done: `Tools/validate_presets` (C++ console app,
see CMakeLists.txt) loads every preset in a given folder via the same
`PresetManager` code path as the plugin GUI, checks schemaVersion + field
validation, and fails with a non-zero exit code on any error -- runs
automatically in CI on every push now, alongside the full `verify_*`
suite (see `.github/workflows/`).
