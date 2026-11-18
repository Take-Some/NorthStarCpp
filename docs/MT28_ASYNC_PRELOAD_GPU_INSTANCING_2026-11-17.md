# MT28 — asynchronous full-detail preload and GPU instancing roadmap
Date: 2026-11-17

## Implemented
- `viewport_model_preloader.hpp`: single-worker bounded queue, no render-thread wait for native .ydr/.ydd and YMAT/YTD decode. Full original asset bytes preserved. Duplicates deduped. Completed bundles move into standard Viewport caches; errors quarantine invalid assets.
- Viewport project remount stops worker before retiring the old AssetStore; paths are validated. Other projects without a preload manifest remain supported.
- `Projects/KaylaNightCity/assets/preload/models.xml` contains 19 MT28 model paths and is queued when the project VFS mounts, possibly while the menu is displayed. Other native models decode asynchronously on demand.
- Long worker tasks print `native_async_preload=<asset> ms=<duration> status=<ready|failed>`.

## Real asset evidence
- `models/mahir_mt28/v_mahir_mt28_coach__ext01_body_01.ydr`: 690,565 mesh vertices, 2,704,716 indices (~901,572 triangles), 33 material definitions.
- Async decode with dependencies: 1,399.6 ms measured in one actual project-asset test; this is background latency, not GPU draw time or FPS.
- 19 manifest entries checked for existence, 4,932,829 bytes combined YDR source size (excludes material/texture dependencies).

## Verification
- `tests/viewport_native_preload_smoke.cpp`: no blocking, dedup, exception propagation, path safety, worker shutdown.
- `tests/mt28_native_preload_integration_smoke.cpp`: actual MT28 original model and VFS decode.
- Full focused regression: 14/14 tests PASS; `northstarcpp_player.exe` built in `out/build`.

## Still required: actual GPU instancing
The Materials v1/v2 ABI accepts expanded world-space triangle streams. The Viewport transforms >2.7M indices for the bus body when it moves; D3D12 uploads changed material vertices and Vulkan currently CPU-projects/clips material triangles. A texture cache or decoder worker alone does not eliminate this per-frame geometry work.
Next ABI needs (1) immutable GPU-resident indexed local-space meshes keyed by asset version/submesh, (2) current/previous world transforms per stable instance, (3) D3D12 default-heap and Vulkan device-local indexed buffers with fenced async uploads and shader-side transform, (4) shadow/GBuf/HDR/pick/temporal parity, (5) full-resolution fallback for external providers.
Do not substitute 1-bone GPU skinning: D3D12 lacks the skinned material extension and Vulkan's compute skin path expands vertices per frame.

## Acceptance limits
CPU parsing is now asynchronous. Uploading decoded material images/vertices to a GPU still occurs at first render use; smooth GPU arrival is not yet established. Real KaylaNightCity FPS before/after, p95/p99, GPU timings, upload bytes, and identical-scenario 1% lows remain unmeasured. No render scale, geometry quality, or texture resolution were changed. No commit/push.