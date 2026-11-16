# KaylaNightCity native Jolt first-frame crash fix — NorthStarCpp 0.6.1

**Status:** Root cause identified, hotfix implemented and reproduced scene passed targeted live QA. This is a QA-only hotfix of the existing 0.6.1 source, not an official production release.

## Evidence

- Source project: `Projects/KaylaNightCity`, startup menu `menu.nscn`, playable world `main.nscn`.
- Earlier fatal crash reports: `%LOCALAPPDATA%/KaylasSystems/NorthStarCpp/Crashes/crash-20261116-192828-486-6952-9808.txt` and later `crash-20261116-193303-100-6952-9336.txt`; exception `0xE0000002` (native SIGABRT).
- The title scene completed 24 startup phases, with outcome `READY`; last project-script message before the original abort was `Title scene -> loading main.nscn`.
- Launching `main.nscn` directly reproduced the abort. GDB with an extended **QA-only** V8 deadline caught `msvcrt!abort` during the first frame, with this call stack:

  ```
  msvcrt!abort
  JPH::TempAllocatorImpl::Allocate(inSize=68000)
  JPH::IslandBuilder::PrepareContactConstraints(inMaxContacts=17000)
  JPH::PhysicsSystem::Update
  ```

- The upstream Jolt file `sdk/vendor/sources/JoltPhysics/Jolt/Core/TempAllocator.h` explicitly calls `std::abort()` when the fixed `TempAllocatorImpl` exceeds its buffer size.
- Our provider `engine/northstarcpp/modules/physics/jolt/jolt.cpp` instantiated `TempAllocatorImpl(8 * 1024 * 1024)`. This was insufficient for the current contact-rich world. No mesh simplification or collision removal was required.

## Code changes

1. Replaced the Jolt World member `j::TempAllocatorImpl temporary_allocator` with the upstream `j::TempAllocatorImplWithMallocFallback temporary_allocator`. The constructor retains an 8 MiB preallocated scratch arena, but extra temporary allocations fall back to malloc rather than aborting on peak contact load.
2. Added optional environment variable `NORTHSTARCPP_QA_SCRIPT_TIMEOUT_MS` when constructing the script runtime in `modules/editor/window.cpp`, bounded between 1,000 and 60,000 ms. This was used to prevent GDB overhead from triggering a separate V8 evaluation timeout. **Ordinary gameplay script timeouts are unchanged** when the variable is not set.
3. Updated `Projects/KaylaNightCity/run.cmd` to prefer the fixed QA-installed Player, then the 0.6.1 build Player, before older testing builds. An explicitly configured `NORTHSTARCPP_PLAYER_EXE` continues to override this selection.

## Rebuild / deployment

- Full CMake Release rebuild completed successfully.
- QA-installed bundle: `out/qa/NorthStarCpp-0.6.1-KaylaNightCity-JoltFix`.
- CMake installation successful; Qt deployment and private runtime isolation successful (345 installed files, four Qt launchers).
- `bin/physics_jolt.dll` is byte-for-byte identical (SHA-256) to the freshly rebuilt provider; `bin/northstarcpp_player.exe`, Jolt provider, D3D12 provider and private Qt Editor report `0.6.1.0`.
- Existing production distribution and generic `out/qa/NorthStarCpp-0.6.1-windows-x64-Release` were not replaced.

## Validation

- Real KaylaNightCity game world, new compiled provider: remained alive for **17 seconds** under extended QA V8 deadline; stopped intentionally by the test harness (no abort).
- Real game with **default** V8 deadline: remained alive for **13 seconds**; stopped intentionally (no abort).
- QA-installed Player with default V8 deadline: remained alive for **16 seconds**; stopped intentionally (no abort).
- Player startup JSON: `READY` / 24 passed / 0 failures, including first frame, on installed QA game run.
- Project JavaScript test `Projects/KaylaNightCity/tests/main_menu_smoke.cjs`: PASS (menus, scene requests, persistence, pause).
- Installed Qt Editor `--smoke`: PASS. Installed Qt Editor `--gpu-smoke`: PASS.
- Jolt-focused CTest: `northstarcpp_player_blueprint_physics`, `northstarcpp_jolt_triangle_materials`, `northstarcpp_jolt_residency`, `northstarcpp_jolt_vehicle_abi` passed; `northstarcpp_native_vehicle_scene`, `northstarcpp_jolt_selected`, and `northstarcpp_jolt_physics` remain failing and need separate investigation. The latter two still report a player not stopping against a static collider; they do not negate the targeted scratch-memory abort fix but block a clean full QA gate.
- Material import can warn about unresolved YMAT slots and neutral PBR fallbacks; independent of this abort.

**Limitations:** This validates the initial crash and short-duration game stability, not extended driving, correctness of road collisions, long-term memory consumption, or a fully passing 286-test production release. No commit, push or production promotion performed.
