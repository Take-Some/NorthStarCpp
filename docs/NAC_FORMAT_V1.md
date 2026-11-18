# NorthStar Audio Container (.nac) — NEF8 Audio ListFile

Updated: 2026-11-15.

**.nac is a typed NEF8 container**, not a separate NAC1 envelope.
The common `engine.assets` NEF8 reader/writer implements the outer format;
the audio codec owns only the audio metadata schema and PCM sample layout.

## NEF8 v1 envelope

Header: 128 bytes, little-endian. Identical NEF8 ListFile layout used by
native scene/material assets.

| Offset | Field | Value for .nac |
| --- | --- | --- |
| 0 | bytes[4] | `NEF8` |
| 4 | u16 major | 1 |
| 6 | u16 header size | 128 |
| 8 | u16 content kind | **12 = NorthStar Audio** |
| 10 | u16 compression | 1 = raw DEFLATE |
| 12 | u16 audio schema | 1 |
| 14 | u16 flags | 0 |
| 16 | u64 body offset | 128 + metadata byte size |
| 24 | u64 compressed bytes | compressed audio body |
| 32 | u64 expanded bytes | expanded PCM body, ≤256 MiB |
| 40 | u64 entry count | number of tracks, 1..1024 |
| 48 | u64 metadata offset | 128 |
| 56 | u64 metadata bytes | bounded JSON dictionary |
| 64 | bytes[32] | BLAKE3 of the expanded body |
| 112 | u64 version marker | 1 |

The generic NEF8 decoder uses bounded raw-DEFLATE expansion, verifies the
BLAKE3 digest and rejects malformed NEF8 headers. The generic writer is
`shared_assets::detail::serialize_list_file` in
`modules/host/asset_list_file_writer.cpp`. Other asset types can reuse this
writer without copying audio-specific serialization logic.

## NAC audio metadata and payload

JSON *inside the NEF8 metadata section* carries schema
`northstar.nac.nef8.v1` and a `tracks` array. Each track stores:
`id`, `name`, `codec`, `sample_rate`, `channels`, `frames`,
`offset`, `bytes`, `crc32`, `looping`, `loop_start`, `loop_end`.

This is internal NEF8 metadata, **not** a second JSON sidecar format.
The compressed NEF8 body expands to 16-byte-aligned interleaved PCM16
little-endian segments. Codec 1 denotes signed PCM16. CRC32 verifies
each individual track in addition to the outer NEF8 BLAKE3.

Limits: 1..1024 tracks per pack; mono/stereo 8..192 kHz; max 47 UTF-8
name bytes, unique track names and IDs, and up to 256 MiB of total expanded PCM.
The encoded NEF8 file also fits within the VFS limit of 256 MiB.
Loop end is exclusive (0 means the end of the track).

## Runtime, Asset Browser and performance

`nac::encode` and `nac::decode` use the canonical NEF8 envelope.
`nac::inspect_file` reads just 128 bytes of NEF8 header and the bounded
metadata section, allowing Content Browser to list all clips without
expanding the compressed PCM.

- The ordinary `audio/footsteps/` directory contains surface-specific NACs.
- Valid `.nac` is a virtual folder with `soundsFolder.png`.
- Each track uses `soundFile.png` and keeps a stable
  `audio/footsteps/metal.nac@track-ID` identity.
- `AudioEngine::play_nac_track` fully validates NEF8 then copies the selected
  PCM to the active provider.
- Legacy `NAC1` is **not** considered a valid NEF8 NAC. Old generated packs
  were migrated and preserved in `out/nac1-backup/footsteps/`.

**Streaming limitation:** NEF8 v1 raw-DEFLATE uses one compressed body for
the whole NAC. Asset Browser indexing is metadata-only, but playing even one
track currently expands the full pack. A future NEF8 v2 per-track chunk index
with bounded VFS range reads would avoid that and permit on-demand streaming.
Do not claim this v1 implementation performs independent per-track reads.

No Wwise or FMOD dependency; `footsteps.yscd.json` is a legacy source file,
not a NAC dependency. Runtime event graph, buses/DSP, codec plugins, hotplug
and full real-device QA remain separate work.
