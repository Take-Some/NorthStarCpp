# NorthStarCpp — reference renderer migration (2026-11-16)

## Reference inspection

User-supplied renderer(7).zip contains 289 entries and 6,737,324
uncompressed bytes. It is a platform-dependent renderer source tree,
not a drop-in Vulkan/D3D12 backend. The audit inspected:
- renderer/Deferred/{DeferredConfig.h,GBuffer.cpp,DeferredLighting.cpp}
- renderer/{PostProcessFX.cpp,PostProcessFXHelper.cpp,SSAO.cpp,
  AdaptiveDOF.cpp,LensArtefacts.cpp,Mirrors.cpp,Water.cpp,occlusion.cpp}
- renderer/Lights/TiledLighting.cpp
- renderer/RenderPhases/{RenderPhaseCascadeShadows.cpp,
  RenderPhaseWaterReflection.cpp}

No reference source code, proprietary shader binaries or platform SDK wrappers
were copied into NorthStar. The implementation follows current NorthStar
original code and the engine's existing GPU/material contracts.

### Important: GBuffer incompatibility

Reference DeferredConfig.h defines:
- RT0: RGB albedo; A SSA
- RT1: RGB normal; A twiddle
- RT2: diffuse/spec mix, spec exponent, Fresnel, shadow
- RT3: natural ambient, artificial ambient, emissive, skin/reflection
- depth/stencil/FMASK depending on platform.

NorthStar currently uses THREE color MRT plus separate depth:
- 0 RGBA8_UNORM: linear albedo RGB and coverage A
- 1 RGBA16F: world normal RGB and signed roughness A (negative = unlit)
- 2 RGBA16F: linear emissive RGB and metallic A
- independent depth (D32/R32 sampled)
These layouts are not interchangeable. The reference's fourth MRT
has NOT been added. The migration cannot claim four-MRT parity.

The new shared/gbuffer_semantics.hpp fixes NorthStar's current channel
interpretation and defines consistent debug indices (1=albedo,
2=normal, 3=roughness, 4=metallic, 5=emissive, 6=depth, 7=AO, 8=coverage).
D3D12 and Vulkan deferred resolves now support those common debug names;
the legacy Vulkan-specific env setting remains a fallback.

## Implemented in this pass

### Optional Filmic PostFX provider ABI v1
File: modules/api/include/northstarcpp/render_postfx_api.h.
Module ABI 0x50584601, table ABI 1. Render ABI 5 unchanged.
Per-renderer set_settings/get_settings, bounded 32-byte POD.
Settings: EV, contrast, saturation, vignette, chromatic aberration pixels,
film grain. Validate NaN/inf, ranges, struct size and reserved fields;
invalid updates preserve the old working look. Neutral settings are default.

Shared helper: render/shared/postfx_look.hpp.

### D3D12
- Film look in the existing HDR/bloom/FXAA compositor, after 3D and
  before the HUD. Independent PostFx ABI; existing providers unaffected.
- Exposure EV integrated in HDR before ACES, without duplicate LDR exposure.
- Saturation, contrast, vignette, grain and chromatic aberration through
  scene-color samples; FXAA can be disabled while grading remains enabled.
- Works in default FXAA mode, in HDR+Bloom mode and with FXAA disabled.
- Optional NORTHSTARCPP_ENABLE_POSTFX_LOOK=1 at creation enables compositor
  even if FXAA, HDR and Bloom are all disabled.
- Fixed a real null-PSO crash in the composite pass for the case above.
- GBuffer debug adds Coverage. Existing D3D12 SSAO, HDR, CSM, PCSS,
  bloom, exposure, FXAA remain in place; this work does not claim to
  replace their implementations or add a full depth-of-field path.

Implementation:
render/d3d12/{renderer_film_look.cpp,renderer_pass_postfx.cpp,
renderer_device.cpp,renderer_private.hpp,d3d12.cpp}
render/shared/{postfx_hlsl.hpp,postfx_hdr_hlsl.hpp,deferred_hlsl.hpp}

### Vulkan
- Deferred 8-tap depth-derived world-space SSAO (optional), bounded radius,
  angle bias and ambient-only modulation, not a separate half-res HBAO
  denoise/temporal pass.
- Enable with NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1
  plus NORTHSTARCPP_ENABLE_VULKAN_SSAO=1.
- Shader push constants extended to 128 bytes; glslc regenerated
  deferred_spirv.hpp; all Vulkan TUs rebuilt.
- Vulkan Film Look supports EV, contrast, saturation, vignette and grain
  in deferred resolve before engine HUD overlay. The current implementation
  **rejects nonzero chromatic aberration**: full-frame scene-color sampling
  requires an extra PostFX pass. Transparent forward materials composed
  later than deferred resolve are not necessarily graded yet.
- Debug modes harmonized with D3D12 and preserved backwards-compatible
  NORTHSTARCPP_VK_GBUFFER_DEBUG variable.
- Shader Reload v1 revalidated after the changed Renderer layout.
Implementation:
render/vulkan/{renderer_film_look.cpp,renderer_deferred.cpp,
renderer_device.cpp,renderer_private.hpp,deferred_resolve.frag,
deferred_spirv.hpp,vulkan.cpp}

### Host and editor integration
- Existing optional provider discovery now has a PostFX validation overload
  in host/module_contracts.hpp.
- Viewport supports_postfx(), set_postfx_settings() and postfx_settings()
  query the active provider. No implicit backend substitution.
