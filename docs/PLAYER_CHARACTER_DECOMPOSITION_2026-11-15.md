# NorthStar Player / Character decomposition — initial authoring slice (2026-11-15)

## Reference model (UE 5.3.2)

Reviewed the release branch of
https://github.com/Mccrae123/Unreal-Engine-5.3.2 :
- `Engine/Source/Runtime/Engine/Classes/GameFramework/Character.h`:
  `ACharacter : APawn`, `USkeletalMeshComponent`,
  `UCapsuleComponent`, `UCharacterMovementComponent`.
- `Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerController.h`:
  `APlayerController : AController` and separate camera manager.
- `Engine/Source/Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h`:
  authored movement parameters (walk speed, jump velocity, etc.).

Architectural ideas were independently implemented. **No UE source, shaders,
binary assets or engine headers were copied**; UE's Epic EULA is not a source
license for third-party GPLv3 game engines.

## Previous NorthStar behavior / root cause

- SceneObject contained `bool player`, `size`, `mass` and `node.asset`;
  `make_startup_scene` created a "Player" box.
- Runtime physics/controller paths still recognize the legacy `player` flag.
- `modules/viewport/viewport.cpp` had an explicit early
  `continue` when `object.player` had no model asset in the GPU Editor.
  The actor therefore existed in Outliner but **was not submitted for drawing**.

## Implemented

1. `modules/api/include/northstarcpp/player_character_components.hpp`:
   - Stable versioned V3 SceneComponentBlob identifiers:
     `northstar.gameplay.pawn`,
     `northstar.gameplay.character_motor`,
     `northstar.gameplay.camera_rig`.
   - Bounded, little-endian binary scalar layouts and validators.
   - Reflection field metadata exposed to the **existing** ECS component
     property editor: walk/run speed, jump, acceleration, air control, gravity,
     FOV, camera height/pitch, auto possession and input-enabled.
   - Idempotent `add_character_components` on a stable V3 entity ID.
   - Existing `northstar.physics.collider` version 3, shape Capsule,
     `ObjectType::Pawn`, with no second physics backend or serializer.
2. `modules/viewport/editor_character_marker.hpp`:
   - Editor-only capsule geometry and heading wedge on existing
     `NorthStarRenderTrianglesApi`, no external mesh required.
   - Bounded per-actor triangle count; zero additional on-disk assets.
   - Character component lookup indexed once per GPU draw.
3. `modules/viewport/viewport.cpp`:
   - Replaced the silent skip for an unbound Player with the capsule marker.
   - If triangle capability/budget is missing, fall back to the existing cube
     proxy rather than hiding the object.
   - The Player's existing model path is still used when set.
4. `modules/editor/qt_editor_viewport.cpp`:
   - Proxy mode shows Pawn/Character as a capsule marker, not a cube.
5. `modules/editor/qt_editor_window_setup.cpp`:
   - `Edit > Add Character (Pawn)` creates a V3 entity with
     Character + Motor + Capsule; auto-possess only when the scene has no
     legacy controlled Player.
   - `Edit > Configure Character Components` and a Details button upgrade
     an existing selected Player/Actor without duplicating components.
   - All edits pass through existing `editScene` Undo/Redo & validation.
6. `modules/editor/qt_editor_window.cpp`: schemas registered in Editor
   before Details UI initialization.

## Verified

- Full Debug build of `northstarcpp_editor_qt` and dependent
  `northstarcpp_player`, Jolt and D3D11 renderer: **PASS**.
- `northstarcpp_character_components_smoke`: **PASS**.
- Qt editor `--smoke` after final build: **PASS**, now including a real
  `Add Character (Pawn)` action, component lookup, capsule lookup, Undo and Redo.
- Isolated `qt_editor_viewport.cpp`, `qt_editor_window.cpp`,
  `qt_editor_window_setup.cpp`, `viewport.cpp`: **compile PASS**.
- Standalone `--gpu-smoke`: final execution result not verified; on-screen
  visual acceptance remains pending.
- Native embedded Player WARP smoke earlier in this integration:
  **PASS**, including pause/resume/eject/possess and return to authoring.
  The very last WARP re-run after adding the Add Character action was
  not confirmed due to tool failure.
- `git diff --check`: **PASS** for the edited tracked files.

Build artifacts are in:
`C:\Users\Kayla\Documents\NorthStar\cache\qt-pie-icons-build\`
(no production deployment, no Git commit or push).

## Deliberately NOT complete

- CharacterMotor numbers are **authoring metadata only**; the runtime
  PhysicsScene player motion is still driven by legacy `SceneObject::player`.
- `Auto Possess Local` has not been wired to a persistent,
  engine-level `PlayerController -> Pawn` runtime ownership service.
- CameraRig properties are saved/editable, but the Player's active
  first-person camera is not yet bound to this component.
- Rendering a capsule is an editor diagnostic, **not a skeletal avatar**.
  Assign a YDD/YDR/OBJ and the appropriate skeleton/animation assets
  for a visible authored character model.
- Input mapping, runtime anim graph, network prediction, AI possession,
  camera blending, pawn unpossession and PlayerStart/GameMode spawn rules
  remain separate follow-up tasks.

## Next implementation milestone

1. Engine-owned Controller/Pawn interface with stable Entity IDs:
   `possess(entityId)`, `unpossess()`, `controlledEntity()`, error states.
   Controller must outlive pawn/vehicle transitions.
2. CharacterMotor plugin capability consumes authored component values,
   performs shape casts, stair/slope/ground contact through Jolt; no
   second overlapping movement loop.
3. Active CameraRig resolves pose from possessed pawn and supports
   first-person, third-person and cinematic camera mode transitions.
4. Migration/compatibility test: serialize V1/V2 legacy player, upgrade
   to V3 components, play, save/reload, ensure deterministic spawn.
5. GPU authoring and runtime visual parity: native mesh/skeleton,
   collider visualization, animation, material and pick/selection IDs.
6. In-game Jolt + controller smoke over 1000+ frames, stable contacts,
   p50/p95/p99 and replayable input sequence.
