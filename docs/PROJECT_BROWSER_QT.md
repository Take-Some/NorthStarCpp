# NorthStarCpp — Qt Project Browser

**Implementation:** C++17 / Qt 6 Widgets  
**Icons:** Font Awesome 6 Free (bundled application font)  
**Default appearance:** Light  
**Engine launcher:** northstarcpp_project_manager.exe

## Native application architecture

The Project Browser is a standalone Qt Widgets executable compiled from:

- engine/northstarcpp/modules/project_manager/qt_project_browser.cpp
- engine/northstarcpp/modules/project_manager/project_catalog.cpp (shared native catalog)
- engine/northstarcpp/resources/fontawesome/fa-solid-900.ttf (Qt resource)
- icons/logoStar.ico (Qt resource and Windows executable resource)

The previous Win32/GDI browser and native dialog are no longer compiled into
northstarcpp_project_manager. The editor still starts the manager from C++.
Python is **not** used for the UI or for runtime project operations.

The browser provides project cards, a responsive grid, search by name/path/
category/tag, recent and favorite views, project details, and these operations:

On every initial display and Refresh, the native catalog **re-scans the actual
Projects directory** (including nested folders up to four levels deep), validates
each project manifest and its startup scene, and merges external paths already
registered in the catalog. Discovery is read-only: it does not implicitly modify
the favorites/history or overwrite the catalog. Symlinks and junctions are not
followed; traversal is bounded to 5,000 directories and 500 discovered projects.
The separate Scan Directory command remains available for registering projects
outside the default root.

Previously registered paths are metadata only, not extra project entries:
any directory that no longer exists, or has lost its project manifest, is
automatically omitted from the visible list. Existing external projects remain
visible if their manifest still exists, and their history/favorites are kept.
This reconciliation does not delete project files or rewrite the registry.
A project removed after its card was rendered is caught by a fresh inspection
at launch time, and the UI reports the problem without terminating.

- Create using the engine's native templates (Blank, Sandbox, Physics
  Playground, City Block, Architecture)
- Import an existing project and scan a directory for other projects
- Launch a project in the native editor
- Favorite, rename, duplicate, or remove a project from the list
- Open the project's folder in the operating system

Removing a project from the browser is intentionally non-destructive:
Catalog::forget() does **not** delete user files. All project metadata and
validations are delegated to the shared native C++ Catalog class.

## Build dependencies

On Windows/MinGW use Qt 6.4+ (Widgets) from the **same MinGW toolchain**
as NorthStarCpp. The tested environment uses Qt 6.10.1 from MSYS2 mingw64.

    C:\msys64\usr\bin\pacman.exe -S --needed mingw-w64-x86_64-qt6-base

CMake finds Qt using the compiler toolchain prefix and the QTDIR environment
variable; a missing Qt is a configuration error, not a silent fallback to GDI.

The Font Awesome font is embedded into the EXE using qt_add_resources. The
font file is **not** required on the user's machine at runtime. The SIL Open
Font License notice remains at licenses/FontAwesome/LICENSE.txt.

## Qt deployment and package validation

The project_manager target executes tools/deploy_qt.py *at build time* to
copy Qt dependencies and the qwindows platform plugin next to the executable.
It resolves transitive MinGW runtime dependencies rather than assuming they
are installed globally. The engine's release builder additionally runs
tools/deploy_qt.py to create a verified qt.runtime.json registry.

The deployer preserves Qt 6 license notices under licenses/Qt6Base and
the Font Awesome SIL OFL notice. Qt remains dynamically linked so binaries
can be replaced by users. Review all applicable LGPL obligations before
public redistribution.

Qt runtime DLLs are deliberately **not** engine capability modules. The
NorthStar plugin registry still tracks only engine.ecs, engine.physics,
engine.render, engine.audio, engine.input and engine.assets.

## Verification

Build and exercise the GUI without opening a persistent project:

    cmake --build out/qt-browser-debug --target northstarcpp_project_manager
    out\qt-browser-debug\northstarcpp_project_manager.exe --smoke

The smoke uses a temporary Catalog, creates a project, tests favorites,
search filtering, Qt resource initialization, and double-clicking a project
whose directory was deleted after discovery. No real project is deleted
or modified. The native catalog smoke also covers unregistered nested projects,
stale registered paths (including resurrection), folders without a manifest,
and missing startup scenes.

Additional checks:

    python -m unittest tests.test_qt_runtime -v
    python -m tools.build_dist --from-build out/<release-build> --build-type Release

The distribution builder smoke launches the installed Project Browser with
only the package and Windows system directories in PATH, demonstrating it
does not rely on a global Qt installation.

## Limitations

The current cards are generated from project metadata with Font Awesome
icons. Actual project-scene thumbnail renderers are a separate future
feature; the project catalog does not currently expose a thumbnail API.

## Engine API provider selection (2026-11-15)

The Qt Project Browser offers project-scoped **ENGINE API PROVIDERS** controls
when multiple ABI-compatible implementations of an `engine.*` capability are
available. The list is based on live validated module exports (external DLLs)
and the builtin inventory. It includes renderer (D3D11 / D3D12 / experimental
Vulkan) and physics (Bullet / Jolt) when installed.

A project may persist choices in `northstarcpp.project.json`:

```json
"providers": {"engine.render":"d3d12","engine.physics":"jolt"}
```

A missing entry means **Automatic**, retaining the established render
fallback. When opening a project with unselected multi-provider APIs, the
browser prompts for a choice. Choices are passed only to the child Editor via
`NORTHSTARCPP_RENDER_PROVIDER`, `NORTHSTARCPP_PHYSICS_PROVIDER`, and their
`engine.*` equivalents; global Windows variables are never overwritten.
Explicit selections must resolve to the chosen provider rather than silently
using another. Unavailable selections are highlighted and block launch.

The runtime and browser use the same normalized provider identity contract.
The provider's advertised ABI and function table are checked before listing;
this does not guarantee hardware support or feature parity.

Details and QA evidence:
`docs/NORTHSTAR_PROJECT_BROWSER_PROVIDER_SELECTION_2026-11-15.md`.

The updated isolated QA browser `out/project-manager-qt/qa_project_browser_provider_choices.exe`
passed `--smoke`; final official build/release packaging still require
an uncontended full build.