- No dedicated Qt Film Look settings panel or JavaScript engine.ui/API
  binding yet. The renderer API is currently accessible to C++ callers.

### Tests
- New fast test tests/render_effect_contract_smoke.cpp checks neutral settings,
  finite/valid settings, invalid inputs and GBuffer channel IDs.
- Extended tests/render_provider_device_probe.cpp: postfx_api,
  postfx_gpu, postfx_invalid_rejected flags and --postfx stress mode.
- The GPU probe verifies the pixel readback while a non-neutral film
  look is enabled, then restores the neutral look by default.
- All newly introduced C++ test/build targets added to CMakeLists.txt.

## Verification (NVIDIA Quadro RTX 4000, Windows)

| GPU scenario | Frames | GPU timestamps | CPU p50 / p95 / p99 ms | GPU p50 / p95 / p99 ms |
|---|---:|---:|---|---|
| D3D12 Filmic PostFX (QA scene) | 1000 | 1000 | 1.5785 / 1.99316 / 2.56456 | 0.602736 / 0.624634 / 0.76027 |
| Vulkan Deferred + SSAO before Film integration | 1000 | 1000 | 16.6556 / 18.3967 / 44.4124 | 0.119584 / 0.126563 / 0.257414 |
| Vulkan Deferred + SSAO + Filmic | 1000 | 1000 | 16.6247 / 17.7846 / 33.764 | 0.100944 / 0.10512 / 0.273124 |

Additional PASS:
- D3D12 --full, including Film PostFX ABI, invalid-value rollback,
  real GPU readback, GPU picking, graphs and textures.
- D3D12 HDR + Bloom + Film PostFX --full.
- D3D12 FXAA=off + Film PostFX with explicit compositor enabled --full
  (regression fixed from initial Access Violation).
- Vulkan --full GPU provider regression: geometry, materials, lighting,
  skinning, surfaces, picking and timestamps passed.
- Vulkan Shader Reload v1: 2 revisions, pixel readback, resize recovery.
- Vulkan Deferred with SSAO and Film PostFX --postfx --stress=1000.
- Native Viewport static library rebuilt successfully after optional
  PostFX dispatch functions were added.
- Render effects contract smoke: PASS.
- git diff --check: PASS on changed tracked sources; line-ending warnings
  from Git's LF/CRLF conversion only.

These are small QA scenes, NOT full Night City GPU profiles. Do not
attribute frame-time differences to an optimization without an equal
scene, fixed camera, same resolution/settings and controlled present mode.

### QA artifacts

_qa_content_browser_p2/render_vulkan.dll
_qa_content_browser_p2/renderer_ref_postfx_qa.dll
_qa_content_browser_p2/northstarcpp_gpu_provider_probe.exe
_qa_content_browser_p2/engine/northstarcpp/northstarcpp_render_effect_contract_smoke.exe

D3D12 normal output render_d3d12.dll is locked by a running process.
All D3D12 translation units compiled, but normal link was denied.
The isolated QA candidate linked to a different output name.
No production executable/DLL was replaced. No Git commit/push.

NOTE AT FINAL RECHECK: the previously used _qa_content_browser_p2 directory
was no longer present on disk after the successful test runs. Its QA binaries
therefore cannot be supplied as currently available files; use a fresh
isolated CMake build directory for the next deployment or test run.
The source edits, CMake targets and this report remained present in the
NorthStarCpp workspace.

## Next technical milestones: not yet implemented from reference

P0 — Explicit GBuffer v2 and full cross-provider post stack:
- Optional four-MRT (ambient/reflection/SSS) *versioned* material ABI,
  shader outputs and reader conversion, formats and bandwidth budget,
  RT/DS state, Vulkan dynamic rendering, descriptor sets, D3D12 PSOs,
  debug inspector, resource aliases, and independent GPU regression.
- Vulkan scene-color offscreen target: complete forward/translucent pass
  before Filmic/AA, independent grade/composite pass and HDR/ACES parity.
  Implement chromatic aberration and full-scene grain without HUD blur.
- RenderGraph resource lifetimes/barriers and GPU timestamp spans per
  effect on both Vulkan and D3D12.

P1 — Temporal and AO:
- Velocity buffer (skinned/static + camera motion), previous per-object
  transforms, disocclusion mask, TAA/TAAU, history invalidation.
- GTAO/HBAO or optimized SSAO in half resolution, depth/normal bilateral
  denoise, temporal reuse and debug targets.
- Motion blur, CoC DOF, lens bokeh and SSR with HZB plus environment probes.
- Shared Film Look asset/preset in XML, Editor visual panel and script bridge.

P1 — Performance and QA:
- GPU PSO precache/pipeline cache, pass merge where safe, aliasing;
  bandwidth/memory budgets and GPU/CPU timeline.
- Controlled Night City 1000+ frame p50/p95/p99 per effect plus VRAM
  and validation-layer checks at consistent resolution and camera state.
- Vulkan 1000-frame stress with transparent geometry and current Filmic pass
  once post stack unifies; D3D12 real DXIL positive hot-reload acceptance
  awaits availability of dxc/dxcompiler.

P2 — World effects:
- Reflection probes and planar/water reflection cache, volumetric lighting,
  lens flares and anamorphic streaks, advanced shadow caching, fog and
  GPU-driven HZB culling.
- Reference depth of field, occlusion, lens optics are architectural
  inspirations; no representation of full source-code port is made.
