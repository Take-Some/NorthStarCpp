# NorthStarCpp Qt Editor — Engine GPU Viewport and ECS Component Inspector

Date: 2026-11-14
Repository: C:/Users/AidenFox/Documents/NorthStarCpp
Build: out/editor-ue-final-qa-20261114/northstarcpp_editor_qt.exe

## Implemented

### Native GPU Viewport integration

- Added Engine GPU (D3D11 / Windows) to Qt editor viewport modes.
- A Qt-owned native child surface is passed to the existing
  northstarcpp::Viewport, which loads the replaceable engine.render
  module through ABI 5. No independent editor-only renderer was added.
- Qt retains selection, camera navigation, gizmo interaction and asset
  drag/drop. The child surface forwards relevant events to Qt authoring.
- Scene snapshots, selected entity and editor camera pose are submitted
  to Viewport::draw using the engine's existing gizmo contract.
- Asset source roots pass through Viewport::set_asset_sources, using
  the current Asset Manager provider rather than direct renderer I/O.
- An initialization/draw error falls back to the cross-platform Qt proxy.
  Switching modes releases the engine swapchain before hiding its window.
- Windows Qt build explicitly depends on render_d3d11.dll and
  assets_directory.dll, required to run the GPU viewport in a QA build.

IMPORTANT: This is the existing D3D11/Windows HWND render provider, NOT
Vulkan or a platform-neutral GPU target. It may use WARP if hardware
D3D11 initialization fails. A successful draw does not establish that
hardware GPU execution, PBR material parity, or full lighting is ready.

### ECS Component Inspector

- Added ECS Components panel inside the existing Qt Details dock.
- Displays stable plugin type ID, version, size and provider state for
  selected-entity or scene-level component blobs.
- Exposes thread-safe read-only plugin registry schema metadata:
  scene_component_schema and list_scene_component_schemas.
- Add, Edit Raw and Remove are undoable, go through validated Scene
  transactions, and preserve unknown plugin component records.
- The hex editor requires even-length hexadecimal input and limits
  edits to 1 MiB, with a conservative 128 KiB interactive edit cutoff
  for existing large components.
- Plugin validators run where registered and where versions match.
  Unsupported versions remain byte-preserving opaque data.

IMPORTANT: The current plugin schema describes IDs, version ranges and
validation callbacks, not typed fields or editor UI. This is a versioned
binary component inspector, not yet reflection-driven per-property editing.

## Verification

- Release Qt Editor compilation: PASS, exit code 0.
- CTest northstarcpp_qt_editor_authoring: PASS.
  Tests scene hierarchy, transformations, Undo/Redo, bookmarks,
  opaque plugin component NEF8 roundtrip, real modal hex editor
  changes to bytes 00 FF AA 7E and version 18, and second roundtrip.
- CTest northstarcpp_qt_editor_gpu_surface: PASS.
  Creates native Qt HWND, invokes actual engine.render GPU API draw,
  then releases renderer and restores Qt authoring proxy.
- Qt editor --player-check: PASS with separately built native Player.
- git diff --check for affected code: PASS.

Reproduce:

    cmake --build out/editor-ue-final-qa-20261114 --target northstarcpp_editor_qt
    ctest --test-dir out/editor-ue-final-qa-20261114 -R ^northstarcpp_qt_editor_(authoring|gpu_surface)$ --output-on-failure

## Remaining to reach UE5 functionality

1. Portable Vulkan/D3D12 GPU render-target contract, Qt interop,
   proper RenderGraph, GPU timestamps and performance profiling.
2. GPU ID picking, full materials/textures/PBR/shadows and lighting.
3. ECS plugin reflection fields and typed component editors.
4. World Partition, HLOD, NavMesh and collision authoring overlays.
5. Embedded Play-In-Editor with isolated ECS/physics state, not external
   Player execution.
6. Visual acceptance under heavy projects, frame-time p50/p95/p99
   and large-world responsiveness tests.

Production dist unchanged. No claim of full UE5 parity.
