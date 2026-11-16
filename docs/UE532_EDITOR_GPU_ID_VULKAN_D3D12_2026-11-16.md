# NorthStarCpp Editor — GPU object-ID picking, Vulkan and D3D12

Date: 2026-11-16
Workspace: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`
QA build: `_qa_editor_mixed`

## Scope and acceptance

This iteration implements **real GPU color-ID picking for cube primitives** in both Vulkan and Direct3D 12, matching the existing optional D3D11 extension. The stable `engine.render` ABI 5 and existing provider APIs are unchanged. Picking is exposed through the optional `NorthStarRenderPickApi` extension (`NORTHSTARCPP_RENDER_PICK_MODULE_ABI`).

**Not implemented in this iteration:** native per-actor IDs for YDD/YDR material sections, untextured triangle streams or skinned animation batches; asynchronous GPU staging/fence polling; persistent offscreen picker targets; measured high-DPI hardware acceptance; full editor/world authoring parity. The engine uses geometry picking fallback when unsupported mesh/material/skin geometry is present; it must not select cube objects hidden behind an untagged native mesh.

## Implemented Vulkan path

- Added `engine/northstarcpp/modules/render/vulkan/renderer_pick.cpp`.
- Per-pick dedicated `VK_FORMAT_R8G8B8A8_UNORM` color attachment, independent depth image, exact unlit 24-bit IDs (zero reserved for no hit), 1x1 `vkCmdCopyImageToBuffer` GPU pixel transfer and host-visible readback.
- New pick graphics pipeline uses a **linear RGBA8 UNORM** render format irrespective of swapchain BGRA/sRGB selection. Color-space conversions and per-face shading cannot alter IDs.
- `renderer_frame.cpp` records the vertex subranges belonging to the submitted cube draw indices, including visibility clipping; `renderer_private.hpp` holds the ranges/pipeline.
- Vulkan module adds optional `NorthStarRenderPickApi` module query without modifying primary ABI.
- Explicitly refuse cube-only picking when unrelated mesh/material/skinned streams remain active, to avoid depth-inaccurate object selection.

## Implemented D3D12 path

- Added `engine/northstarcpp/modules/render/d3d12/renderer_pick.cpp`.
- Per-pick RGBA8 UNORM color + D32 depth committed attachments, normal depth-tested D3D12 graphics pipeline, exact unlit ID channels, 1x1 GPU->CPU transfer with 256-byte aligned texture footprint.
- Visible cube source/draw-index mapping is retained in `renderer_frame.cpp` and `renderer_private.hpp`. Source meshes/material batches remain excluded until they have explicit per-draw object IDs.
- Module query adds the same optional `NorthStarRenderPickApi` extension, leaving `engine.render` ABI 5 unchanged.

## Viewport and GPU tests

- Qt Editor native viewport now converts Qt **logical mouse coordinates** to **Win32 HWND client pixel coordinates** using the actual surface dimensions, avoiding a 1:1 assumption at fractional DPI.
- `tests/render_provider_device_probe.cpp` extends the real-hardware ID test (centered object, empty background, closer of two overlapping cubes) to both new providers. The harness clears previously submitted mesh/material/skin smoke data before testing the intentionally cube-only attachment.
- The test uses the NVIDIA Quadro RTX 4000 hardware adapter (`software_adapter=false`); both Vulkan and D3D12 report `gpu_pick_supported=true` and `gpu_pick_verified=true`.
- Both existing **1,000-frame GPU stress tests** passed. These test the existing frame/renderer path and GPU timestamps; they do not measure 1,000 consecutive ID readback operations.
- The first combined Qt regression reported `component Undo failed` and a dependent GPU surface failure. These were unrelated to provider ID results: editor scene now already contains ECS components, but the smoke asserted the whole collection should be empty after Undo. The smoke now snapshots and compares the complete pre-edit component collection and uses the appended last record for targeted payload assertions. Authoring and GPU surface both subsequently passed.

## Final verified CTest matrix

| Test | Result |
|---|---|
| northstarcpp_device_probe_d3d11_hardware | PASS |
| northstarcpp_device_probe_d3d12_stress | PASS |
| northstarcpp_device_probe_d3d12_hardware | PASS |
| northstarcpp_device_probe_vulkan_stress | PASS |
| northstarcpp_device_probe_vulkan_hardware | PASS |
| northstarcpp_qt_editor_authoring | PASS |
| northstarcpp_qt_editor_gpu_surface | PASS |
| northstarcpp_play_in_editor_render_qt | PASS |
| northstarcpp_scene_component_dll_lifecycle | PASS |

**Final result: 9/9 PASS, zero failures (42.00 s)**. `git diff --check` exit code **0** (existing working-tree CRLF conversion warnings only).

QA binaries were rebuilt and linked successfully, including:
- `_qa_editor_mixed/render_vulkan.dll`
- `_qa_editor_mixed/render_d3d12.dll`
- `_qa_editor_mixed/northstarcpp_editor_qt.exe`
- `_qa_editor_mixed/northstarcpp_player.exe`

## Follow-up — remaining P0 work

1. Add **versioned per-draw actor/entity IDs** across colored triangle streams, UV/PBR material batches (which currently combine multiple actors sharing a material), and GPU skinned YDD/YDR sections. These IDs must survive grouping, clipping and streaming without misidentifying a submesh.
2. Render unified geometry/material/skin IDs into the **same depth-correct attachment** with skinning/opacity masking semantics; test overlapping native and skinned meshes, including occlusion against cube proxies and material-translucency rules.
3. Move from per-click transient allocation and a **blocking GPU fence** to persistent resize-aware attachments, ringed readback staging, asynchronous fence polling and frame-generation tags. Prevent stale results after resize/device loss/scene changes.
4. Verify high-DPI Qt pointer mapping at 100/125/150/200% scale on hardware; run resize/hot reload/device lost and long-duration `0xC0000005` regression tests.
5. Continue ECS Details complex nested reflection and batch edits; then World Partition, HLOD and NavMesh authoring.

## Change management

Source changes are local to the connected user's workspace. Production editor and installed provider DLLs **were not replaced**. No Git commit or push. Other existing working-tree edits were preserved.
