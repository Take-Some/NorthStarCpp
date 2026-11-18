# Abby / NorthStarCpp: eliminate bind/T-pose flashes

## Root causes

1. `SkeletonPose::sample` initializes every omitted YCD track from YMT bind
   transforms; Abby's actual clips often animate only **33–72** joints.
2. Native viewport fallbacks rendered original, unskinned YDD when an incoming
   YCD was unavailable, malformed or the Animation Graph was still loading.
3. Some project animation references did not match exact YCD dictionary entry
   names (for example `abby-mm-soft-landing` vs the actual
   `abby-mm-soft-landing^explore-idle`).
4. The Animation Graph's async and transition evaluators interpolated missing
   channels toward bind pose.

## Fixes

- Direct YCD tracks now preserve previous **pre-IK** joint transforms for all
  missing channels and crossfade keyed bones over 0.18 seconds, without a
  temporary rest pose. The outgoing source is sampled before a transition.
- NativeViewport retains the last valid per-entity skin palette. When a new
  clip fails or is still loading, both GPU and CPU paths use retained skin,
  with the current mesh_world orientation. If the character has never
  produced a valid skin, it is withheld rather than drawn in bind pose.
- The playback continues across transient animation-component disappearance.
- Animation Graph remembers its last pre-IK pose, preserves all unkeyed
  channels and avoids crossfades through bind. Renderer waits for required
  graph clips before drawing a new graph.
- Eight incorrect Abby clip references replaced with exact, decoded native
  entries. 90-degree locomotion loops are used instead of **transition**
  strafe names, which did not exist as standalone clips.
- Skeleton local-transform continuity avoids overlapping buffer self-copies.

## Real YCD inspection

From the shipped `mm-explore.ycd` NEF8 metadata, 259 entries were found.
Native decoder QA verified **14** selected locomotion/landing/fall/crouch
clips (33 to 72 tracks in the examples), including both backward and
90-degree locomotion, sprint, soft/medium/hard landing, and falling. The
initial idle clip is covered by the original Animation Graph asset test.

**Known separate limitation:** `characters/abby/animations/traversal.ycd`
has NEF8 `content_kind=opaque:11` with zero native list-file entries;
`abby-ground-jump-run` cannot yet be verified as a native playable YCD clip.
The new retention rule prevents this failure from exposing a T-pose, but a
proper jump take-off animation still requires conversion to the supported
native clip dictionary. Do not claim this file was converted.

## Verification

- `cmake --build out/build --target northstarcpp_animation_graph_smoke
  northstarcpp_viewport` — success.
- Synthetic skeleton test: idle/full-body to sparse/partial, alpha 0, 0.5,
  1.0 — passed, all unkeyed bones retain old animated transforms.
- Animation Graph test: full-body to partial graph transition retains spine,
  blends hand, remains posed after transition — passed.
- Actual async YCD loader: 14/14 representative native clips decode
  successfully with nonempty joint tracks.
- `ctest -R ^northstarcpp_(animation_graph|skeletal_animation|
  scene_animation|player_action_events|player_blueprint)$`: **5/5 passed**.
- `node tests/abby_pistol_demo_smoke.cjs` — passed.
- `northstarcpp_player.exe --runtime-init-smoke` using AbbyPistolDemo:
  D3D12 READY 12 passed/0 failures; Vulkan READY 12 passed/0 failures.
- A live rotated/transitioning character GPU capture and visually validated
  no-T-pose frame sequence are still **outstanding**. Startup smoke and
  headless unit tests are not equivalent to that visual acceptance.

Local source and build only; **no production dist overwrite, commit or push**.
