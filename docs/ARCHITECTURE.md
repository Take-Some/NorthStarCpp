# NorthStarCpp — Repository Architecture

Updated: 2026-11-14. Canonical source layout: SDK / engine / tools / tests are distinct.

## Directory contract

    NorthStarCpp/
    ├── architecture/
    │   └── module_catalog.json              # Reviewed module, target and contract ownership
    ├── sdk/
    │   ├── include/northstarcpp/             # Public C/C++ APIs and versioned plugin ABIs
    │   └── vendor/                           # Third-party sources, headers, libraries, licenses
    │       ├── flecs/                        # Git submodule
    │       ├── sources/
    │       │   ├── JoltPhysics/              # Git submodule
    │       │   ├── libvorbis/                # Xiph.Org Vorbis v1.3.7 (Git submodule)
    │       │   ├── bullet-3.25/
    │       │   └── blake3-1.8.7/
    │       ├── include/                      # Vendor-only headers, e.g. Boost
    │       ├── libraries/                    # Vendor prebuilt libraries
    │       └── bcdec/
    ├── engine/
    │   └── northstarcpp/
    │       ├── CMakeLists.txt                # Targets and provider registrations
    │       ├── modules/                      # Only first-party implementation code
    │       │   ├── foundation/
    │       │   ├── host/
    │       │   ├── editor/
    │       │   ├── project_manager/
    │       │   ├── viewport/
    │       │   ├── shared/
    │       │   ├── tools/
    │       │   ├── assets/directory/
    │       │   ├── ecs/flecs/
    │       │   ├── physics/{bullet,jolt}/
    │       │   ├── render/{d3d11,d3d12}/
    │       │   ├── audio/winmm/
    │       │   ├── input/win32/
    │       │   ├── logger/multisink/
    │       │   ├── perlin/improved/
    │       │   └── scripting/{v8,v8_sidecar}/
    │       └── resources/
    │           ├── icons/
    │           ├── fontawesome/
    │           └── *.rc.in
    ├── tools/                                # Development, V8 build and packaging automation
    ├── tests/                                # Unit, integration and release regression
    ├── docs/                                 # Design and operational reference
    ├── config/                               # Engine defaults and config templates
    ├── Projects/                             # Project workspaces
    ├── sharedAssets/                         # Runtime samples and authored assets
    ├── out/                                  # Generated files (gitignored)
    └── dist/                                 # Approved releases only (gitignored)

## Boundaries

1. **Public API (engine/northstarcpp/modules/api/include/northstarcpp)** contains stable versioned host
   and plugin contracts. No provider implementation may be stored here.
2. **External dependencies (sdk/vendor)** retain original licenses and upstream
   revisions. Do not edit vendor code for cosmetic reorganization.
3. **First-party source (engine/northstarcpp/modules)** is owned by NorthStarCpp.
   A capability provider is registered through NorthStarModules.cmake and
   compiles independently of its host interface.
4. **Engine resources (engine/northstarcpp/resources)** include Windows icon
   assets, Qt resources and build-time manifests. Game assets remain in
   sharedAssets/ or project-owned asset trees, not beside sources.
Versioned V8 binary SDKs belong under sdk/vendor/v8/; upstream V8
source snapshots belong under sdk/vendor/sources/. Neither belongs in
the engine implementation or public NorthStar ABI include directories.
See docs/V8_VENDOR_SDK_DOWNLOAD_2026-11-14.md.

5. **Automation (tools)** contains build, QA, packaging and optional V8 SDK
   provisioning scripts, NOT bundled V8 binary SDKs. An external V8 SDK
   is selected via NORTHSTARCPP_V8_ROOT.
6. **Build products (out/ and dist/)** do not belong in source or SDK trees.

## Dependency direction

    Applications (Editor / Player / Project Manager)
      -> Engine host services (versioned capability interfaces)
         -> Runtime module registry / replaceable providers
            -> Public C/C++ SDK contracts
            -> Third-party libraries (only in concrete providers)

