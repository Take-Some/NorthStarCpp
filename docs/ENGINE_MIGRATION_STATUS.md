# NorthStarCpp — functional engine migration ledger

**Updated:** 2026-11-14  
**Author:** Kayla Verner

## Meaning of the states

- **Source relocated:** original contents and provenance preserved. This does **not** imply corresponding functionality is linked or usable.
- **Compiles:** GCC/MinGW-w64 builds that CMake target.
- **Integrated:** exported API is exercised by editor, player or a sample game.
- **Verified:** behavioral contract demonstrated in repeatable tests.
- **Release-ready:** verified with a clean distribution and SDK consumer, dependency audit and redistribution permissions.

Historical sources were imported from `external/northstarcpp/compat/` into
`engine/northstarcpp/modules/foundation/northstarcpp/compat/`.

The import manifest `.northstarcpp/migrations/compat-foundation-v1.json` records
3,037 original files and their checksums. Running
`python tools/migrate_compat_to_engine.py --verify` compares the current files
with that baseline and reports files changed during subsequent development.

## Active capabilities

| Area | Current implementation | Status / limits |
| --- | --- | --- |
| Foundation | `northstarcpp_math`, geometry primitives and spatial queries | Compiled historical math and primitive tables, procedural meshes and ray/AABB queries |
| ECS | engine.ecs / Flecs API v2 | Basic scene bridge integrated |
| Physics | engine.physics / Bullet and Jolt | Rigid bodies; not full historical collision pipeline |
| Rendering | engine.render / D3D11 | Viewport uses the original cube geometry and frustum tests to cull cubes outside the camera |
| Sound | engine.audio / WinMM PCM16 | Basic playback, not full mixing/streaming |
| Input | engine.input / Win32 and dynamic XInput | Keyboard, mouse, pads; no complete input action system |
| File/assets | `AssetManager` / `engine.assets` | Directory and uncompressed PC FAT/DB mounts share the virtual namespace. The player loads archive-backed scenes. Shared NEF8 assets stream through the background VFS queue with priority, residency and camera zones. PPMd, incremental NEF8 decoding and offline cooking remain open. |
| Scenes | .nscene V3, stable IDs, layers/cells/portals, Flecs scene bridge and stream diagnostics | Generic prefabs, component reflection and full production scene workflow remain open |
| Editor/player | Native app prototypes | Functional scene editing and playback; incomplete production pipeline |
| Scene animation | Active scene-transform clips, AssetManager load, C++ playback, JavaScript engine.animation API | Native clip sampling and Scene V3 sample pass; full skinned rigs and rendered Player test remain open |
| Scripting | engine.scripting ABI 2 with native V8 provider smoke and project JavaScript callbacks | Full Player/V8 integration and packaged-game gameplay run are not yet release-verified |
| Shader generator | Selected historical shaders tool | Utility only, not full shader/material system |

## Geometry SDK

Link `NorthStarCpp::northstarcpp_math` and include `<northstarcpp/geometry.hpp>` and
`<northstarcpp/collision_queries.hpp>`. Static solid and wireframe primitive views
reference the original tables. The SDK also builds transformed solid meshes,
rings and tori. The ray/AABB query returns the nearest nonnegative ray parameter
within the supplied limit, including zero when the origin is inside the box.

## Game example: FPS Arena

Prototype project at Projects/NorthStarFPSArena contains a script-owned target-range implementation.
Its weapon/targets/health/mission rules live in scripts/main.js and exercise registered engine APIs.
A JavaScript contract smoke passes under Node; native Player/V8 rendering remains a separate acceptance gate.

## Historic source groups still requiring implementation

| Original group | Files relocated | Needed work |
| --- | ---: | --- |
| editor | 781 | Port native editing interface, authoring and import workflows |
| render | 471 | Geometry, materials, textures, render graph |
| core | 297 | Memory, job system, scheduler, configuration |
| animation | 186 | A separate native scene-transform clip runtime is now available; skeletons, skinning, blending and state machines from the historical group are not integrated |
| physics | 111 | Shapes, constraints, original integrations |
| collision | 85 | Ray/AABB integrated into FPS hit tests. Spatial trees, mesh queries and collision resources remain |
| input | 32 | Legacy-specific devices, action mapping |
| network | 48 | Transport, sessions and replication |
| sound | 38 | Mixing, streaming, spatial effects |
| game | 57 | Gameplay logic and game lifecycle |
| ui | 39 | Widgets, layout, focus and navigation |
| engine | 31 | Runtime bootstrap and subsystems |
| shader_compiler | 25 | Shaders cooking and validation |
| Others | Remaining files | Asset cooking, Maya, tools and tests |

These groups are **source-relocated**, not automatically integrated or verified.

## Acceptance conditions for each capability

1. Stable ABI/SDK surface; provider-neutral when replaceable.
2. Source compiled in active engine CMake target.
3. Tested with GCC Debug and Release.
4. Real gameplay/editor/asset use, not only test-only symbols.
5. Lifecycle and failure tests, compatibility checks.
6. SDK + module registry + installed package validation.
7. Dependency and license verification for redistributable games.

## Redistribution

Code movement or successful builds do not establish rights to redistribute
third-party engine code. Retain original notices and review required permissions
before distributing derived binaries publicly.
