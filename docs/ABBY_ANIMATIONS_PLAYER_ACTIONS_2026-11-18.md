# Abby: YCD animation inventory, physics events and play-mode integration

Date: 2026-11-18

## Source inspection

Exact files in engine shared assets:
- `characters/abby/animations/mm-explore.ycd` (~8.8 MB). Binary scan yielded
  **155 unique Abby-prefixed strings**, of which **153 are clip-like names**
  and two are internal `__native_...` resource aliases. These are string
  discoveries, not proof that every record is a fully playable animation.
- `characters/abby/animations/fall-medium.ycd`: contains the string
  `abby-fall-medium`.
- `characters/abby/animations/traversal.ycd`: compact binary NEF8;
  `abby-ground-jump-run` is configured in playerConfig.js but cannot be
  independently verified through raw-text inspection alone.
- Rig `models/abby.ymt` and model `models/abby.ydd` are present.

All animation asset paths remain VFS-relative. No resource conversion,
destructive rewrite, or incompatible YCD format change was performed.

## Clip categories (not disjoint)

- **Приземления**: 7 named entries matching category keywords
- **Повороты**: 10 named entries matching category keywords
- **Приседание**: 2 named entries matching category keywords
- **Ходьба**: 47 named entries matching category keywords
- **Бег**: 69 named entries matching category keywords
- **Спринт**: 8 named entries matching category keywords
- **Стрейфы**: 48 named entries matching category keywords
- **Ступени и склоны**: 50 named entries matching category keywords

## Actually wired to AbbyPistolDemo

- Idle, forward walking, running, sprinting: existing exploration clips
- Rising jump: `traversal.ycd@abby-ground-jump-run` (project-configured)
- Falling: `fall-medium.ycd@abby-fall-medium`
- Soft, medium, hard landing: `mm-explore.ycd@abby-mm-soft-landing`,
  `@abby-mm-med-landing`, `@abby-mm-hard-landing`
- Walk/run backward and left/right strafe: clips verified in `mm-explore.ycd`
- Crouching idle listed as an optional animation, but **not automatically
  activated** without a native character crouch/motor action.

CharacterVisual continues to animate the one authored SkeletalMesh entity,
preserving the Pawn, first-person presentation, bone attachment and weapon.

## Native player action events

Physics steps produce transitions from real Pawn positions and grounded
contact, not keyboard presses:

| Signal | Condition |
|---|---|
| `player.jump` | leaves ground with positive vertical speed |
| `player.apex` | ascending becomes falling |
| `player.fall` | enters falling, including walking off a ledge |
| `player.land` | air-to-ground collision transition |
| `player.move.start`, `player.move.stop` | horizontal velocity hysteresis |
| `player.state.changed` | idle/moving/jumping/falling transition |
| `player.action` | common catch-all for these events |

Payload fields: `pawn` (string ID), `state`, `previous`,
`action`, `horizontalSpeed`, `verticalSpeed`, `fallDistance`,
`impactSpeed`, `grounded`, `moving`.

- `Player.motionState()` is a read-only physics-frame snapshot.
- `Player.on("land", callback)` and `Player.once("jump", callback)`
  delegate to the root Events bus.
- `Events.scope()` lets gameplay script handlers be disposed at script stop.
- Grounded detection uses native Player animation physical sample or a
  fallback sphere sweep for Pawns without an authored animation graph.
- Teleports and Pawn changes reset motion history, avoiding synthetic impact
  events. One-shot landing animations have timed replay windows.

### Example

```javascript
import { Player } from "@northstar/engine";

const unsubscribe = Player.on("land", e => {
  if (e.impactSpeed >= 7) {
    // Attach a camera shake, particles or an NAC landing effect here.
  }
});

// On script stop:
unsubscribe();
```

## QA

- Native Player and D3D12/Vulkan providers compiled and linked successfully.
- `northstarcpp_player_action_events`: passed jump / apex / fall /
  land / impact / movement / teleport / session-reset unit tests.
- `node tests/player_action_script_api_smoke.cjs`: passed API bindings,
  once/on and subscription cleanup.
- `node tests/abby_pistol_demo_smoke.cjs`: passed, including simulated jump,
  fall and all three landing clips with original pistol attachments intact.
- TypeScript `tsc --noEmit`, bootstrap verify: passed.
- `northstarcpp_player.exe --runtime-init-smoke`: passed D3D12 and Vulkan,
  startup report READY, 0 failures.
- Broader Player Blueprint CTest run: **6 of 7 passed**. Existing
  `northstarcpp_player_blueprint_physics` fails at
  `Valid Root Motion rejected tick=1`; this is not covered by the new
  event state machine and is **not claimed resolved**.
- Full live gameplay with human-controlled jump and motion capture has
  not yet been visually accepted. Do not claim tested blend or perfect
  landing synchronization in a full frame.

### Late build / end-to-end QA caveat

