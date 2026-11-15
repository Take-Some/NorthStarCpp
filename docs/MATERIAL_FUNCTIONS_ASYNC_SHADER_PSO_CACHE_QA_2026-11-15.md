# NorthStarCpp — Material Functions, asynchronous shader variants and persistent PSO cache

Date: 2026-11-15
Workspace: C:/Users/AidenFox/Documents/NorthStarCpp
QA build: out/material-functions-qa-20261115
Build: Windows x64, Qt 6, C++17, D3D11/D3D12 WARP
Plugin ABI: unchanged
Production dist: unchanged
Commit/push: not performed

## Implementation — Material Functions

A first-party typed Material Function asset format was added:

- Source extension: .material-function.json
- Schema: northstar.material-function.v1
- Up to 8 inputs and 8 outputs, each scalar/vector2/vector3/vector4
- Multiple named outputs with output selection at the call node
- Nested functions and strict typed boundaries (type_assert)
- Recursive dependency cycle detection and bound expansion depth
- Limits: source <=32 KiB, <=16 dependencies, <=64 expanded nodes
- Safe VFS resolver; no direct user-supplied HLSL
- Compiler lowers function calls to the existing typed Material Graph DAG and HLSL macros.

The runtime loads source through AssetStore in material.cpp. The Qt Material Editor can add Material Function nodes, connect named typed inputs, select function outputs, copy/paste function nodes, and validate the function graph. The GPU Preview preflights nested graph dependencies with QtConcurrent, watches the function files for changes, compiles into isolated scratch content and retains the last valid GPU material after an error. The Content Browser recognizes AssetKind::material_function and displays the typed signatures.

The initial first-party function editor supports signature visualization, bounded JSON editing, validation of every output, dependency validation and atomic QSaveFile save. A full Blueprint-style internal visual function graph editor and simultaneously exposed multi-output sockets remain open UI improvements.

Demo project assets:
- Projects/NorthStarFirstPersonPlatform/assets/MaterialFunctions/MultiplyColor.material-function.json
- Projects/NorthStarFirstPersonPlatform/assets/MaterialFunctions/SoftTint.material-function.json
- Projects/NorthStarFirstPersonPlatform/assets/Materials/SoftTintDemo.material.json

## Implementation — asynchronous GPU shader and PSO variants

D3D11:
- Nonblocking std::async background D3DCompile for material_ps ps_4_0.
- Render thread performs CreatePixelShader when its future is ready.
- In-process memoization of ready variants, 128-entry bound.
- At most 8 pending compile jobs.
- If the device rejects cached bytecode, evict and retry once from source.
- On compilation errors, quarantine the failing key and continue rendering using the base shader.

D3D12:
- Nonblocking std::async shader compilation and true CreateGraphicsPipelineState for graph variants.
- Device and root signature are retained by ComPtr inside each worker; workers do not borrow the renderer or draw-item memory.
- At most 8 pending workers and 128 ready in-process cache entries.
- Cache keys include HDR mode, blend mode, shader source and adapter LUID for persistent blobs.
- Render thread polls for ready PSO each frame; opaque variants use the base material PSO while pending. Transparent variants do not draw until their compatible PSO becomes available.
- Failed variants are quarantined, logged to stderr, and do not crash the renderer. New valid variant keys remain compilable.

## Persistent disk shader/PSO cache

Internal header: engine/northstarcpp/modules/render/shared/shader_disk_cache.hpp

Default storage: LOCALAPPDATA/NorthStarCpp/ShaderCache/v1/<backend>/
Override: NORTHSTARCPP_SHADER_CACHE_DIR=<absolute directory>
Disable: NORTHSTARCPP_SHADER_CACHE_DISABLE=1

- D3D11 stores compiled DXBC shader bytecode.
- D3D12 stores actual ID3D12PipelineState::GetCachedBlob data.
- CachedPSO is verified by the D3D12 runtime before use; a rejected cache entry is discarded and compiled from scratch.
- Bounded file payload <=8 MiB, versioned header, source-key hash and payload checksum. Invalid files are ignored and replaced with new valid bytes.
- Cache saves use temporary files followed by atomic MoveFileExW replacement; no GPU work is performed by the cache IO caller on the render thread.
- CMake WARP tests isolate cache storage inside the QA build directory.

Note: This is an initial persistent cache, not a cryptographically authenticated artifact store. Global disk-budget eviction/retention policy and shader cache statistics reporting remain future work. Repeated renders benefit from cached variants, but no measured FPS or p95/p99 speedup is claimed.

## Validation results

Fresh isolated Release QA build successfully linked:
- out/material-functions-qa-20261115/northstarcpp_editor_qt.exe
- out/material-functions-qa-20261115/render_d3d11.dll
- out/material-functions-qa-20261115/render_d3d12.dll

Final targeted CTest run: 16/16 PASS, total 7.85 seconds.
Tests:
- northstarcpp_material_functions
- northstarcpp_material_graph_typed
- northstarcpp_material_graph_hlsl
- northstarcpp_shader_disk_cache
- northstarcpp_d3d11_material_warp
- northstarcpp_d3d12_material_warp
- northstarcpp_asset_catalog
- northstarcpp_material_resources
- northstarcpp_skeletal_asset_interop
- northstarcpp_character_namespace_rebase
- northstarcpp_editor_asset_pipeline
- northstarcpp_qt_material_function_gpu
- northstarcpp_qt_material_graph_authoring
- northstarcpp_qt_material_instance_authoring
- northstarcpp_qt_editor_authoring
- northstarcpp_qt_editor_gpu_surface

GPU WARP readback tests were updated to require eventual green Graph shader pixels after a temporary fallback. Both providers passed cold and repeat runs. WARP cache output observed:
- D3D11 compiled bytecode 5,160 bytes.
- D3D12 PSO blob 7,874 bytes.
- Both reran successfully in independent processes.
- Deliberate corruption of each cached blob was detected and recreated byte-for-byte.
- Invalid HLSL with unknown function was rejected and logged as a warning; rendering continued, and a subsequent valid green variant appeared in GPU pixel readback on both providers.
- Unit regression verifies cache key integrity, corrupt file rejection, successful rewrite and disabled mode.

Git diff --check: PASS (exit 0). No production runtime installation, git commit or push performed.

## Next acceptance priorities

P1: Full Blueprint-style visual editor for internal Material Function graphs, with input/output pin nodes and multiple output sockets on function calls. Introduce first-party presets and reusable parameter collections.

P1: Shader variant prewarming based on dependency graph, GPU/CPU timeline instrumentation, cache hit/miss metrics, and p50/p95/p99 across at least 1,000 frames. Add global persistent cache eviction bounds.

P1: Full D3D11/D3D12 hardware GPU regression (QA so far uses WARP); verify correct materials across resizes, device resets and rapid graph edits. Test PSO driver upgrade invalidation.

P2: Professional UX, editable HDRI, exposure/light rig, preview pixel comparison and content browser workspaces.

No full equivalence with Unreal Engine Material Editor is asserted.
