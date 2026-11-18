# Ellie native import — full-resolution texture and shading QA (2026-11-17)

## Scope and provenance

Original The Last of Us Part II Remastered PC sources only. All original
PAK/game payloads remain in the local installed game. **Do not commit,
publish or distribute the original or derived game textures, meshes or NSPs.**

- Indexed 11 original `common` and `sp-common` `texturedict3/*.pak`.
- Matched **397 / 397** material texture hash64 IDs, no missing IDs.
- Verified all **2,805** original low-resolution source PAK mips byte-for-byte
  against the streaming dictionary's larger mip chains.
- Upgraded **383** entries; retained **14** already-complete entries.
- Native `characters/ellie/textures/ellie.ytd`: **397** textures,
  **4,047** mip levels, **224,167,836** original BCn/RGBA pixel bytes,
  **132,770,081** NEF8 bytes.
- SHA-256:
  `2b73052c319d395bfefaa163e605c7ad2c86b507eb272c42ca904b8f2f95ad63`.
- Original 64px YTD backed up privately at
  `characters/ellie/source/previous-lowres-ellie.ytd`.
- Full provenance report at
  `characters/ellie/source/fullres-texture-report.json`.
- Stream hashes/index report at
  `characters/ellie/source/streamed-texture-index.json`.

### Repeatable import (from NorthStarCpp root)

```powershell
python tools/native_texturedict.py
python tools/native_fullres_ellie.py --no-replace
python tools/native_fullres_ellie.py
python tools/native_ymat.py --force
python tests/ellie_native_material_skin_smoke.py
```

**Safety guard:** running `python tools/native_ytd.py --force`
does not silently overwrite the full-res YTD with PAK-local low-res mips.
Explicit `--allow-downgrade` is required to discard the high-res map.

## Geometry normal reconstruction

- Validated original stream type **2** signed-byte (SNORM8x3) against
  original triangle winding: average dot product about **0.998**.
- Stream type **130** had been misread as three unrelated signed bytes:
  source-derived normal/face correlation was approximately zero. The
  exact proprietary encoding is not yet identified.
- Current conservative fallback derives unit area-weighted vertex normals
  from original triangle geometry for type 130 only.
- Both the original 15-part preview and 15 individual part YDDs were rebuilt; canonical `ellie.ydd` now has 38 submeshes and 100,038 vertices.
- This retains geometry, UVs, indices, bone streams and source hashes,
  but sacrifices exact authored vertex tangent frames on type 130.
- Unit-vector spot checks pass. **Exact normal/tangent codec parity
  remains OPEN.**

## Material adapters

- Native XML YMAT retains **42** real source `physical-character.fx`
  instances and **822** source/PBR texture bindings (previously 821).
- Added an original **sclera/iris** base-color fallback for an eye material
  that otherwise had no standard PBR albedo.
- Derived PBR `base_color` from `g_baseColor01` and `roughness` from
  `g_roughness01` where directly usable; PBR eye roughness 0.12 is
  an engine approximation, not the proprietary original shader.
- C++ `MaterialLoadSession` now folds a source `opacity` mask red
  channel into base-color alpha for native masked YMAT. The source
  texture data is not changed. No GPU provider ABI expansion.
- NEF8 read cap selectively increased to 256 MiB **for YTD only**;
  64 MiB limit remains for other formats. Existing v2 body limit stays
  256 MiB, with checksum verification.

## Helper bones / incomplete animation

- Source `ellie-skel.pak` has **1,074** joints under TWO hierarchy roots:
  `root` (520 authored transforms) and `root_helper` (554).
- Existing converter matches 51 helper aliases, but **503 helper
  bind transforms are not recovered** from a verified original data source.
- Source bone weights frequently reference the helper hierarchy.
  **Do not fabricate bind transforms or claim completed skinned facial QA.**
- Original ICE clips remain source-only; no validated YCD animation.
- GPU skeletal animation and exact authored face/hair deformation are
  therefore **NOT accepted**.

