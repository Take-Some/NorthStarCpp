# NorthStarCpp engine SDK class sources

The **authoritative source tree** is engine/northstarcpp/resources/sharedAssets/api.

Each subsystem has its own directory, and the protected V8 entrypoint is built
from exactly the 23 class source units listed in api/manifest.json.
The list is ordered by lexical initialization dependencies, never by
filename prefix or directory traversal order.

- core: EngineAPI, Factory, Script, Contracts, NorthStarSDK
- audio: universal Audio playback only (WAV/NAC)
- camera: Camera, CameraViewController
- character: CharacterService, PlayerService and character-authored behavior
- scene: Scene, Entity, Prefab
- physics: Physics and Collision
- weapon: WeaponController, WeaponBallistics, WeaponEffects
- vehicle: Vehicle and VehicleHandle
- input, events, ui, session, player, particles, logging: domain services

The generated northstar/bootstrap.js is a single V8 lexical unit, not an
additional authored implementation. The native resource embedder excludes
manifest-selected class source files by exact path so they are not loaded
a second time. Other standalone default gameplay root scripts remain
separate resources and keep their own lifecycle.

Run:

    python tools/dev/compose_northstar_bootstrap.py --root engine/northstarcpp/resources/sharedAssets/api --verify
    node tests/engine_api_modularity_smoke.cjs
    node tests/engine_api_oop_smoke.cjs
    node tests/engine_api_layout_smoke.cjs

Public imports remain @northstar/engine. API is the immutable EngineAPI
singleton, Factory is the one canonical creation service, and the
EngineModuleRegistry is a class-owned immutable catalog. The native
providers remain authoritative for ECS, physics, audio, cameras and rendering.
