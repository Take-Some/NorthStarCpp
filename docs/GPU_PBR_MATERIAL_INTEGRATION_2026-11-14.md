# NorthStarCpp 2026 — Real GPU Materials, UV and PBR Integration

Date: 2026-11-14
Status: engineering QA implementation integrated into existing first-class modules;
native D3D11/D3D12 GPU WARP and hardware acceptance PASS.
Full CTest has unrelated failures; production distribution not promoted.

## Problem

Previously the Qt Material Editor persisted an `northstarcpp.editor.material`
component and project `*.material.json`, but the Viewport flattened arbitrary
static/procedural mesh geometry into position+flat RGB triangles. UVs and
normals were lost before calling renderer plugins. Neither provider could
execute an object's authored Base Color/Normal/Roughness/Metallic/Emissive
textures in a real shader.

## First-class implementation

- `modules/api/include/northstarcpp/material.hpp`
- `modules/material/material.cpp`
- `modules/api/include/northstarcpp/render_materials_api.h`
- `modules/render/shared/material_hlsl.hpp`
- `modules/render/d3d11/d3d11.cpp`
- `modules/render/d3d12/d3d12.cpp`
- `modules/viewport/viewport.cpp`
- `modules/host/mesh_asset_assets.cpp`
- `modules/editor/qt_editor_window.cpp`

All code stays inside `engine/northstarcpp/modules`; no standalone mandatory
material DLL or new compatibility layer was introduced.

### Material resources

A single provider-neutral `MaterialDefinition` is loaded through the
current `AssetStore` project > shared VFS. Supported authored source
formats: `*.material.json` (schema 1), native YMAT libraries, and native
YMAT `@entry` references. Base Color hex is converted sRGB->linear.
Factor ranges and path/size bounds are validated.
Texture inputs: common raster formats decoded through Windows Imaging
Component and YTD native NETD block compression (BC1, BC3, BC4, BC5, BC7)
decoded through first-party codec integration into bounded RGBA8.
Native compiled libraries and original assets remain unchanged.

### Renderer ABI

The base `engine.render` ABI **remains 5**. Optional
`NORTHSTARCPP_RENDER_MATERIALS_ABI=1` / module ABI `0x524d5401`
accepts material batches, world-space vertices, normals, UVs, factor
constants and five RGBA8 texture slots. Providers copy caller data.
External DLL/SO implementations can independently replace the API
without relying on C++ layout or project disk access.

D3D11 and D3D12 now compile the SAME first-class HLSL PBR source.
The shader supports an approximate GGX/Smith/Fresnel microfacet
surface BRDF, world-space light, true camera position, and maps for:
Base Color (sRGB), Normal, Roughness, Metallic and Emissive (sRGB).
Unlit mode is also provided. Neither provider uses a Qt/QPainter
fake for authored GPU material rendering.

GPU implementation:
- D3D11: dynamic UV vertex buffer, dedicated shader constants,
  texture SRV cache, per-material PSO draw state.
- D3D12: root signature + PSO, 5-SRV descriptors per material,
  256-byte aligned material constants and row-aligned texture uploads,
  GPU fences and cached textures. Provider version 1.2.0.
- Both implement the same extension negotiation.
- Existing sky, HUD, character, terrain world surface and legacy triangle
  pipelines are retained.

### Scene and Editor

Viewport resolves the editor's material ECS component by **stable actor ID**.
The actor stores a material reference, NEVER a texture reference.
Material assignments supply real UV/normal vertex streams for:
- OBJ assets from any safe relative project Content Browser location.
- Existing `engine.geometry` procedural primitives (face-planar generated UVs).
- Default scene cube proxies (per-face 0..1 UVs).

The renderer receives one batch for each distinct material, with
bounds 128 batches / 300k vertices. Normal-colored primitives continue
through the old ABI when no material is authored.
The Qt Material Editor invalidates cached VFS/GPU material resources
on Save so the next GPU viewport frame uses edited factors and textures.
An external renderer lacking the materials extension can use the
existing D3D11 fallback; material data is not silently flattened away.

## Tests performed

New tests:
- `northstarcpp_material_resources`: project JSON, raster/WIC RGBA,
  real sharedAssets YMAT/YTD, path validation.
- `northstarcpp_d3d11_material_warp`: actual D3D11 WARP texture UV GPU readback.
- `northstarcpp_d3d12_material_warp`: actual D3D12 WARP texture UV GPU readback.
- `northstarcpp_d3d11_material_hardware`: native GPU texture UV readback.
- `northstarcpp_d3d12_material_hardware`: native GPU texture UV readback.

After the final real-camera shader changes, these tests plus the Editor
authoring, native Editor GPU surface, D3D12 WARP/hardware and older
D3D11 camera tests **all passed (10/10)**.

The D3D12 general WARP stress test also passed 1,000 frames. Its
CPU+Present timing was p50=16.666ms, p95=29.659ms, p99=51.777ms.
These are NOT GPU timestamp measurements or an optimized PBR workload.

Full `cmake --build ... --target all --parallel 2`: **PASS**.
Full CTest: **121/132 PASS; 11 FAIL** (not release-cleared).

Failures reported:
- Missing or invalid independent QA/scene/animation fixtures:
  qa_reporter, scene_obj_mesh, scene_animation,
  project_gameplay_scripts, exploration_bullet, exploration_jolt,
  scene_nef8_crosslang.
- Additional unrelated or insufficiently triaged failures:
  project_character_warp (source YMAT library error),
  exploration_player (same YMAT library error),
  shared_assets (JSON content parse error),
  logger_application_lifecycle (fallback log assertion).

No failing tests were suppressed and no unrelated project assets were
restored or overwritten. These failures prevent claiming a fully clean QA
release. The production `dist` package was not replaced.

## Still distinct from Unreal Engine 5 material parity

The native material implementation is a functional, bounded **first
GPU-PBR pipeline**, not UE5's Material Graph.
Outstanding capabilities include:
- Node-based shader graph / arbitrary HLSL graph compiler.
- Texture array/material instances, layers, material parameter collections.
- Physically calibrated IBL/probes, multiple dynamic lights/shadow BRDF.
- Clear Coat, Subsurface, Transmission, opacity/blend/alpha-test modes.
- Consistent tangent-space MikkTSpace normal maps, quality mip/LOD,
  anisotropic sampling, GPU residency/streaming/PSO precaching.
- Native UV/PBR for every YDD/YDR static drawable mesh (current static
  authored path is OBJ, procedural, cube; skinned YDD uses its existing
  character pipeline).
- Quantitative GPU performance profiling with timestamps, p50/p95/p99
  and long-running multiple-material stress tests.

A fully authored arbitrary multi-material PBR frame with YDD/FBX/GLTF and
advanced shader graphs has NOT been verified. Public release and production
promotion remain subject to clean QA and independent acceptance.
