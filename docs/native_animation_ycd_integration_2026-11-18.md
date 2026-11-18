# Native animation -> NorthStarCpp YCD conversion and integration

Date: 2026-11-18. Scope: working native container/installer, NOT completed original codec decoder.

## Original source data
- Ellie: 6221 self-contained compressed ICE buffers + 4 external stream references.
- Isaac: 6 validated ANIMMAGICSTR multi-actor streams, 18 Isaac-specific ANIM references.
- Ellie YMT: 1074 joints. Isaac YMT: 757 joints.
- Original ICE transform channels are still compressed and not converted to bone tracks.
- An independent fully working and verified decoder for ICE v1/v2/v6/v7 and
  ANIMMAGICSTR has NOT been established. No genuine playable original YCD
  has been emitted or installed.

## Implemented
- tools/native_decoded_tracks_to_ycd.py now accepts verified ICE decoder output
  AND verified ANIMMAGICSTR decoder output, requiring for Isaac the exact
  source scene PAK hash and unique isaac= ANIM record.
- Input requires exact source SHA256, imported YMT SHA256 and joint names,
  source-native quaternion basis, original clip timing where verifiable,
  normalized transform samples, explicit Root Motion policy.
- tools/dev/convert_native_animation_ycd.py imports decoded JSON manifests
  from the private Extracted/TLOU2/decoded directory, constructs NEF8 YCD v3,
  independently checks dictionary fields, frame counts and BLAKE3 digest,
  writes private converted files with --convert, installs under project
  animations/npcs/native/{actor}/ with --convert --install.
- Refuses mutated existing assets, mismatched source hashes, wrong actor
  skeletons and unsafe clip logical paths; no silent fallback.
- Project engine already supports YCD playback via Scene.playAnimation and
  asynchronous AnimationGraph YCD loading; no new runtime codec DLL required
  to play valid converted YCD v3.

## Private conversion report
C:/Users/Kayla/Documents/NorthStar/Extracted/TLOU2/native-ycd-conversion-report.json

Current scan: 0 decoder-produced input manifests, 0 genuine original YCD
conversions, 0 original installed animations. This is an intentional
fail-closed result, not a conversion success.

## Verified existing NPC fallback (distinct from originals!)
KaylaNightCity already has source-labeled Abby-derived YCD retargets:
- Projects/KaylaNightCity/assets/animations/npcs/ellie_locomotion.ycd:
  npc-idle (501 frames), npc-walk (106 frames), 35 tracks at 30 Hz.
- Projects/KaylaNightCity/assets/animations/npcs/isaac_locomotion.ycd:
  npc-idle (501 frames), npc-walk (106 frames), 35 tracks at 30 Hz.
Existing scene components reference npc-idle and script main.js switches
npc-walk when agents move. The current YCD files must NOT be relabeled
as original TLOU2 animations.

## QA
- 10 Python single-clip decoder-ABI/strict-parser tests PASS.
- 5 Python end-to-end batch conversion/install tests PASS with synthetic key data.
- Original Ellie ICE headers and Isaac STM headers were validated in prior QA.
- Existing C++ Animation Graph / Scene Animation / Skeletal Animation
  smoke binaries passed in the prior cycle; no fresh production engine build.
- Native motion on real characters has NOT passed visual/physics QA.

## Reproduce
From C:/Users/Kayla/Documents/NorthStar/NorthStarCpp:

    python -m unittest discover -s tests -p test_native_ice_ycd_pipeline.py -v
    python -m unittest discover -s tests -p test_native_animation_convert_batch.py -v
    python tools/dev/convert_native_animation_ycd.py --report C:/Users/Kayla/Documents/NorthStar/Extracted/TLOU2/native-ycd-conversion-report.json

Once a verified real decoder has produced valid original per-frame track
manifests inside Extracted/TLOU2/decoded/{ellie,isaac}:

    python tools/dev/convert_native_animation_ycd.py --convert
    python tools/dev/convert_native_animation_ycd.py --convert --install

Do not run --install on counterfeit or unvalidated transform data. Native
clip state names should then be wired to Animation Graph only after validation.

## Blockers
1. Decode ICE bone-key compression and source channel mapping.
2. Extract original Isaac tracks from cinematic scene STM (multi-actor).
3. Validate coordinate basis and helper joint bind transforms.
4. Hook Root Motion to Jolt CharacterMotor and verify ground contact.
5. Validate clips in D3D12/Vulkan and release build.

Game source files and derived assets are copyright-restricted; do not
commit or redistribute proprietary game data without appropriate rights.
No commit, push or production dist replacement in this run.
