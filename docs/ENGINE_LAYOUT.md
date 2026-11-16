# NorthStarCpp — Canonical Source Layout

Updated: 2026-11-14

Refer to ARCHITECTURE.md for the directory contract, dependency direction
and extension rules. The machine-readable owner/target catalog is
architecture/module_catalog.json.

- First-party source: engine/northstarcpp/modules/
- First-party resources: engine/northstarcpp/resources/
- Public SDK: engine/northstarcpp/modules/api/include/northstarcpp/
- Third-party SDK and libraries: sdk/vendor/
- Build and release automation: tools/
- Validation and regression: tests/
- Projects and game assets: Projects/ and sharedAssets/
- Generated objects: out/; release packages: dist/

All source-root variables used by CMake are declared in
tools/cmake/NorthStarPaths.cmake. The provider registry is implemented in
tools/cmake/NorthStarModules.cmake.

Run:

    python tools/verify_architecture.py --json
    cmake -S . -B out/layout-validation -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build out/layout-validation --parallel 4
    ctest --test-dir out/layout-validation --output-on-failure

The historical tools/organize_engine_layout.py was a one-time migration utility
for an older layout. Do NOT call --apply again. The current SDK migration has
its own journal at .northstarcpp/migrations/sdk-layout-v2.json, and does not
depend on the older journal being present.


## Native foundation runtime integration (2026-11-14)

Original first-party engine source:

    engine/northstarcpp/modules/core/northstarcpp/

This source tree is now owned by the engine. The previous
northstarcpp/compat include prefix was migrated to
northstarcpp/foundation. Do not reintroduce a separate compat tree.

Directly compiled original translation units (no compatibility bridge):

- northstarcpp_math contains 12 original math and geometry source units.
- northstarcpp_core compiles the original geometry_utils.cpp,
  strings_functions.cpp, buffer_string.cpp, fs_path.cpp, fs_path_win.cpp.
- northstarcpp_runtime PUBLICLY links both libraries as first-party
  runtime dependencies. The five core objects are really present in
  libfoundation_core.a, not merely registered as header-only sources.

SDK packaging:

- The active historical-header dependency closure (64 files) is maintained under
  engine/northstarcpp/modules/api/include/northstarcpp/.
- Original inactive source archives are NOT installed into the SDK.
- Compiled static libraries expose their corresponding public APIs.
- The original source archive remains in core/northstarcpp for provenance only.

Coverage: 1,150 original C++ translation units and 1,585 .h headers
currently remain in the tree. 17 original C++ units are now compiled
directly, while 1,133 still require porting or appropriate classification.
PS3/Xbox variants, obsolete editor tools and Maya integrations are
not runtime implementations for the current Windows build. The original
animation, collision, physics, audio, networking, rendering and resource
managers require a deliberate subsystem migration; bulk-linking all of
them would conflict with existing modern provider implementations.

Any additional port must compile the ORIGINAL C++ implementation directly
within its native engine target, preserve ABI contracts, fix platform
dependencies, and pass native runtime tests. No compatibility wrapper
or blind duplicate algorithm implementation should be substituted.

Verified in QA build out/compat-native-qa-20261114:

- CMake configuration PASS.
- northstarcpp_math CTest PASS: original strings, buffer strings, FS paths
  and geometric algorithms are exercised directly.
- northstarcpp_runtime, northstarcpp_viewport, northstarcpp_projects build PASS.
- Engine layout, architecture and math tests PASS (3/3).
- Isolated CMake SDK install PASS with both libraries, all source files
  and all original headers.

The full northstarcpp_player clean build, complete native legacy source
migration and production dist replacement are NOT complete.