A full Player link initially succeeded, and the ordinary Player executable
passed `--runtime-init-smoke` on both D3D12 and Vulkan. After the final
Play-session tracker reset, `window.cpp.obj` recompiled successfully. A
subsequent normal full link was blocked by `render_d3d12.dll: Permission denied`
(the output DLL was open by another process). An isolated Player executable
linked successfully but failed in V8 script initialization with
`std::bad_alloc` on both providers. The earlier ordinary Player binary
continued to pass startup smoke tests. This inconsistency requires a clean,
unlocked Player rebuild and fresh Play-mode verification before production
acceptance. The long-running `abby_animation_locomotion_acceptance.py`
acceptance test also timed out at 40 seconds.

The new unit and JS simulation tests are green, but **do not establish final
end-to-end gameplay acceptance**.

No production dist overwrite, commit or push.

## Appendix: discovered Abby names in mm-explore.ycd

- `abby-explore-idle-add`
- `abby-explore-idle-sub`
- `abby-explore-idle-with-leg-holster-add`
- `abby-explore-move-pronate-add`
- `abby-explore-move-sub`
- `abby-explore-move-supinate-add`
- `abby-explore-quick-turn-180-l`
- `abby-explore-quick-turn-180-r`
- `abby-mm-crouch-idle`
- `abby-mm-explore-crouch-idle`
- `abby-mm-explore-idle`
- `abby-mm-explore-idle-mirror-switch`
- `abby-mm-explore-idle-turn-135l`
- `abby-mm-explore-idle-turn-135r`
- `abby-mm-explore-idle-turn-180l`
- `abby-mm-explore-idle-turn-180r`
- `abby-mm-explore-idle-turn-45l`
- `abby-mm-explore-idle-turn-45r`
- `abby-mm-explore-idle-turn-90l`
- `abby-mm-explore-idle-turn-90r`
- `abby-mm-explore-run-loop-bkwd-135l`
- `abby-mm-explore-run-loop-bkwd-135r`
- `abby-mm-explore-run-loop-bkwd-180`
- `abby-mm-explore-run-loop-fw`
- `abby-mm-explore-run-loop-fw-45l`
- `abby-mm-explore-run-loop-fw-45r`
- `abby-mm-explore-run-loop-fw-90l`
- `abby-mm-explore-run-loop-fw-90r`
- `abby-mm-explore-run-slope-loop-135l-dn`
- `abby-mm-explore-run-slope-loop-135l-up`
- `abby-mm-explore-run-slope-loop-135r--dn`
- `abby-mm-explore-run-slope-loop-135r--up`
- `abby-mm-explore-run-slope-loop-45l-dn`
- `abby-mm-explore-run-slope-loop-45l-up`
- `abby-mm-explore-run-slope-loop-45r-dn`
- `abby-mm-explore-run-slope-loop-45r-up`
- `abby-mm-explore-run-slope-loop-90l`
- `abby-mm-explore-run-slope-loop-90l-dn`
- `abby-mm-explore-run-slope-loop-90l-up`
- `abby-mm-explore-run-slope-loop-90r`
- `abby-mm-explore-run-slope-loop-90r-dn`
- `abby-mm-explore-run-slope-loop-90r-up`
- `abby-mm-explore-run-slope-loop-bw-dn`
- `abby-mm-explore-run-slope-loop-bw-up`
- `abby-mm-explore-run-slope-loop-fw-dn`
- `abby-mm-explore-run-slope-loop-fw-up`
- `abby-mm-explore-run-stair-90l-dn`
- `abby-mm-explore-run-stair-90l-up`
- `abby-mm-explore-run-stair-90r-dn`
- `abby-mm-explore-run-stair-90r-up`
- `abby-mm-explore-run-stair-fw-dn`
- `abby-mm-explore-run-stair-fw-up`
- `abby-mm-explore-run-strafe-135l`
- `abby-mm-explore-run-strafe-135r`
- `abby-mm-explore-run-strafe-45l`
- `abby-mm-explore-run-strafe-45r`
- `abby-mm-explore-run-strafe-90l`
- `abby-mm-explore-run-strafe-90r`
- `abby-mm-explore-run-strafe-bw`
- `abby-mm-explore-run-strafe-diamond-l-foot-a`
- `abby-mm-explore-run-strafe-diamond-l-foot-b`
- `abby-mm-explore-run-strafe-diamond-l-foot-c`
- `abby-mm-explore-run-strafe-diamond-l-foot-d`
- `abby-mm-explore-run-strafe-diamond-r-foot-a`
- `abby-mm-explore-run-strafe-diamond-r-foot-b`
- `abby-mm-explore-run-strafe-diamond-r-foot-c`
- `abby-mm-explore-run-strafe-diamond-r-foot-d`
- `abby-mm-explore-run-strafe-diamond-reverse-l-foot-a`
- `abby-mm-explore-run-strafe-diamond-reverse-l-foot-b`
- `abby-mm-explore-run-strafe-diamond-reverse-l-foot-c`
- `abby-mm-explore-run-strafe-diamond-reverse-l-foot-d`
- `abby-mm-explore-run-strafe-diamond-reverse-r-foot-a`
- `abby-mm-explore-run-strafe-diamond-reverse-r-foot-b`
- `abby-mm-explore-run-strafe-diamond-reverse-r-foot-c`
- `abby-mm-explore-run-strafe-diamond-reverse-r-foot-d`
- `abby-mm-explore-run-strafe-fw`
- `abby-mm-explore-run-strafe-quickplants-45l-l-foot`
- `abby-mm-explore-run-strafe-quickplants-45l-r-foot`
- `abby-mm-explore-run-strafe-quickplants-45r-l-foot`
- `abby-mm-explore-run-strafe-quickplants-45r-r-foot`
- `abby-mm-explore-run-strafe-quickplants-90-l-foot`
- `abby-mm-explore-run-strafe-quickplants-90-r-foot`
- `abby-mm-explore-run-strafe-quickplants-fw-l-foot`
- `abby-mm-explore-run-strafe-quickplants-fw-r-foot`
- `abby-mm-explore-sprint-loop-135-l`
- `abby-mm-explore-sprint-loop-135-r`
- `abby-mm-explore-sprint-loop-45-l`
- `abby-mm-explore-sprint-loop-45-r`
- `abby-mm-explore-sprint-loop-90-l`
- `abby-mm-explore-sprint-loop-90-r`
- `abby-mm-explore-sprint-loop-bw`
- `abby-mm-explore-sprint-loop-fw`
- `abby-mm-explore-strafe-run-switch-cc-l-foot`
- `abby-mm-explore-strafe-run-switch-cc-r-foot`
- `abby-mm-explore-strafe-run-switch-cw-l-foot`
- `abby-mm-explore-strafe-run-switch-cw-r-foot`
- `abby-mm-explore-strafe-walk-switch-cc-l-foot`
- `abby-mm-explore-strafe-walk-switch-cc-r-foot`
- `abby-mm-explore-strafe-walk-switch-cw-l-foot`
- `abby-mm-explore-strafe-walk-switch-cw-r-foot`
- `abby-mm-explore-walk-loop-bkwd-135l`
- `abby-mm-explore-walk-loop-bkwd-135r`
- `abby-mm-explore-walk-loop-bkwd-180`
- `abby-mm-explore-walk-loop-fw`
- `abby-mm-explore-walk-loop-fw-45l`
- `abby-mm-explore-walk-loop-fw-45r`
- `abby-mm-explore-walk-loop-fw-90l`
- `abby-mm-explore-walk-loop-fw-90r`
- `abby-mm-explore-walk-repositions`
- `abby-mm-explore-walk-slope-loop-135l-dn`
- `abby-mm-explore-walk-slope-loop-135l-up`
- `abby-mm-explore-walk-slope-loop-135r-dn`
- `abby-mm-explore-walk-slope-loop-135r-up`
- `abby-mm-explore-walk-slope-loop-45l-dn`
- `abby-mm-explore-walk-slope-loop-45l-up`
- `abby-mm-explore-walk-slope-loop-45r-dn`
- `abby-mm-explore-walk-slope-loop-45r-up`
- `abby-mm-explore-walk-slope-loop-90l`
- `abby-mm-explore-walk-slope-loop-90l-dn`
- `abby-mm-explore-walk-slope-loop-90l-up`
- `abby-mm-explore-walk-slope-loop-90r`
- `abby-mm-explore-walk-slope-loop-90r-dn`
- `abby-mm-explore-walk-slope-loop-90r-up`
- `abby-mm-explore-walk-slope-loop-dw-dn`
- `abby-mm-explore-walk-slope-loop-dw-up`
- `abby-mm-explore-walk-slope-loop-fw-dn`
- `abby-mm-explore-walk-slope-loop-fw-up`
- `abby-mm-explore-walk-stairs-90l-dn`
- `abby-mm-explore-walk-stairs-90l-up`
- `abby-mm-explore-walk-stairs-90r-dn`
- `abby-mm-explore-walk-stairs-90r-up`
- `abby-mm-explore-walk-stairs-bw-dn`
- `abby-mm-explore-walk-stairs-bw-up`
- `abby-mm-explore-walk-stairs-fw-dn`
- `abby-mm-explore-walk-stairs-fw-up`
- `abby-mm-explore-walk-strafe-135l`
- `abby-mm-explore-walk-strafe-135r`
- `abby-mm-explore-walk-strafe-45l`
- `abby-mm-explore-walk-strafe-45r`
- `abby-mm-explore-walk-strafe-90l`
- `abby-mm-explore-walk-strafe-90r`
- `abby-mm-explore-walk-strafe-bw`
- `abby-mm-explore-walk-strafe-fw`
- `abby-mm-hard-landing`
- `abby-mm-hard-landing-mirrored`
- `abby-mm-hard-landing-run`
- `abby-mm-med-landing`
- `abby-mm-med-landing-mirrored`
- `abby-mm-soft-landing`
- `abby-mm-soft-landing-mirrored`
- `abby-mm-tense-idle`
- `abby-mm-tense-mirrored-idle`
- `abby-player-mm-explore.pak`
