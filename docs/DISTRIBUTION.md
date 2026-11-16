# NorthStarCpp: source architecture and distributable releases

## Repository boundaries

NorthStarCpp uses **one** module registry (`tools/cmake/NorthStarModules.cmake`). CMake's registered targets are the authority for plugin metadata, output directories, install destinations and shipping manifests. No release script should search arbitrary build-tree DLLs.

```text
CMakeLists.txt                 Global build, SDK/install exports
tools/cmake/
  NorthStarModules.cmake      Plugin registration and generated manifests
  NorthStarCppConfig.cmake.in SDK find_package configuration
engine/northstarcpp/modules/api/include/northstarcpp/   Public SDK and foundation headers
engine/northstarcpp/
  modules/
    host/                    Capability resolver, scene and subsystem adapters
    editor/                  Editor and player entry point
    viewport/                Rendering integration and viewport API
    shared/                  Common provider implementation helpers
    ecs/flecs/               Flecs provider
    physics/bullet/          Bullet provider
    physics/jolt/            Jolt provider (alternative)
    render/d3d11/            D3D11 provider
    audio/winmm/             WinMM provider
    input/win32/             Win32/XInput provider
    core/                   Active foundation/math implementation
    api/include/northstarcpp/ Maintained public ABI and compatible math headers
    # Historical source archive (not compiled or installed): core/northstarcpp/
sdk/vendor/sources/        Vendored physics dependencies
sdk/vendor/flecs/              Flecs source dependency
tools/shader_generator/      Maintained shader generator sources
tools/build_release.cmd      Clean Release build from current sources
tools/build_dist.py          Release builder + content validator
out/                         Intermediate objects and build outputs
distributable/               Releases from the standalone script
dist/                        Packages from the configurable dist command
```

The generated northstarcpp.plugins.json lists external and builtin providers. External DLLs ship beside the editor and player. Builtin entries describe providers linked into the engine binaries. At startup a compatible external module takes precedence over its builtin counterpart. See [module selection](MODULES.md).

## Build and package

Run the standalone Release script from the repository root:

```powershell
.\tools\build_release.cmd
```

The script builds the current source tree in a fresh GCC x64 Release directory under `out/distribution/`. It runs CTest, installs registered CMake components, verifies the plugin registry, launches the installed editor and player, builds an external SDK consumer, and checks imported DLLs. It publishes a versioned directory and verified ZIP under `distributable/`. The version comes from the CMake project.

Use the configurable `.\northstarcpp.cmd dist` command to produce packages under `dist/`.

For a fully built Release tree that has already been compiled, skip redundant compilation but still perform all CTest/package checks:

```powershell
.\northstarcpp.cmd dist --from-build out/gcc-release
```

For debug packages:

```powershell
.\northstarcpp.cmd dist --build-type Debug
```

To inspect and verify a published package without rebuilding:

```powershell
.\northstarcpp.cmd dist --verify dist/NorthStarCpp-0.1.0-windows-x64-Release
```

## Release folder

```text
NorthStarCpp-<version>-windows-x64-Release/
  bin/
    northstarcpp_player.exe
    northstarcpp_project_manager.exe         # native launcher
    northstarcpp_asset_browser.exe           # native launcher
    northstarcpp_editor_qt.exe               # native launcher
    northstarcpp_camera_timeline_editor.exe  # native launcher
    bugtrap.exe
    bootstrap.json
    AssetManager.dll
    physics_jolt.dll
    ... other registered engine providers ...
    archive/
      northstar_shared.nsp
      ... additional *.nsp asset packs ...
    qt_tools/
      northstarcpp_project_manager.exe       # real Qt application
      northstarcpp_asset_browser.exe
      northstarcpp_editor_qt.exe
      northstarcpp_camera_timeline_editor.exe
      Qt6Core.dll
      Qt6Gui.dll
      Qt6Widgets.dll
      Qt6Concurrent.dll
      platforms/qwindows.dll
      ... other private Qt/MinGW runtime DLLs and plugins ...
  tools/shader_generator.exe
  sdk/include/northstarcpp/...
  sdk/lib/*.a
  licenses/Qt6Base/...
  licenses/FontAwesome/...
  documentation/
  qt.runtime.json
  northstarcpp.plugins.json
  manifest.json
```

The **four public Qt executables always live directly in `bin/`**. They are
small native Windows launchers retaining the application's icon and file metadata;
the real Qt executables live in `bin/qt_tools/` with their Qt dependencies.
The launchers preserve Unicode command-line arguments, the working directory,
and the Qt child's exit status. Double-clicking the public executables does
not require installing Qt or changing the system PATH.

**All Qt DLLs and Qt platform plugins belong in `bin/qt_tools/`**, never in
the native Player's `bin/` directory. Isolated Qt processes load engine
providers and `bootstrap.json` from parent `bin/`. Release validation
checks launcher hashes and rejects Qt6 DLL leakage into root `bin/`.
This layout applies even when the distribution has no external V8 provider.

All first-party and additional `.nsp` packs are installed in `bin/archive/`.
The baseline `northstar_shared.nsp` is generated from the editable sources
in `engine/northstarcpp/resources/sharedAssets/`.

Release distributions include an editable `bin/bootstrap.json` with native
`plugins.path` defaults. No Qt SDK or compiler toolchain is required on an
end-user machine.

### Qt launcher tests

```powershell
python -m unittest tests.test_qt_runtime -v
python tests/qt_release_launcher_integration.py --build build/foundation-migration --gpp C:/msys64/mingw64/bin/g++.exe
```

The integration test stages a temporary Qt distribution, launches the public
Project Manager from root `bin/` with no Qt on PATH, and verifies the Qt
application actually executes from `bin/qt_tools/`.

## C++ SDK

The SDK ships public headers under `sdk/include/northstarcpp` and compiled
libraries under `sdk/lib`. Public vectors, matrices and bounds are declared in
`<northstarcpp/math.hpp>`. Link `NorthStarCpp::northstarcpp_runtime` for the
runtime and its math dependency, or `NorthStarCpp::northstarcpp_math` for math,
geometry and collision queries alone. The compatibility source tree stays in
the source checkout.

## Adding plugins

Create a CMake SHARED library, link its SDK dependencies, then register it with `northstarcpp_register_plugin(TARGET ... CAPABILITY ... PROVIDER ...)`. The existing `northstarcpp_module` wrapper in `engine/northstarcpp/CMakeLists.txt` handles standard provider source files.

Each registration supplies capability, provider identity and ABI, output directory and `cmake --install` rules. Avoid copying DLLs by hand or relying on a fixed filename list in `build_dist.py`; its module file set is taken from the generated CMake registry and validated against actual installed files.

## Compatibility, dependencies and distribution rights

Release packages contain GNU GCC-compiled engine apps, plugins and SDK, not the entire original game implementation. They do not bundle unregistered third-party binaries. The shader generator uses `d3dx9_43.dll`, which is an **external prerequisite** from the Microsoft DirectX End-User Runtimes (June 2010); it is recorded in the manifest, not copied from a local system.

`licenses/` contains original third-party license files for Flecs (MIT), Bullet (zlib) and Jolt (MIT). Original engine copyright notices must remain intact. **Successful packaging is not a grant of intellectual-property rights to publicly redistribute third-party engine code**; review code provenance and relevant permissions before distributing externally.

The build validates package hashes, registered modules and ZIP integrity. To recheck a standalone release after publication, run:

```powershell
python -B -m tools.build_dist --output-dir distributable --verify distributable/NorthStarCpp-0.1.0-windows-x64-Release
```
