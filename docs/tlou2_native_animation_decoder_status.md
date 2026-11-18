# Native ICE and ANIMMAGICSTR animation integration status

Date: 2026-11-18. Status: partial and source-verified, NOT playable native YCD.

## Original private source assets
Game: C:\Games\The Last of Us Part II Remastered
Staging: C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\animations

### Ellie
- 37 original anim-ellie-* banks, 6,225 ICE source buffers.
- 6,221 ICE self-contained compressed clips with real headers, no bone decode.
- 4 64-byte external stream references; not self-contained.
- At 0x0c ICE revisions observed: 1:571, 2:95, 6:30, 7:5525; 0:4.
- Original header: bytes 0x08 total extent, 0x0c revision, 0x0e frame count,
  0x1c sample rate, 0x28 last frame index, 0x2c frame time.
- The entire source set has a 30 Hz rate. Header fields are verified against
  frame counts and buffer boundaries, NOT proof of decoded bone tracks.
- Native source inventory: ellie/ice-metadata.json
- 72 older ICE skeleton-only sources remain separately preserved.

### Isaac
- Six original cinematic PAKs with 18 specifically named isaac= ANIM refs.
- Six original ANIMMAGICSTR multi-actor STM source files, version 2681,
  168-byte base header.
- Source inventory: isaac/cinematic_streams/stm-metadata.json
- The Isaac actor tracks have NOT been isolated from the multi-actor stream.

## Source code
- tools/native_ice_header.py: independently validated ICE source timing.
- tools/native_stm_header.py: checksums and bounds for STM cinematic headers.
- tools/native_decoded_tracks_to_ycd.py: strict import of decoder-produced
  full joint keyframes into existing NEF8 YCD v3, with YMT joint mapping,
  original SHA256, frame count, quaternion normalization checks, and metadata.
- tests/test_native_ice_ycd_pipeline.py: synthetic-only ABI and rejection QA.

## Test record
- Real ICE header validation: 6225/6225 PASS (6221 compressed + 4 external).
- STM header validation: 6/6 PASS, 0 decoded actor tracks.
- Actual original YMT structures: Ellie 1074 joints, Isaac 757.
- Python importer and negative checks: 10 PASS.
- Existing compiled C++ Animation Graph, Scene Animation, Skeletal Animation
  smokes: PASS. These were NOT new builds.
- Production-native Ellie/Isaac YCD: 0; Root Motion-to-Jolt: not integrated.

## Next implementation gates (must not be skipped)
1. Reverse-engineer per-revision ICE compressed joint channels: quantization,
   channel IDs, additive tracks, sparse bone keys, interpolation, root motion.
   Compare actual per-frame poses against independently captured references.
2. Parse ANIMMAGICSTR multi-actor streaming records and map the 18 isaac=
   source descriptors to real original Isaac pose tracks.
3. Recover original helper-joint bind transforms (Ellie 503 unresolved;
   Isaac 276 unresolved) and confirm YMT/YDD joint-space compatibility.
4. Apply root trajectory to the CharacterMotor/entity, not the skinned mesh
   alone. Until implemented, the YCD writer preserves original root keyframes
   but rejects in_place mode to prevent false capsule synchronization.
5. Bind verified YCD clips into NPC Animation Graph, validate transitions and
   foot contacts, visual-test D3D12 and Vulkan in KaylaNightCity.

## Reproduction from the NorthStarCpp repository root

    python tools/native_ice_header.py --output C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\animations\ellie\ice-metadata.json
    python tools/native_stm_header.py --output C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\animations\isaac\cinematic_streams\stm-metadata.json
    python -m unittest discover -s tests -p test_native_ice_ycd_pipeline.py -v

After a REAL separately verified native decoder generates a full document
with schema northstar.tlou2.decoded_joint_tracks.v1, use:

    python tools/native_decoded_tracks_to_ycd.py --input real-decoded.json
    python tools/native_decoded_tracks_to_ycd.py --input real-decoded.json --output private\real-original.ycd --write

Do not mark a source ICE blob as YCD or substitute guessed tracks.
No public proprietary assets, production resource replacements, commit or push.
