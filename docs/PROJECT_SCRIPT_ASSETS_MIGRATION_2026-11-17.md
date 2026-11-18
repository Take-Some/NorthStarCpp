# NorthStarCpp: project scripts as first-class assets
Date: 2026-11-17

## Canonical layout

Each new and existing project uses:

    Projects/<Project>/
      assets/
        scripts/
          main.js
          playerConfig.js
          player/
          examples/
        ui/
        audio/
        shaders/
        textures/
      main.nscn
      northstarcpp.project.json

Physical source: assets/scripts/main.js
VFS reference: scripts/main.js
This distinction is intentional: assets is the filesystem mount root.
Scene objects and project manifests retain VFS-relative script references;
no rewrite to assets/scripts/... is required inside scenes.

## Existing projects migrated

- AbbyPistolDemo: 2 active scripts migrated
- KaylaNightCity: 4 active scripts migrated
- NorthStarFirstPersonPlatform: 4 active scripts migrated
- 23 historical .bak files moved out of the asset tree to each project's
  migration/legacy-script-backups. Checksums were verified before removal
  of the old project/scripts trees.
- Existing assets, XML UI documents, shaders, audio and scenes unchanged.

## Engine and tooling

- AssetCatalog indexes physical scripts, JS/MJS/TS, folders and nested source
  trees normally without an external-script virtual mount.
- Qt Content Browser can open JS/TS text assets in the embedded source editor.
- The Player resolves canonical assets/scripts and watches them for hot reload.
- Older source project/scripts directories remain readable as a migration
  fallback if assets/scripts is absent; new projects never write to them.
- Native C++ and Python project factories create assets/scripts.
- Abby demo and FPS arena generators output assets/scripts.
- The game exporter validates executable script assets and places them
  exactly once at game/assets/scripts without a redundant game/scripts copy.
- Project jsconfig.json now includes assets/scripts/**/*.js and resolves the
  actual embedded @northstar/engine type declarations.
- SDK Session.scene/loadScene facades restored to match native bindings and
  the SDK's existing TypeScript declarations.

## Verified

- CTest: catalog, script asset layout/package, catalog index, NEF8 parallel,
  Qt text asset editor, declarative UI and language service: 7/7 PASS.
- KaylaNightCity NUI menu smoke: PASS.
- Project HLSL / GLSL and Ink-like UI smoke: PASS.
- Actual Win32 Player, temporary copy of KaylaNightCity canonical assets:
  Settings click activates correctly on D3D12 and Vulkan: PASS.
- Native D3D12 UI pointer, keyboard, NAC sounds, project YTD/shaders
  and paused UI update: PASS.
- TypeScript SDK declaration paths resolve for all three projects.

## Installation / known issue

Player, AssetCatalog and the full Qt Editor have been compiled and linked
successfully at out/build/northstarcpp_player.exe and
out/build/northstarcpp_editor_qt.exe. The initial Windows EXE lock disappeared
on a later build; no user process was forcibly terminated. A restart of the
editor plus F5 reindexes assets/scripts in the Content Browser. The linker
still emits nonfatal boost::any RTTI/COMDAT size mismatch warnings; these
are tracked separately and are not claimed to be fixed by script migration.

No production out/package export or Git commit/push was requested/performed.
