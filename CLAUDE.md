# CLAUDE.md

**Underheard** is a personal-use plugin suite (Splicer, Room Bleed, Drifter, and the
`underheard-*` effects), built with iPlug2 for Ableton Live 11 on Windows. It's developed on a
Mac and tested in GarageBand.

**Start with `docs/STATUS.md`.** It covers where the work stands and how to build and test.
Specs live in `docs/`, starting with `docs/SPLICER-SPEC.md`.

Ground rules:
- **Plugin names** are user-chosen: Splicer, Room Bleed, Drifter, `underheard-<effect>`. Don't
  rename them.
- **DSP** goes in framework-free C++ under `libs/`, so cores can be shared between plugins and
  tested offline. Plugin folders hold only the iPlug2 glue and UI.
- **Parameters** use `kParam*` names and are append-only once projects exist.
- **The audio thread** must never lock, allocate, free or touch files.
- **Before committing:** run `./tests/run-tests.sh`. It runs the offline AU test, auval and the
  VST3 validator.
- **Portability:** no Max for Live dependencies, and keep everything building on Windows.

Framework references: @iPlug2/CLAUDE.md, @iPlug2/Documentation/structure.md
