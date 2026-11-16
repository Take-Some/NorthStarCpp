# Footsteps NAC — NEF8 migration, 2026-11-15

All NorthStar `.nac` archives now use the existing **NEF8 ListFile
envelope**, NEF8 content kind **12 (Audio)**, audio schema v1.
The previous independent `NAC1` envelope has been retired.

## Active resources

```text
sharedAssets/audio/footsteps/
  dirt.nac     37 tracks      905,986 bytes
  grass.nac    32 tracks      347,987 bytes
  metal.nac    32 tracks      673,934 bytes
  mud.nac      31 tracks    1,104,795 bytes
  snow.nac     32 tracks    1,309,849 bytes
  stone.nac    37 tracks      510,468 bytes
  water.nac    48 tracks    1,122,553 bytes
  wood.nac     37 tracks      778,048 bytes
```

**286/286 audio tracks**, 8 packs, total **6,753,620 bytes**.
Previous uncompressed NAC1 packs totalled 9,045,456 bytes.
NEF8 raw-DEFLATE saves approximately **25.3%** without losing audio data.

All source WAVs are PCM16 mono/48 kHz. Names within each container
remain `walk_01`, `run_02`, `sprint_03`, `land_01`, etc. Track
IDs remain the deterministic 32-bit FNV-1a of the local name.

## Migration and rollback

- The new writer creates NEF8 files with `NEF8` magic, audio kind 12
  and typed audio metadata; it never emits standalone `NAC1`.
- The existing native NEF8 decoder validates raw-DEFLATE data and BLAKE3;
  the NAC decoder checks each track's CRC32 and metadata bounds.
- The independent Python standard-library verifier inflates DEFLATE and
  compares every PCM byte to the corresponding source WAV.
- All eight new archives were validated **before** an atomic replacement.
- The original `NAC1` files are backed up under
  `out/nac1-backup/footsteps/` for rollback.
- The source files under
  `C:\Users\Kayla\Documents\NorthStar\Shared\Source\audio\footsteps`
  were not modified.
- `footsteps.yscd.json` is legacy data: it is not parsed, imported,
  bundled, or used by the NAC import process.

## Reproducible build / verification

```powershell
cd C:\Users\Kayla\Documents\NorthStar\NorthStarCpp
ninja -C out/project-manager-qt northstarcpp_nac_pack
powershell -NoProfile -ExecutionPolicy Bypass -File tools/pack_footsteps.ps1
```

The PowerShell importer discovers surface directories, verifies already
converted NEF8 files, and performs a validated atomic migration for an old
`NAC1` pack. It does not silently overwrite a changed NEF8 file. New
NEF8 outputs are built in `out/nac-nef8-staging/footsteps/` and atomically
replaced with backup on the same volume.

The Qt Content Browser indexes the NEF8 NAC metadata without expanding
the PCM body. The legacy `NAC1` format is not a supported active NAC.
A separate NEF8 v2 chunked-audio codec is future work for per-track
selective compressed reads and runtime streaming.

Targeted NEF8/NAC codec, Content Browser and provider regression tests
were performed. Full release distribution / game listening QA, commit and
push are not included in this migration.
