# Pistol audio import — XVAG -> NEF8/NAC (2026-11-16)

## Scope

Source: `sharedAssets/audio/weapon/pistol/*.xvag` (29 XVAG audio clips).

Output: `sharedAssets/audio/weapon/pistol/pistol.nac`.

Converter: `tools/pack_pistol_xvag.py`, using the vendored `vgmstream-cli`
to decode Sony XVAG PlayStation 4-bit ADPCM to **PCM16**.
Output is the current engine **NEF8 v1 audio ListFile**: kind 12, audio
schema 1, `northstar.nac.nef8.v1`, BLAKE3-expanded-body checksum,
raw-DEFLATE compression, 16-byte aligned track PCM, per-track CRC32,
and deterministic FNV-1a track IDs.

## Result

- 29/29 individual audio tracks packed; no missing clips.
- `pistol.nac`: **1,537,900 bytes**.
- Expanded PCM: **2,925,188 bytes** including inter-track alignment.
- Original sampling rates preserved (32,000 / 44,100 / 48,000 Hz).
- 1 channel for every clip, 16-bit PCM.
- One-shot clips have looping disabled.
- Source XVAG files and `pistol.yscd` were **left unchanged**.

Included tracks: `empty`, `equip`, `fire_body`,
`fire_reflection_01..03`, `fire_tail_01..03`, `reload`,
`shell_contact_hard_01..04`, `shell_contact_medium_01..03`,
`shell_contact_small_01..03`, `shell_contact_soft_01..05`,
`shell_roll_01..03`, `unequip`.

## Verification

1. All XVAG clips decoded successfully using vgmstream.
2. NAC header/offsets, audio track limits, track names and IDs validated.
3. Full raw-DEFLATE expansion and outer BLAKE3 hash verified.
4. CRC32 verified for each track.
5. Every decoded NAC PCM sample compared against XVAG-decoded PCM (exact).
6. Independent reader also accepted existing engine
   `sharedAssets/audio/footsteps/wood.nac` (37 tracks), accounting for
   quoted integer metadata written by Boost property_tree.

To recheck:
```powershell
cd C:\Users\Kayla\Documents\NorthStar\NorthStarCpp
python tools/pack_pistol_xvag.py --verify-only
```

To build on a clean checkout, remove the generated NAC from the output
directory and run `python tools/pack_pistol_xvag.py`.
The script **does not silently overwrite** existing output. For an existing
NAC, it decodes the XVAG sources and checks PCM identity.

## Limitations

The native `northstarcpp_nac_pack.exe` was not available in the current
workspace build outputs, so conversion used a Python NEF8 writer that
follows the current engine's C++ writer and schema. The archive has
passed independent full-binary verification, including BLAKE3/CRC32,
but **runtime playback/Content Browser QA in a newly built engine**
has not been performed. No engine binary was modified.
No commit or push was made.

The archive is a collection of raw PCM16 clips compressed as a single NEF8
body. It does not replace `pistol.yscd` cue/behavior definitions and does
not yet set up weapon fire or reload events in game scripts.
