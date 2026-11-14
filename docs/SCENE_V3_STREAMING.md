# NorthStarCpp — Scene V3, Spatial Streaming and Editor Integration

**Engine:** NorthStarCpp (current C++ source tree, `engine/northstarcpp`)  
**Integration tool:** NorthStar — Suite V4.2  
**Reference:** the 210-entry `scene(3).zip` source archive, used for subsystem analysis only.

## Scope and architecture

The source archive contains separate implementations of scene entities, streamed map
loading, scene scoring, LOD management, interiors and portals, volumes, world
queries and debugging. NorthStarCpp implements equivalent *categories* through
its own C++ scene API, **without copying the archive's platform-dependent source
or binding gameplay/editor objects to Flecs implementation classes**.

The document model (`sdk/include/northstarcpp/scene.hpp`) is provider-neutral.
`EcsWorld` exposes the stable ECS abstraction through `NsEcsApi`; scene I/O,
the authoring graph, physics providers, asset providers, and rendering remain
separate responsibilities.

### Implemented

| Capability | Implementation |
|---|---|
| Legacy document support | `scene.cpp`: `.nscene` v1/v2 parser and writer preserved |
| V3 format | `scene_v3.cpp`: stable IDs, hierarchy, layers, cells, triggers, portals, authored LOD, asset paths, strict validation, deterministic serialization |
| ECS scene metadata | `ecs_world.cpp`: lossless scene metadata across provider-neutral ECS load/create/update/snapshot |
| Spatial scene queries | `scene_world.cpp`: cell-based AABB/sphere search; incremental reindexing and streaming-cell migration of moving entities |
| Scene streaming | distance-based load/unload hysteresis, camera-velocity lookahead, always-loaded/pinned cells |
| Streaming work budgets | deterministic priority scores and independently bounded cell loads/unloads per frame |
| Streaming diagnostics | last load/unload transitions, pending work, cell/entity counts and update generation |
| Interior and layer visibility | open-portal reachability, room/layer masks, per-entity LOD near/far tests |
| Editor integration | `window.cpp`: World → Streaming Diagnostics; v3 inspector; hierarchy, layers, cells, portals, volumes; streaming status |
| Renderer integration | viewport.cpp: source-index-preserving visibility, two-tier HLOD proxies and 3D streaming cell edges |
| Asset requests | Version-3 streaming priorities and bounded asynchronous AssetManager drawable requests |
| Physics collision residency | Optional ABI 1 extension for Bullet and Jolt; static broadphase suspension/restoration without changing physics ABI 4 |
| GPU transient residency | D3D11 dynamically sizes the vertex buffer and releases unused storage after 120 frames; skydome GPU resources can be explicitly cleared |
| GPU diagnostic ABI | Optional render-memory ABI 1 for vertex capacity and sky geometry/texture allocations, without changing render ABI 5 |
| Editor spatial overlays | World menu 3D cell bounds, live HLOD diagnostics and editable streaming budgets |

### Streaming document contracts

`NorthStarCpp.Scene 3` accepts an optional record:

```text
stream_budget <max_cell_loads_per_update> <max_cell_unloads_per_update>
```

Both values must be in `[1, 8192]`. The defaults are `64 / 64` when a
previously authored V3 scene contains no `stream_budget` record. All V1/V2
scene contracts remain unchanged.

The two cell budgets only bound *normal* distance-driven requests. Cells
marked always-loaded, or pinned through `SceneWorld::pin_cell`, are brought
online immediately without consuming the ordinary load budget.

SceneWorld manages render visibility, asset priorities and static collision
participation. PhysicsScene::sync_scene_residency uses an OPTIONAL provider
extension; old physics providers keep their collision data resident.
Bullet/Jolt remove eligible far static bodies from collision broadphases
and restore them without changing scene indices or ECS handles.
The native shape allocation is preserved for fast restoration.
Dynamic actors, players, external triangle meshes and persistent geometry
are protected. A 32-meter minimum radius keeps nearby collision active.

The D3D11 provider now releases excess transient GPU vertex capacity after
120 low-demand frames. This does not yet evict arbitrary YDD/ymat/YTD GPU
resources per streaming cell. Sky geometry and texture GPU resources
can be explicitly released via the existing render API.

Static scene geometry has two spatial HLOD bands with 64m and 256m
groups. At distance, multiple cubes become a conservative AABB proxy.
No ECS or physics bodies are deleted. Actual optimized mesh/texture
baking, material-aware HLOD and HLOD asset caching remain future tasks.

### Live object transforms

`SceneWorld::update(position, velocity, room, &live_scene)` receives a live
scene snapshot. Dynamic positions are validated, reindexed in the spatial
grid and used for LOD decisions. The ordering and stable IDs of the live scene
must match those of the authored scene. The original hierarchy and metadata
are never silently overwritten by physics updates.

When a live entity crosses into another valid streaming cell, the runtime
updates its effective cell ownership without editing the serialized SceneNode
or changing the ECS entity handle. If a moving entity leaves all cell bounds,
it remains logically resident until re-entry instead of disappearing when its
old cell unloads.

The editor's fifth Streaming Budget inspector tab edits per-frame cell
load/unload limits and streaming distances through validated C++ transactions.

