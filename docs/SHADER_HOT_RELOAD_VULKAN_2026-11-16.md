# NorthStarCpp — Vulkan Shader Hot Reload (ABI v1)
Date: 2026-11-16
Workspace: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp

## Status

Hardware-validated Vulkan forward material FRAGMENT PSO hot swap on Quadro RTX 4000. Shader module creation and pipeline compilation run on a worker; old PSO stays active until the replacement builds. Render ABI v5 is unchanged. No GPU idle wait on a normal hot reload. No production DLL was replaced.

## Native XML YMAT material association

Canonical YMAT stays NEF8 kind 4 with an XML material-library payload:

    <Material name="WetAsphalt" shader="standard_lit">
      <Surface blend="translucent" two_sided="False" alpha_cutoff="0" />
      <Shaders fragment="assets/shaders/wet_asphalt.frag" />
      <Textures />
      <Params />
    </Material>

Each named material may hold an optional project-relative fragment source. The Qt Material Editor exposes Fragment GLSL (.frag). Material entry parsing, serialization, VFS loading and UI all preserve this reference. Absolute paths, parent traversal, backslashes, control bytes and malformed extensions are rejected. Sibling entries are preserved.

## Optional provider extension

Header: engine/northstarcpp/modules/api/include/northstarcpp/render_shader_reload_api.h

- NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI: 0x52534801
- NORTHSTARCPP_RENDER_SHADER_RELOAD_ABI: 1
- set_material_references: associates current material V1/V2 GPU batches with exact VFS material IDs.
- queue_fragment_spirv: copies <=1MiB SPIR-V fragment for a bound material, queues one asynchronous pipeline compile, caps active cached keys to 32.
- poll: nonblocking QUEUED/BUILDING/READY/FAILED, requested and installed revisions, diagnostics.

No filesystem I/O or GLSL compilation inside the provider. The editor compiles via glslc into temporary SPIR-V, emits shaderCompiled(path,bytes), the host matches shader path to cached MaterialDefinition and submits to the selected renderer. Unsupported or unavailable extensions return an explicit error; they never silently switch providers.

## GPU execution and rollback

The Vulkan renderer records a normal frame and waits for the previously submitted graphics completion fence. pump_shader_reload polls any completed worker in a nonblocking way, performs the PSO swap only when old GPU submissions cannot reference the retired PSO, and keeps the previous shader when a compilation fails. New source revisions supersede older worker results. Resize/shutdown explicitly joins any outstanding worker before destroying layouts/device. After resize, retained shader bytecode is rebuilt against new render state.

Supported: Vulkan forward material fragment shaders. Deferred opaque GBuffer, arbitrary vertex shaders and Direct3D/HLSL providers are not implemented. Some Vulkan built-in programs still originate from embedded SPIR-V; they are not changed by the material hot reload extension.

## Hardware regression evidence

Build root: _qa_content_browser_p2
Isolated candidate provider: shader_hot_vulkan.dll
Acceptance executable: engine/northstarcpp/northstarcpp_render_shader_hot_reload_smoke.exe
Generated fixture: generated/shader_hot_green.spv
Source fixture: tests/fixtures/shader_hot_reload_green.frag

On NVIDIA Quadro RTX 4000:
- GPU provider probe default: PASS, including hardware initialization, draw, readback and timestamps.
- GPU provider probe --full: PASS, including material textures, Material Graph, dynamic lights, GPU skinning, world surfaces and GPU picking.
- Shader hot reload GPU smoke: PASS. Rejects broken SPIR-V and unknown material, verifies initial PSO install, actual green GPU pixel output, a second PSO revision and successful GPU output following swapchain resize without frame submission errors.
- Qt text asset smoke: PASS (0 failures), including glslc valid/invalid compile cases.
- Native YMAT edit smoke: PASS, including fragment link persisted to XML, read-back and safe rejection of a path escaping the project.
- All modified Vulkan translation units recompiled consistently and the isolated provider DLL linked.
- git diff --check: PASS (line-ending notices only).

An earlier candidate DLL crashed because some Vulkan object files used the old Renderer memory layout; rebuilding ALL Vulkan objects fixed it. Do not mix old and new Vulkan objects after editing renderer_private.hpp.

## Remaining restrictions

Main production Qt Editor linking was blocked by Windows Permission denied on render_d3d12.dll, held by an active process. Production executable/DLL not replaced. Full Release/end-to-end UI acceptance remains incomplete. No commit/push was performed and existing unrelated changes were not reverted.

Further requirements: full Shader Dependency Graph/Asset Registry integration for material remap, hot reload for opaque deferred MRT & vertex shaders, D3D12 DXIL/HLSL implementation, Vulkan pipeline cache/parallel PSO budgets and p50/p95/p99 profiling over >=1000 frames. GPU pass on the small fixture is not a claim of full Night City runtime performance acceptance.
