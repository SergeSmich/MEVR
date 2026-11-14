# Halcyon's MELE VR code

GPL-3.0, Copyright (C) 2026 Halcyon ([LICENSE](LICENSE)). Imported from the public repositories
and modified for this project:

| Game | Upstream | Commit imported | Here |
|------|----------|-----------------|------|
| LE1 | [jrush64/MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR) | `3c5151409b4ec1d0b8d936bf3b14441da3598c33` (2026-09-30) | `le1/` |
| LE2 | [jrush64/MassEffect2-VR](https://github.com/jrush64/MassEffect2-VR) | `4784e90649a0398c6812b89fc09f4a0dbcce6441` | `le2/` |

`le1/` holds upstream's `src/` plus its `docs/`, `runtime/MELEVR.ini`, `README.md` and `BUILD.md`
(renamed `UPSTREAM_*.md`). Its vendored MinHook and Dear ImGui are in `third_party/`.
`le2/` holds upstream's `src/` (with `MELE2VR.vcxproj`) plus `docs/ME2_UI_STEREO_FIX.md`, `runtime/MELE2VR.ini`,
`README.md` and `BUILD.md` (renamed `UPSTREAM_*.md`), imported 2026-11-14 in commit `f042b109`. Its vendored MinHook and
Dear ImGui are byte-identical to `third_party/` and were not imported again; its `builds/` and `release/` were left out.
Its `dllmain.cpp` and `dxgi_proxy.cpp` stay for provenance: our ASI entry starts it (research/le2-renderer-integration.md).

The import commit is verbatim; every later commit touching these files is our modification, so
`git log -p -- src/halcyon` is the full record of changes (GPL-3.0 section 5a).