Ordinary editor selection and inspector refreshes do not rebuild `SceneWorld`;
scene mutations, undo/redo, new/open and add-object actions explicitly rebuild
it. This prevents unnecessary loss of streaming queues on UI-only changes.

## Verification

- `northstarcpp_scene_world_v3` — passed after stream priority, live
  relocation, older-V3 compatibility, portal, LOD, hysteresis and validation tests
- `northstarcpp_flecs_ecs` — passed
- `northstarcpp_ecs_provider_independent` — passed with the mock ECS provider
- `tests/scene_smoke.cpp` — legacy scene test independently linked and passed

- Bullet collision residency CTest: passed (native provider broadphase)
- Jolt collision residency CTest: passed (native provider broadphase)
- PhysicsScene and SceneWorld C++ integration: passed with Bullet and Jolt
- D3D11 WARP GPU memory residency: passed (grow, shrink and skydome release)
- HLOD tests: level 1 and 2 selection, near-detail, hidden layers, stable original IDs passed

Build/test examples:

```powershell
cmake --build out/scene-v3-cmake-check --target northstarcpp_scene_world_smoke
ctest --test-dir out/scene-v3-cmake-check -R "northstarcpp_scene_world_v3" --output-on-failure
ctest --test-dir out/scene-v3-cmake-check -R "northstarcpp_(flecs_ecs|ecs_provider_independent)" --output-on-failure
```

## Not yet at archive-level feature parity

- Sector-based GPU resource residency for textured YDD meshes and YTD
  textures requires a sector-aware GPU asset manager.
- Static collision bodies are removed from their broadphase but shapes stay
  allocated. Large streamed terrain-mesh physics requires independent
  unloading/recreation contracts.
- HLOD proxies are AABB approximations; actual mesh simplification,
  material preservation, texture baking and automatic LOD asset caching
  are not yet implemented.
- Portals currently implement connectivity/reachability, not polygonal
  frustum clipping and GPU occlusion.
- Per-cell NavMesh and agent simulation, advanced world render graph and
  end-to-end profile telemetry remain pending.
- The changed source was unit/integration tested, but a complete new
  editor+player Release build and its in-game acceptance are NOT confirmed.
  The temporary build output directory was removed during ongoing work;
  release binaries must not be replaced without rebuilding and retesting.

Acceptance remains: clean Release C++ rebuild, selected ECS/physics provider
tests, true editor/player scene launch, sector fast travel and road collisions,
stable GPU memory under long camera movement, frame-time p50/p95/p99 over
>=1000 valid frames, and an atomic distributable replacement with rollback.


## Integration verification update — 2026-11-14

This implementation remains entirely in current NorthStarCpp C++ through
NorthStar — Suite V4.2. It does **not** switch to the legacy engine.

### Native runtime changes

- Bullet and Jolt export optional Physics Residency ABI 1, independently of
  Physics ABI 4. Native static boxes leave/re-enter broadphase; player,
  dynamic rigid bodies, terrain ground and persistent collision are protected.
- The host calls PhysicsScene::sync_scene_residency after SceneWorld updates.
  No Flecs-private symbols are required; old physics providers simply keep
  all collision resident.
- SceneWorld generates two-tier (64m / 256m) static HLOD groups with bounded
  AABB proxies; 3D cell outlines can be toggled in the editor's World menu.
- D3D11 maintains transient GPU vertex memory proportionally to visible scene
  complexity; it shrinks after 120 low-demand frames, and allocates replacement
  buffers before releasing the old COM object. Render ABI 5 is unchanged.
- Render Memory ABI 1 is an optional provider symbol for memory statistics.
- An authored regression fixture now exists at
  tests/fixtures/scene_v3_hlod_streaming.nscene.

### Tests already executed successfully

| Test | Environment | Result |
|---|---|---|
| Bullet residency (native collision sweep) | Debug | PASS |
| Jolt residency (native collision sweep) | Debug | PASS |
| Host PhysicsScene + SceneWorld streaming on Bullet | Debug | PASS |
| Host PhysicsScene + SceneWorld streaming on Jolt | Debug | PASS |
| D3D11 GPU buffer growth/shrink and skydome memory release | Debug, WARP | PASS |
| Scene V3 HLOD/streaming/serializer regression | Debug and fresh Release | PASS |
| D3D11 GPU memory residency regression | Fresh Release, WARP | PASS |
| Dedicated Scene V3 HLOD fixture parse | Fresh Release library | PASS: 14 entities, 2 cells, 1 portal |
| Flecs ECS and provider-independent ECS | Debug | PASS |
| Legacy scene parser test | Debug | PASS |

### Release delivery gate

The fresh Release CMake configuration completed and the standalone runtime,
D3D11 and Scene V3 tests built. A full Release Editor/Player rebuild was
started in the isolated .northstar_scene_validation directory; its recorded
log reached task 474/522, but **neither application EXE was produced and no
successful build exit was recorded**. This is NOT a release-ready build.
Do not overwrite previously installed engine applications or providers.

Full image/game visual parity with the reference archive is NOT claimed.
Missing features include real HLOD mesh/material baking, streaming of GPU
YDD/YTD resources by cell, triangle-mesh collision sector reconstruction,
portal-frustum occlusion, NavMesh sector residency, and 1000-frame
p50/p95/p99 in-game profiling.