The host must not directly depend on a provider's Flecs, Bullet, Jolt or V8
implementation. Builtin and external providers share capability ABI contracts;
external implementations may replace a compatible builtin provider.
Not every engine.* API can be claimed replaceable without registration/tests.

## Traceability

The version-controlled architecture/module_catalog.json is the owner map:
module ID, source path, responsibility, target(s), public contract (if any),
provider capability and external Git-submodule URL. Build and installed
plugin metadata is produced by tools/cmake/NorthStarModules.cmake as
northstarcpp.plugins.json. tools/verify_architecture.py checks the catalog,
filesystem boundaries, public headers and CMake registrations in CI and CTest.

## Adding a module

1. Assign an owner, role and target in architecture/module_catalog.json.
2. Put first-party implementation in engine/northstarcpp/modules.
3. Put public ABI contracts in engine/northstarcpp/modules/api/include/northstarcpp.
4. Register CMake target/provider capability and runtime override behavior.
5. Add ABI, provider-substitution, asset and release regression tests.
6. Run source layout verification, incremental build and the full CTest suite.

## Acceptance commands

    python tools/verify_architecture.py --json
    cmake -S . -B out/layout-validation -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build out/layout-validation --parallel 4
    ctest --test-dir out/layout-validation --output-on-failure

Never modify production dist during a source-layout refactor.

## Migration history and rollback

On 2026-11-14 the public headers, vendor SDK, Flecs and Jolt Git submodules,
and icon resources were relocated. The original file bytes were preserved.
Move journal: .northstarcpp/migrations/sdk-layout-v2.json.
Configuration backups: .northstarcpp/backups/2026-11-14-sdk-layout-v2.
Git records tracked renames. For rollback follow journal source/destination
pairs and restore the backed-up configuration files. Do not delete projects,
QA outputs or production dist. No Git commit was created automatically.

### Audio SDK dependency: libvorbis

Official full upstream Git repository: https://github.com/xiph/vorbis.git.
Pinned submodule: sdk/vendor/sources/libvorbis at v1.3.7 (commit
0657aee69dec8508a0011f47f3b69d7538e9d262). Source includes libvorbis,
libvorbisenc, libvorbisfile, examples, tests, CMake and BSD license COPYING.
The historical sdk/vendor/sources/libvorbis-1.2.3 tree is retained untouched
as a legacy VS2008 project artifact. Vorbis requires separately versioned
libogg headers/libraries for linking; cloning alone does not activate a
Vorbis audio provider in the engine build.

## Provider override precedence (engine.* ABI contract)

A native builtin provider is the default, not the exclusive implementation.
A replaceable engine.* API uses the same versioned C ABI whether it is
compiled into the engine or supplied by an external author.

Priority order:
1. Explicit author-specified external module via the capability-specific
   NORTHSTARCPP_*_PLUGIN environment variable, then bootstrap.json
   plugins.overrides[capability]. The environment variable takes precedence.
2. Compatible external provider found in bootstrap.json plugins.path (or
   executable directory). Windows accepts DLL, POSIX accepts SO.
3. Builtin provider compiled into the runtime.

Every candidate must match the capability ID, host ABI, requested API ABI,
API table size and required callbacks. An invalid automatically discovered
module is skipped. An invalid *explicit* override is a configuration error,
not a silent fallback that hides the author's chosen provider.

The host and its clients use provider-neutral ABI tables. Replacing a provider
must not require rebuilding the engine, Player or Editor. Provider handles
are kept alive while their API tables/resources remain in use. Optional
extension ABIs are resolved only on the selected provider, not borrowed from
a different builtin. For interfaces not yet registered as replaceable engine.*
capabilities, this is a migration requirement, not an existing feature.

Regression tests: northstarcpp_foundation_geometry_override and the
corresponding engine.perlin override smoke.