## Runtime and rendering verification

- **PASS**: native YTD parse, complete 4,047 mips, source hash/mip
  provenance, YMAT 42 materials and 822 links.
- **PASS**: rebuilt C++ runtime static library and native
  `northstarcpp_material_resources_smoke.exe` with Ellie full-res
  masked hair and eye materials loaded through real VFS, original BCn.
- **PASS**: D3D12 hardware smoke after repairing an unrelated sky
  shader constructor error X3014.
- **PASS**: Vulkan GPU present/resize/readback smoke.
- **PASS**: skeletal viewport smoke.
- **NOT VERIFIED**: same-camera, same-light, full-body Ellie screenshots
  on both D3D12 and Vulkan. The foreground Player capture is an
  extreme close-up of dark scene geometry; it is not usable as
  character appearance acceptance.
- Active `out/build/archive/northstar_shared.nsp` has CRC-matching
  up-to-date `ellie.ydd`, `ellie.ymat`, `ellie.ytd`.
  Extra temporary NSP backups were moved out of the auto-mounted
  archive directory to `out/qa/ellie/backups/`.

## Remaining release gates (not completed)

1. Identify exact source stream type-130 normal/tangent encoding, compare
   decoded vectors to original normals; remove geometry fallback.
2. Restore authored `root_helper` bind transforms via verified game
   source/physics rig data; validate static pose, IK, facial movement.
3. Implement proper skin, cornea/iris, anisotropic hair and physically
   masked transparency shader variants in **both** Vulkan and D3D12.
   Current material conversion is an approximated standard PBR bridge.
4. Export compatible YCD clips, then validate same pose/lighting in
   Vulkan/D3D12 and archive two GPU readback images, timings and logs.
5. Confirm acceptable image and animation against visually inspected
   game reference, including face close-up, silhouette, hair and hands.

**Release decision: PARTIAL. High-resolution source textures and material
runtime integration verified; exact rig/normal restoration, original
specialized shader parity and dual-backend visual acceptance remain OPEN.**


## Model assembly and material preview fix — 18 November 2026

The screenshot showed a dark silhouette with missing hair/outer clothing
and detached T-pose arms. The original canonical YDD was assembled only
from ellie-body, ellie-head and ellie-arms (15 meshes). This is a
**confirmed source assembly omission**, independent of the fullres YTD.

Canonical `characters/ellie/models/ellie.ydd` now contains **38 source
submeshes, 100,038 original vertices and 147,367 triangles** from 11
authentic character PAKs. Added source hair and loose strands, jacket
cloth, backpack, holster, bracelet and bandages. Original source vertex
positions, skin influence streams and material UUID bindings retained.
Alternate styles are still separate; no fabricated geometry or rig was
introduced.

All 38 YDD sections resolve a real YMAT. C++ MaterialLoadSession
integration QA confirmed **35 base-color-textured sections and
14 alpha-masked sections**. Original BCn albedo average channel values
in the runtime are head 116.196/255, eyes 148.842/255,
hair 68.596/255: texture pixels are **not** intrinsically black.

The Qt GPU asset preview had a key light at z=-4, behind the actor's
front (+Z). Its studio setup now uses a front key (z=+3.2) and a
soft front-side fill, preserving authored source material data.
The engine viewport uses a shared in-frame MaterialLoadSession to
avoid independently unpacking the 132-MiB YTD for every new YMAT slot.

Built the editor, player and viewport modules from current sources.
The active shared NSP was verified to include CRC-matching current
ellie.ydd (5,065,216 bytes), ellie.ymat and fullres ellie.ytd.
Static character texture/material GPU-provider smokes passed.

**Still not accepted:** full scene visual comparison on Vulkan and D3D12,
correct arm rest pose / original YCD animation, 503 unresolved helper
bind matrices, proper cloth/hair bone attachments. The 38-mesh import
is a complete *static visual assembly*, not a playable animated
character. The user must restart an existing Player/Editor process
to load relinked binaries and refreshed NSP if currently running.
