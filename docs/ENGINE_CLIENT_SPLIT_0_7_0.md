# NorthStarCpp 0.7.0 — separate Engine and Game Client distributions

Version 0.7.0 introduces two independently distributable role packages.
The source repository and canonical Ninja tree remain unified; the release
artifacts are **not** unified.

## Layout after independent QA approval

```text
out/package/release/NorthStarCpp-0.7.0-windows-x64-Release/
├── Engine/
│   ├── bin/
│   │   ├── northstarcpp_editor_qt.exe
│   │   ├── northstarcpp_project_manager.exe
│   │   ├── qt_tools/
│   │   ├── archive/
│   │   └── <engine providers and native dependencies>
│   ├── sdk/
│   ├── tools/
│   ├── documentation/
│   ├── licenses/
│   └── manifest.json
├── GameClient/
│   ├── bin/
│   │   ├── northstarcpp_player.exe
│   │   ├── bugtrap.exe
│   │   ├── bootstrap.json
│   │   ├── archive/
│   │   └── <registered engine providers and native dependencies>
│   ├── licenses/
│   ├── northstarcpp.plugins.json
│   ├── native.runtime.json
│   └── manifest.json
├── Engine-0.7.0-windows-x64-Release.zip
└── GameClient-0.7.0-windows-x64-Release.zip
```

Both archives contain their own root folder and have separate SHA-256 manifests.
The Game Client runtime does not require Qt, the Editor, the SDK, or source code.
The Engine package does not ship `northstarcpp_player.exe`. Game projects and
content are **not** embedded in GameClient; the game exporter supplies those
from the selected project.

Providers, shared runtime libraries, and the `northstar_shared.nsp` baseline
pack can exist in both packages by design: every package is independently
deployable. They must be copied from the validated source manifest, never by
scanning arbitrary DLLs in the build directory.

When Engine and GameClient are extracted as siblings, the Qt Editor resolves
`../../../GameClient/bin/northstarcpp_player.exe` from its private
`Engine/bin/qt_tools` runtime. If only Engine is installed, the user may
configure a separate Player path in **Game > Set Player Executable**.

## Build and QA

```powershell
python -B -m tools.build_dist --qa-only --build-type Release
```

A successful QA build creates a **non-shipping**, fully validated original QA
candidate under `out/package/qa`, plus an Engine/GameClient QA preview.
It does **not** authorize public distribution.

After passing independent signed clean-machine acceptance and the V8 approval
requirements already enforced by `tools.build_dist`:

```powershell
python -B -m tools.build_dist --promote-qa "<QA candidate folder>" `
  --clean-machine-report "<signed acceptance report>"
```

The publish step now emits only separated packages. It never ships the
monolithic QA source distribution.

Validate each independently:

```powershell
python -B -m tools.split_distribution --verify "<release>/Engine"
python -B -m tools.split_distribution --verify "<release>/GameClient"
```

Export an actual game using the standalone GameClient runtime:

```powershell
python -B -m tools.build_game --engine-package "<release>/GameClient" `
  --project "Projects/NorthStarFirstPersonPlatform"
```

The game exporter includes the verified shared `.nsp` resources and
`bootstrap.json` where present.

## Release acceptance criteria

- Engine starts its Qt tools without referencing the Game Client directory.
- Player starts using its own GameClient `bin` and `bin/archive`.
- Editor Play works when the two components are installed as sibling folders.
- Plugin identity, ABI, DLL import closure, and SHA-256 file manifests pass.
- No Qt tools or SDK files leak into the GameClient archive.
- Game export using `--engine-package <GameClient>` can load a project scene.
- No release is published before independent acceptance; existing `dist`
  files and uncommitted source changes are not overwritten.

Synthetic role-packaging tests: `python -B -m unittest tests.test_split_distribution -v`.
Full executable and GPU acceptance requires a successful native Release build.
