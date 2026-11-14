# NorthStarCpp — ECS API v2

**Status (2026-11-13):** editor object authoring migrated to a provider-neutral ECS API; GNU GCC/MinGW-w64 build and 10/10 CTest checks passed.

## Architecture

- `include/northstarcpp/ecs_api.h` — versioned pure-C ABI (`NORTHSTARCPP_ECS_API_VERSION = 2`) using opaque world handles and 64-bit `NsEntity` IDs.
- `include/northstarcpp/ecs.hpp` — provider-neutral `EcsWorld` interface for editor, game runtime and tools.
- `engine/northstarcpp/modules/host/ecs_world.cpp` — ECS world host and runtime-to-API bindings.
- `engine/northstarcpp/modules/ecs/flecs/flecs.cpp` — default Flecs 4.2.0 implementation, behind `engine.ecs` module capability.
- `tests/ecs_mock_provider.cpp` — independent, non-Flecs implementation of the same API for compatibility testing.

## Editor migration

The editor's authoritative objects now reside in `EcsWorld`, not in an editable `Scene::objects` array. Create, delete, selection, inspection and component updates use stable entity handles through the ECS API. Existing scene `.nscene` files are loaded into the ECS world. A temporary `Scene` snapshot is used only for the current serializer and renderer. Play-mode Bullet physics creates an independent simulation world and synchronizes transforms back through the ECS interface; stopping Play does not overwrite authored ECS data.

Core schemas: `northstar.name`, `northstar.position`, `northstar.visual`, `northstar.body`, `northstar.controller`, `northstar.motion`. API operations include registering POD component types, creating/deleting entities, checking liveness, getting/setting/removing components, filtering entity queries and advancing systems.

## Replaceability

Provider DLL is selected by `northstarcpp.modules` (`engine.ecs`) or the `NORTHSTARCPP_ECS_PLUGIN` override. The host loads a `NorthStarModule` with a versioned `NsEcsApi` table and validates the ABI size. No Flecs type crosses the DLL boundary. See [module configuration](MODULES.md).

To exercise both independent implementations:

```powershell
.\northstarcpp.cmd configure
.\northstarcpp.cmd build
.\northstarcpp.cmd test
```

The tests `northstarcpp_flecs_ecs`, `northstarcpp_ecs_provider_independent`, `northstarcpp_editor_ecs_provider_independent`, and `northstarcpp_editor_player` cover ECS operations and real native editor/player smoke runs. The capability swap test also exercises the shared module loader.

## Boundaries

The current `.nscene` file format and graphical inspector handle the built-in scene properties. User-defined third-party component blobs are available through the ECS API but are **not yet persisted by `.nscene`** or edited in the built-in inspector. Live hot-swap while a world is running and a native ECS-query render path also remain distinct future work. For current built-in editor objects, ECS is authoritative.
