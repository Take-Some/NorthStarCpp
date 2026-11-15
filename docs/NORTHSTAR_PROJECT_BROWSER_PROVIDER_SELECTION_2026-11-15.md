# NorthStarCpp — Project Browser Provider Selection

**Date:** 2026-11-15. **Project:** NorthStarCpp. **ABI compatibility:** Module ABI 1, Render ABI 5 unchanged.

## Implemented

The Qt Project Browser now discovers multiple ABI-compatible implementations of a given `engine.*` API and displays an **ENGINE API PROVIDERS** section in the selected project details. Every capability with multiple options receives a selectable provider control; previously stored missing providers are explicitly shown as unavailable. Choices may be reset to **Automatic**. If a project has not selected among several providers, an Editor-launch dialog offers choices. The selected Vulkan implementation is labelled **experimental**.

The Project Catalog stores per-project preferences in `northstarcpp.project.json` under the `providers` object using literal dotted keys:

```json
"providers": {
  "engine.render": "d3d12",
  "engine.physics": "jolt"
}
```

An absent entry means Automatic. Existing project metadata is preserved, written atomically with validation, and copied when a project is duplicated.

The Qt Browser enumerates external DLL candidates using `northstar_module_query`, validating host/API ABI and the actual function table; it merges builtin choices from the generated `northstarcpp.plugins.json` inventory. Scanning is bounded and does not instantiate devices. The descriptor's display name and the stable selection ID are mapped consistently by `northstarcpp/provider_identity.hpp` (e.g. Direct3D 11 -> d3d11, Directory VFS -> directory, Bullet -> bullet, Jolt -> jolt).

**Runtime:** The child Editor process receives only its project's selected `NORTHSTARCPP_<CAPABILITY>_PROVIDER` environment variables, without changing the parent/global Windows environment. Inherited provider choices for known APIs are cleared when the project uses Automatic selection. `Module` resolves matching external DLLs or builtin implementations, rejects missing providers, and rejects mismatches between selected IDs and explicit plugin DLL paths. `Viewport` honors an explicit render provider without silently falling back. In Automatic mode the established D3D12-first -> D3D11 fallback remains.

## Changed files

- `engine/northstarcpp/modules/project_manager/qt_project_browser.cpp`
- `engine/northstarcpp/modules/project_manager/provider_inventory.cpp/.hpp`
- `engine/northstarcpp/modules/project_manager/project_catalog.cpp`
- `engine/northstarcpp/modules/api/include/northstarcpp/project_catalog.hpp`
- `engine/northstarcpp/modules/api/include/northstarcpp/provider_identity.hpp`
- `engine/northstarcpp/modules/host/module_loader.cpp`
- `engine/northstarcpp/modules/viewport/viewport.cpp`
- `engine/northstarcpp/CMakeLists.txt`
- `tests/project_catalog_smoke.cpp`
- `tests/project_provider_runtime_smoke.cpp`

## Acceptance evidence

**Qt UI integration:** Manually linked an isolated updated test executable at `out/project-manager-qt/qa_project_browser_provider_choices.exe`. Its `--smoke` **passed with exit 0**, including live discovery of multiple renderer/physics DLLs and AssetManager, actual GUI selection of Vulkan, reset to Automatic, manifest round-trip, saved missing project handling and detached child receiving both Vulkan and Jolt selection. The smoke passed even with stale contradictory environment preferences inherited from the browser's parent.

**Catalog:** Isolated native `qa_provider_selection/qa_project_catalog_provider_selection.exe` **passed, exit 0**, covering atomic preference persistence, duplicate inheritance, clearing a single selection and rejection of malformed IDs.

**Real Module loader:** Isolated `qa_runtime_provider_selection.exe` used the updated runtime module loader to resolve actual DLLs. **8/8 tests passed**: D3D11, D3D12, Vulkan, Jolt, Bullet, Directory VFS, missing renderer rejection, and explicit DLL/provider conflict rejection.

All five edited C++ production translation units passed the MinGW compiler. The whole production Project Browser build was **not** completed: existing shared Ninja/CMake build trees were being used concurrently and failed at build-file recompaction with `Permission denied`. For QA, changed objects were compiled and linked into isolated executable/library names within the existing QA directory without overwriting the currently installed project manager.

## Limitations

- This changes provider **selection**, not functional parity: Vulkan is experimental and lacks the complete D3D12 PBR/material/sky/HDR feature set.
- The bounded live scan currently recognizes known first-party DLL naming conventions and AssetManager.dll. A package manifest should enable arbitrary third-party filenames without blindly loading unrelated DLLs.
- When distinct DLL files claim the same capability/provider ID, the existing external-over-builtin/loader scan precedence applies. A future selection contract can carry an authenticated source ID or exact plugin reference.
- A clean, uncontended full Editor/Player build and runtime scene integration are still required.
- **No production `dist` deployment, Git commit or push.** Previously existing uncommitted changes were preserved.
