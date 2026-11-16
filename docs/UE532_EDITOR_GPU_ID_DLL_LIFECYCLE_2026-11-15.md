# NorthStarCpp Editor P0 — ECS DLL Lifecycle & GPU ID Picking

Date: 2026-11-15
Workspace: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`
Isolated Release QA build: `_qa_p0_ecs`

## Delivered: external DLL ECS Reflection lifecycle

The existing stable scene-component C host API is extended (without changing its ABI 1 prefix) by the optional export:

```cpp
extern "C" uint32_t northstarcpp_register_scene_components_v1(
    const NorthStarSceneComponentHostApi* host);
```

Only the **selected**, compatible external provider is allowed to register types during `Module::resolve`. Schema metadata is copied into host storage, the host counts acquisitions by OS DLL handle, rolls back partial registration if the callback fails, and unregisters schemas **before FreeLibrary** after the last module is released. The C validator is never invoked after its library unload.

Files:
- `engine/northstarcpp/modules/api/include/northstarcpp/scene_component_plugin.h`
- `engine/northstarcpp/modules/api/include/northstarcpp/scene_component_registry.hpp`
- `engine/northstarcpp/modules/host/scene_component_registry.cpp`
- `engine/northstarcpp/modules/host/module_loader.cpp`
- `tests/scene_component_lifecycle_fixture.cpp` (new external DLL fixture)
- `tests/scene_component_lifecycle_smoke.cpp` (new)
- `engine/northstarcpp/CMakeLists.txt` (new standalone lifecycle CTest target)

The real Windows LoadLibrary/FreeLibrary CTest validates failure rollback, metadata visibility, typed write/read, two concurrent Module refs to the same DLL, final unload, opaque payload preservation and fresh re-registration after reload. This test is linked with the exact host loader and ECS registry sources, isolated from unrelated physics runtime dependencies.

## Delivered: D3D11 GPU ID attachment, ABI-safe extension

Added optional `NorthStarRenderPickApi` v1 with `NORTHSTARCPP_RENDER_PICK_MODULE_ABI = 0x52504b01`. The renderer's existing ABI 5 table is unchanged.

D3D11 `RendererState` collects a small transform + draw-ID record for visible cube primitives during scene rendering. Only on click, it generates vertices for a dedicated RGBA8 color ID pass and D24S8 depth attachment. It reads a single 1x1 pixel through GPU staging, decodes unsigned 24-bit IDs and maps renderer draw indices back to authored actor indices. Zero is reserved for the empty background.

A critical bug discovered by the overlapping-object QA was fixed: the shared `add_cube` helper scales RGB by per-face shading factors. Those visual colors **must not** be reused as GPU IDs, since 2/255 becomes 1/255 and aliases another object. The ID pass explicitly overwrites all generated vertex colors with **exact unlit normalized ID channel values**; D3D11 uses a separate depth attachment with LESS_EQUAL/clear depth 1.0. This now passes a real-hardware overlap test selecting the closer of two cubes.

The Qt native Viewport calls the GPU ID API when available, and falls back to exact scene ray geometry when unsupported or when meshes/materials are active. This is intentional: cube-only ID buffers must not select an object hidden behind a YDD/YDR mesh. GPU ID picking for Vulkan, D3D12, GPU mesh/character IDs, async readback and DPI scaling remains a future P0 task.

Files:
- `engine/northstarcpp/modules/api/include/northstarcpp/render_pick_api.h` (new)
- `engine/northstarcpp/modules/host/module_contracts.hpp`
- `engine/northstarcpp/modules/render/d3d11/d3d11.cpp`
- `engine/northstarcpp/modules/render/d3d11/renderer_private.hpp`
- `engine/northstarcpp/modules/render/d3d11/renderer_frame.cpp`
- `engine/northstarcpp/modules/render/d3d11/renderer_pick.cpp` (new)
- `engine/northstarcpp/modules/viewport/viewport.hpp`
- `engine/northstarcpp/modules/viewport/viewport.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_viewport.cpp`
- `tests/render_provider_device_probe.cpp`

## Workspace compatibility fixes necessary for the clean build

Two small fixes were necessary for changes already present concurrently in the physics/collision authoring tree:
- `collision_mesh.hpp`: explicitly include `physics_surface.hpp`, referenced by mesh baking.
- `collision_wireframe.hpp`: rename the lambda parameter named `emit` to `send_line`. Qt's `emit` macro erased this identifier during Qt Viewport compilation.

These do not modify serialized physics data or plugin ABI.

## Reproducible QA evidence

- A new isolated Ninja/MinGW GCC 16.2 Release build was configured at `_qa_p0_ecs` after the previous `out` QA tree was removed externally.
- Official V8 15.8.62 sidecar archive retrieved using Windows system TLS and verified against SHA-256:
  `9924fe95ae8a05b35f728da8258bd1d72a8810edc0179d65fd3018c7845489e3`.
  The pinned runtime version `V8 version 15.8.62` was verified. Cached only under `_qa_p0_ecs`; no certificate checks bypassed.
- Full `ninja -C _qa_p0_ecs northstarcpp_editor_qt -j 6` completed: **435/435 steps and successful editor linking**.
- `ninja -C _qa_p0_ecs northstarcpp_render_d3d11` completed successfully after the ID color aliasing fix.
- D3D11 hardware GPU probe on **NVIDIA Quadro RTX 4000** passed: nonsoftware adapter, geometry GPU readback, object-ID background and centered cube, and nearest cube in an overlapping pair.
- `ninja -C _qa_p0_ecs northstarcpp_scene_chunked_smoke` passed.
- Nine focused CTest cases passed after final changes:

| Case | Result |
|---|---|
| northstarcpp_device_probe_d3d11_hardware | PASS |
| northstarcpp_qt_editor_mouse_look | PASS |
| northstarcpp_qt_editor_native_ui | PASS |
| northstarcpp_qt_editor_authoring | PASS |
| northstarcpp_qt_editor_gpu_surface | PASS |
| northstarcpp_play_in_editor_render_qt | PASS |
| northstarcpp_play_in_editor_qt | PASS |
| northstarcpp_scene_component_dll_lifecycle | PASS |
| northstarcpp_scene_chunked | PASS |

No production binary was replaced. No Git commit/push was performed. This QA result does not assert complete UE 5.3.2 editor parity, full CTest regression, no access violations in prolonged use, or correctness of unimplemented Vulkan/D3D12 GPU ID paths.

## Remaining P0/P1/P2

1. P0: Vulkan/D3D12 per-object integer ID buffer (not CPU color inference), native mesh and character IDs, lossless depth-ordered selection, asynchronous pick staging and high-DPI viewport coordinate tests.
2. P0: ECS complex type metadata (enum, string, array, nested structs) and mixed-value multi-object UI, plus plugin load/unload concurrency and malicious plugin lifecycle boundaries.
3. P0: Extended AV `0xC0000005` reproduction, repeated resize, device lost, hot reload and driver-specific runs.
4. P1: World Partition cell authoring, HLOD, Data Layers/NavMesh, NEF8 ContentBrowser indexing, complete PIE Pause/Step/Eject isolation.
5. P2: CPU/GPU timeline correlation and p50/p95/p99 over 1000+ real frames in a 10,000+ actor scene.
