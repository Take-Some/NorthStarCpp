# NorthStarCpp — эталонный renderer(7).zip: аудит и план паритета

Дата: 2026-11-17. Реализация: NorthStarCpp, единый build root `out/build`.

## Источник

Проверен предоставленный пользователем архив `renderer(7).zip`: 289 записей,
около 6,4 MiB распакованного кода. Это исходники старого RAGE-рендерера,
с собственными SDK, платформенными заголовками, draw lists и ресурсами.
Архив **не содержит готового совместимого модуля** NorthStarCpp; его
исходники не копировались и не присоединялись к проекту. Использованы
только архитектурные ориентиры, а новая реализация написана самостоятельно.

Основные подсистемы архива (точные пути внутри ZIP):
- `renderer/Deferred/GBuffer.cpp` + `DeferredConfig.h`: MRT, depth/stencil,
  deferred layout и декали
- `renderer/Deferred/DeferredLighting.cpp`: deferred shaders,
  направленные/локальные/объёмные источники света
- `renderer/Lights/TiledLighting.cpp`, `Lights/LightClassification.inl`:
  tile classification, depth reduction и light culling
- `renderer/RenderPhases/*`: std, deferred, FX, reflections, water,
  mirror reflection, cascades, paraboloid shadows, damage, debug
- `renderer/Shadows/ParaboloidShadows.cpp`: omnidirectional local shadows
- `renderer/PostProcessFX.cpp` и `PostFX_shared.h`: HDR, bloom, exposure,
  tonemapping, DOF/bokeh/CoC, blur, motion/lens effects
- `renderer/SSAO.cpp`: несколько SSAO quality/scale techniques
- `renderer/AdaptiveDOF.cpp`, `LensArtefacts.cpp`, `MLAA.cpp`
- `renderer/Mirrors.cpp`, `Water.cpp`, `River.cpp`, `newWater.cpp`
- `renderer/PlantsGrassRenderer.cpp`, `occlusion.cpp`,
  `Entities/InstancedEntityRenderer.cpp`,
  `Entities/VehicleDrawHandler.cpp`.

## ВАЖНО: четыре MRT не означают одинаковые каналы

Эталон, `Deferred/DeferredConfig.h`:
- RT0 RGB albedo, A SSA
- RT1 RGB normal, A twiddle
- RT2 R diffuse/specular mix, G spec exponent, B Fresnel, A shadow
- RT3 R natural ambient, G artificial ambient, B emissive,
  A skin/reflection intensity
- Depth + stencil independently.

NorthStar v2 имеет **другой PBR-контракт**, намеренно:
- RT0 RGBA8: linear albedo RGB / coverage A
- RT1 RGBA16F: world normal RGB / signed roughness A
- RT2 RGBA16F: linear emission RGB / metallic A
- RT3 RGBA8: material AO R (default 1), clearcoat G,
  sheen B, reserved A
- depth D32/R32 sampled view.

Не интерпретировать RAGE-каналы напрямую как NorthStar MRT.
Это нарушит GGX-материалы `.ymat` и подменит семантику шейдеров.
Переключатель расширения: `NORTHSTARCPP_GBUFFER_VERSION=2`.

## Матрица соответствия: факт реализации против задачи

| Эффект / подсистема эталона | NorthStarCpp сейчас | Что остаётся |
|---|---|---|
| Render phases / граф ресурсов | Typed Render Graph; D3D12 GPU passes; Vulkan graph executor | Общая система плагинных фаз/зависимостей, барьерная QA |
| G-buffer 4 MRT и depth | Опциональный G-buffer v2 в D3D12 и Vulkan | Профили memory bandwidth, MSAA, sample classification |
| Material PBR | GGX/UV/PBR, textures, XML YMAT и Material Graph | Clearcoat/Sheen authoring, parity of custom shaders |
| Point / spot / directional / skylight | Расширение динамических световых источников; tiled lists | Физические единицы/IES, clustered volumetric |
| Tiled lighting | D3D12/Vulkan light lists; fallback on overflow | GPU compute classification + depth pyramid |
| Sun CSM | D3D12 CSM; экспериментальная Vulkan CSM | Dedicated stabilization and cascade parity |
| PCF / PCSS | D3D12 CSM PCF/опциональный PCSS | Vulkan PCSS parity, light-radius model |
| Point / spot shadows | Типы света существуют | Cube/paraboloid shadows с cache/atlas |
| SSAO | D3D12 8-sample deferred AO; Vulkan half-res SSAO + depth/normal bilateral | Temporal + denoise + multi-scale GTAO-like |
| HDR scene | FP16 HDR, luminance/exposure/tonemap D3D12; Vulkan HDR opt-in | Pre-exposure, HDR gamut, EV compensation |
| Bloom | D3D12/Vulkan opt-in bloom | Multi-scale pyramids, anamorphic streaks, temporal stability |
| FXAA / MLAA | D3D12 FXAA; Vulkan отличается | Общий AA quality contract, SMAA/temporal |
| Film look | Exposure, contrast, saturation, grain, vignette D3D12/Vulkan | Chromatic aberration parity и LUT color grading |
| Camera motion vectors | D3D12/Vulkan experimental | Previous transform for rigid/skinned actors, all surfaces |
| TAA | Не заявлено production-ready | History ping-pong, reprojection, rejection/variance, transparency masks |
| Depth of field / Bokeh | Не реализовано полноценно | CoC/near/far split, gather/splat, alpha-safe composition |
| Motion blur | Не реализовано полноценно | Per-object velocity, tile/neighborhood max, shutters |
| Volumetric fog/rays | Нет полноценного volume scattering | Froxel density/light integration, reprojection, shadowing |
| Exponential height fog | **Добавлен** optional D3D12/Vulkan depth-based analytic effect | Artistic parameters, per-scene API, transparent objects |
| SSR / planar mirrors | Нет полноценной parity с эталоном | Hi-Z SSR + temporal denoise; planar mirror render phase |
| Translucency / glass | Material alpha forward, сортировка; depth-read теперь корректно описан | OIT для пересечений, refraction, water/glass |
| Water/rivers/reflections | Нет эталонного water pipeline | Surface simulation, normal refraction, foam, reflection |
| Vegetation/grass | Нет эталонного instanced vegetation pipeline | Wind, alpha-to-coverage, HLOD, shadows |
| Lens artefacts/distortion | Есть базовый Film Look, нет полного стека | Lens ghosts, streaks, distortion, dirt masks |
| Dynamic decals | Нет полного deferred decal pass | Decal MRT read/write split, sorting and masks |
| GPU occlusion | Есть WorldPartition/HLOD/CPU visibility | Hi-Z pyramid, occlusion queries, async feedback |
| Rigid GPU instancing | D3D12 indexed GPU meshes experimental | Vulkan, temporal velocity, translucent material fallback |
| Debug / capture / GPU timing | G-buffer diagnostic, timing, RenderDoc hooks/test probes | Named graph captures, quantitative pixel QA |

