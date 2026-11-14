# Perlin Integration — Sky, Clouds, World, JSON Bridge

## Sky and cloud rendering
`Viewport::set_sky()` recognizes the exact material binding `textures/skydome.ytd@baseperlinnoise3channel`, preserving YDD geometry and YMAT tint while replacing the displayed noise RGB with a tileable procedural RGBA8 texture from `engine.perlin`. Three noise bands feed the existing D3D11 triplanar cloud shader. It generates on sky initialization, not per frame. All other base-color textures continue through normal YTD decoding.

## Procedural terrain
`make_perlin_world(PerlinWorldSettings)` builds an opt-in, deterministic, editable Scene V3 with height-derived solid terrain cubes, collision flags, stable entity IDs, one player spawn, and streaming cells. Use `perlin_world_generator.exe output.nscene 2026` from the installed tools directory. Existing scenes are not modified. This is a block-terrain prototype, not continuous tessellated terrain or infinite World Partition.

## YSC service
The YSC client now addresses `engine.perlin`: `perlin.sample_json_v1`, `perlin.fbm_json_v1`, and `perlin.batch_json_v1`. A C++ dispatcher `northstarcpp::call_perlin_service_json(service, method, request_json)` returns `{ "ok": true, "payload_json": "..." }` or `{ "ok": false, "error": "..." }` and invokes the installed ABI 1 provider. JSON input is validated; seeds use the JS safe integer range, fBm accepts 1–16 octaves, batches at most 4096 points.

**Remaining limitation:** The current `engine.scripting` host has no connected `NewViso.callServiceJson` callback and YSC imports are not fully executed as an engine-integrated script module. The native dispatcher and retargeted YSC source are implemented, but execution from a real weather/timecycle script remains unverified. Do not claim full YSC runtime integration until the script host callback is wired and tested.

## Acceptance and release
The focused suite covers ABI, deterministic cloud pixel generation, generated V3 scene round-trip, scene streaming, JSON sample/fBm/batch, exporter, and D3D11 WARP sky rendering. Five focused integration checks passed in the earlier build. A fully verified `dist` is pending: unrelated `engine.scripting` test sources are absent and current all-target build has failed under Ninja/Windows file-lock contention. Preserve existing releases; never publish a dist marked verified without a clean build and full CTest.

See [Perlin API](PERLIN_API.md).