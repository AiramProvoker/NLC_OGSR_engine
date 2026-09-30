# NLC OGSR engine

Engine fork for **NLC Improved (HE)**: [OGSR Engine](https://github.com/OGSR/OGSR-Engine)
(Shadow of Chernobyl) plus the NLC changes. This repository is the only source of
truth for the NLC engine. Do not edit or build copies elsewhere (for example the old
snapshot under `C:\Games\NLC_OGSR_HE\engine`). The mod repo's `engine/` folder is a
read-only mirror of this repository, updated with each release.

- Current build: see [`CHANGELOG_NLC.md`](../CHANGELOG_NLC.md) (top entry).
- Upstream README: <https://github.com/OGSR/OGSR-Engine>.
- Mod repository (game data, deployed `bin_x64`): `jordan1227/improved_to_he`.
  Its `docs/ENGINE_BUILD.md` has the longer notes and evidence.

## Branches and remotes

| Name | Meaning |
|---|---|
| `nlc` (default) | What ships. Every commit here should build and have passed an in-game test. |
| feature branches | Work in progress, e.g. `nlc-upgrade`, `feature/<name>`. Merge into `nlc` after the in-game test. |
| `upstream` remote | `https://github.com/OGSR/OGSR-Engine`. Add once: `git remote add upstream https://github.com/OGSR/OGSR-Engine.git` |
| tags `nlc-<ogsr>.<n>` | One per shipped build, e.g. `nlc-3.589.1`, with a GitHub Release holding the exe and its PDB. |

## Build (Windows, Release x64)

Prerequisites:

- Visual Studio 2022 Build Tools, MSVC v143, plus the individual component
  **C++ ATL for latest v143 build tools (x86 & x64)** (`xr_3da/splash.cpp`).
- Windows 10/11 SDK, `git` on PATH, about 10 GB free disk space.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File nlc_tools\fetch_deps.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File nlc_tools\build_release.ps1 -Rebuild
```

- `fetch_deps.ps1` checks out every third-party source at a pinned commit. Do not use
  upstream `Update_Components.cmd`: it clones moving branch heads that stop matching the
  project files.
- `build_release.ps1` builds into `bin_x64\`, writes a log to `nlc_tools\logs\`, prints
  size and SHA-256, and copies `xrEngine.exe` + `xrEngine.pdb` into
  `nlc_tools\builds\<date>_<sha8>\` (git-ignored) so every exe keeps its matching PDB.
  Omit `-Rebuild` for incremental builds.
- It sets `CL=/execution-charset:windows-1251` (the projects compile with
  `/source-charset:utf-8 /we4566`; OGSR and NLC are built on Russian Windows) and clears
  `NoDefaultCurrentDirectoryInExePath`. The `dbghelp.dll` post-build copy fails without
  the SDK debugging tools; that single error is expected and tolerated.

## Deploy and test

1. Copy `bin_x64\xrEngine.exe` into the mod repo's `bin_x64\` (plus any changed runtime
   DLL, currently `nvngx_dlss.dll`).
2. Deploy with the mod repo's `docs\deploy_to_test.ps1` (it copies named files to the
   game install and checks SHA-256).
3. Copy the matching `xrEngine.pdb` from `nlc_tools\builds\...` next to the installed exe,
   so crash logs show function names. A PDB from another build is ignored.
4. Check the log line `[OGSR Engine ... (nlc-...)]`, then test the change in game plus a
   short regression (save load, `.sq` archive content, weapons, knife combo,
   inventory/PDA, options menu).

## Conventions

- Keep NLC code in NLC files where possible (`xrGame/fl_hook.cpp`/`.h` hold the features
  ported from the old `dinput8.dll` hook). Mark edits inside upstream files with
  `// NLC:`, keep them small. This keeps upstream merges manageable.
- Do not rename existing console commands or Lua functions; gamedata depends on them.
- Commit messages start with `NLC:`.
- Engine source files are UTF-8 (some with BOM) with CRLF; keep that.
- New settings that need game data (shaders, menu rows, strings) go into the mod repo
  in the same release.

## Release a build

1. Merge the tested branch into `nlc`.
2. Set the version text in `ogsr_engine/xrCore/xrCore.cpp` (`nlc-<ogsr>.<n>`), rebuild,
   and run the in-game smoke test.
3. Add a `CHANGELOG_NLC.md` entry: version, date, upstream base, NLC changes, required
   game data, dependency changes, exe SHA-256.
4. Tag and push: `git tag nlc-<ver>` then `git push origin nlc nlc-<ver>`.
5. GitHub Release for the tag with the exe and PDB from `nlc_tools\builds\...`:
   `gh release create nlc-<ver> <exe> <pdb> --title nlc-<ver> --notes-file <notes>`.
   PDBs (about 200 MB) never go into git; the mod repo only tracks the exe.
6. In the mod repo, in one commit: the exe in `bin_x64\`, the mirror update of `engine/`
   (replace its contents with `git archive <tag> | tar -x -C engine`) and
   `engine/ENGINE_VERSION.md`. Never edit engine source in the mod repo.

## Upgrading upstream OGSR

1. `git fetch upstream`, branch off `nlc`, `git merge <upstream commit>` (prefer a pinned
   `main` commit over a release tag; OGSR release notes can describe commits after the tag).
2. Resolve conflicts file by file: keep NLC behaviour, take upstream fixes; record
   decisions in `nlc_tools/UPGRADE_CONFLICTS.md`. Ask when both sides changed the same
   behaviour.
3. Re-pin `nlc_tools/fetch_deps.ps1` to match the new `Update_Components.cmd`, rebuild.
4. Port upstream game-data changes the engine needs into the mod repo: shaders from
   `Game/Resources_SoC_1.0006/gamedata/shaders` file by file (keeping NLC-only and
   NLC-edited shaders), menu rows and strings for new settings. Missing shaders crash at
   startup (for example `temporal_resolve` on the 3.589 upgrade).
5. Build, deploy, full regression, release as above.