## Реальные изменения этого этапа

1. `shared/gbuffer_semantics.hpp`: единые имена диагностик и
   `material_ao`, `clearcoat`, `sheen` для MRT3.
2. `d3d12/renderer_device.cpp` и `vulkan/renderer_deferred.cpp`:
   распознавание дополнительных диагностик; D3D12 v1 rejects v2 debug.
3. `d3d12/renderer_pass_graph.hpp`: прозрачный проход декларирует
   DepthRead, не фиктивный DepthWrite.
4. D3D12 depth barrier исправлен по фактическому GPU QA:
   отдельный READ_ONLY_DEPTH DSV, корректная привязка для прозрачности,
   возврат depth resource в DepthWrite к завершению кадра.
5. `shared/height_fog.hpp`: строгий числовой валидатор opt-in
   `NORTHSTARCPP_HEIGHT_FOG_DENSITY` и численная функция проверки.
   Значения 0..0.25; пустое, NaN, Inf или недопустимое значение = disabled.
6. D3D12 `shared/deferred_hlsl.hpp` и
   `d3d12/renderer_pass_deferred.cpp`: analytic height extinction.
   Отдельный float4 CBV слот без нового RT.
7. Vulkan `deferred_resolve.frag` и `renderer_deferred.cpp`:
   тот же extinction, density передаётся в ранее свободном канале
   `push.sky.w` (128 B push constants сохраняются).
8. Все 26 Vulkan deferred shader variants скомпилированы из изменённого
   GLSL через `tools/generate_vulkan_deferred_spirv.py` в
   `deferred_spirv.hpp`. D3D12 HLSL компилируется провайдером.
9. Добавлен `tests/render_reference_pipeline_smoke.cpp` и CTest
   `northstarcpp_render_reference_pipeline`.

## Верификация

- `cmake --build out/build --target northstarcpp_render_d3d12 northstarcpp_render_vulkan`: OK
- D3D12/WARP при `NORTHSTARCPP_GBUFFER_VERSION=2` и
  `NORTHSTARCPP_HEIGHT_FOG_DENSITY=0.018`: **7/7** целевых тестов OK
  (material, PostFX+shadow, rigid indexed, graph, Vulkan sync/executor).
- Vulkan hardware: `ssao_gbuffer_2` и
  `hdr_gbuffer_2_ssao_1_csm_1`: **2/2** OK.
- Cross-backend hardware G-buffer parity: **1/1** OK.
- Это подтверждает сборку и целевые GPU пути, а не полный пиксельный
  parity с ZIP и не прирост FPS непосредственно в KaylaNightCity.

## Приоритеты следующего этапа

P0: согласованный postfx-compositor/Render Graph для D3D12 и Vulkan;
внедрение cinematic DOF через CoC/near/far, velocity для всех rigid и
skinned meshes, TAA resolve с disocclusion rejection. Не менять рендер
разрешение и исходные ассеты.

P1: depth pyramid + GPU tile/cluster light classification,
temporal GTAO/SSAO, local point/spot shadow atlas,
multi-level bloom, volumetric lighting + fog.

P2: planar mirrors, SSR, refraction and transparent OIT, deferred decals,
lens artefacts, vegetation/water material phases.

Тестирование P0/P1: одна и та же сцена KaylaNightCity, камера, время,
weather, идентичные текстуры и разрешение; измерить p50/p95/p99
CPU/GPU, 1% low, VRAM, bytes uploaded/frame и pixel compare по MRT.

## Ограничения и договорённости

- Height fog — **не** полноценное volumetric light scattering и пока
  не обрабатывает все прозрачные/Unlit материалы одинаково.
- В отсутствие `NORTHSTARCPP_HEIGHT_FOG_DENSITY` изображение и качество
  остаются как прежде.
- GPU QA smoke ≠ снятый benchmark KaylaNightCity. Benchmark ещё не выполнен.
- Production packages, `dist`, commit и push не трогались.
