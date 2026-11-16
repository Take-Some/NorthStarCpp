# Shader Hot Reload v2: GPU acceptance (2026-11-16)

Source: C:\\Users\\Kayla\\Documents\\NorthStar\\NorthStarCpp

## Implementation
- Render ABI 5 unchanged; Vulkan legacy Shader Reload ABI v1 still works.
- Optional ABI v2 (module 0x52534802), binary formats SPIR-V (Vulkan)
  and DXIL (D3D12), stages Vertex, Fragment Forward, Fragment GBuffer.
- Vulkan: async build of forward + deferred MRT material PSOs,
  fence-safe install, rollback and superseded revision discard.
- D3D12: async DXIL material PSO, stage-bound validation, old-PSO fallback,
  cached material variant fallback; isolated DLL built without replacing
  production. DXIL validator rejects SM5 DXBC and malformed data.
- Canonical XML YMAT Shaders fields: vertex, fragment, gbuffer,
  vertex_hlsl, fragment_hlsl, gbuffer_hlsl; project-relative path validation.
  Qt Editor and Viewport dispatch compiled bytes to the matching provider.
- Qt HLSL compile via dxc -T vs_6_0/ps_6_0. Name conventions:
  *.vs.hlsl / *.vertex.hlsl -> entry material_vs;
  *.gbuffer.hlsl -> entry material_gbuffer_ps; otherwise material_ps.

## Hardware results: NVIDIA Quadro RTX 4000

1. Vulkan v2 forward (Vertex + Fragment): PASS, GPU pixel readback.
2. Vulkan v2 deferred (Vertex + GBuffer): PASS, GPU pixel readback.
3. Vulkan Shader Reload v1 regression: PASS, two revisions + resize.
4. Vulkan 1000 ordinary frames: PASS, 1000 valid GPU timestamps:
   CPU p50/p95/p99=16.6438/17.6503/34.0926 ms;
   GPU p50/p95/p99=0.041248/0.0469152/0.212227 ms.
5. Vulkan repeated-hot-reload forward, 1000 frames, 1000 GPU samples,
   revision 16, no failed frames:
   CPU p50/p95/p99=16.6435/17.4337/18.0908 ms;
   GPU p50/p95/p99=0.044736/0.0502096/0.0526723 ms.
6. Vulkan repeated-hot-reload deferred, 1000 frames, 1000 GPU samples,
   revision 16, no failed frames:
   CPU p50/p95/p99=16.64/17.5365/18.2597 ms;
   GPU p50/p95/p99=0.196416/0.201602/0.205698 ms.
7. D3D12 isolated candidate --full: PASS, ABI v2 reported present,
   invalid DXIL safely rejected, existing Material Graph/geometry/texture
   and GPU picking regression passed.
8. D3D12 ordinary 1000 frames, 1000 GPU timestamps: PASS:
   CPU p50/p95/p99=1.59825/2.0233/2.35653 ms;
   GPU p50/p95/p99=0.549424/0.778886/2.14419 ms.
9. Qt text-editor smoke PASS (0 failures), native YMAT XML multi-stage
   link roundtrip and rollback smoke PASS.

QA binaries:
- _qa_content_browser_p2/render_vulkan.dll
- _qa_content_browser_p2/shader_hot_d3d12_v2.dll
- _qa_content_browser_p2/engine/northstarcpp/northstarcpp_render_shader_hot_reload_v2_smoke.exe

## Not yet accepted
- Real positive D3D12 DXIL PSO swap and its 1000-frame hot-reload stress:
  DXC executable and dxcompiler.dll are absent in this environment.
- Full production Qt Editor/Release build: render_d3d12.dll locked by
  existing process, link returned Windows Permission denied.
- Night City scene-specific p50/p95/p99, GPU validation-layer coverage,
  multi-worker PSO budget and memory growth profiling.
- No commit/push, production provider DLLs not replaced.
