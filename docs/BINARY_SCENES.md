# NorthStar Scenes — NEF8 ListFile v2

Updated 2026-11-14. Extension: .nscn; signature NEF8; content kind 6.
Canonical schema 2 uses independent raw-DEFLATE blocks. Decoders also accept
legacy NSCN v1, NEF8 single-body scene schema 1 and text .nscene scenes.

## Envelope (fixed 128-byte header, little endian)

| Offset | Field | Value |
| --- | --- | --- |
| 0 | bytes[4] | NEF8 |
| 4 | u16 major | 1 |
| 6 | u16 header bytes | 128 |
| 8 | u16 kind | 6, Scene |
| 10 | u16 compression | 2, independent DEFLATE |
| 12 | u16 schema | 2 |
| 14 | u16 flags | 0 |
| 16 | u64 | compressed payload offset, after index |
| 24 | u64 | combined compressed chunk bytes |
| 32 | u64 | total expanded chunk bytes |
| 40 | u64 | total typed records |
| 48 | u64 | index offset = 128 |
| 56 | u64 | index byte length |
| 64 | 32 bytes | BLAKE3 concatenated expanded chunks |
| 96 | 16 bytes | truncated BLAKE3 of index |
| 112 | u64 | schema marker 2 |

## NSCK chunk index

The uncompressed index starts directly after the envelope: NSCK magic,
u16 major 2, u16 minor 0, u32 chunk count and u32 entry stride 60.

Every entry contains: u8 kind (0 global, 1 streaming cell); u8 reserved;
u16 UTF-8 name length; u32 record count; u64 compressed file offset;
u32 compressed size; u32 expanded size; 32-byte expanded-chunk BLAKE3;
u32 compressed-chunk CRC32; and the variable-length cell name after the
60-byte fixed entry.

The first chunk holds global scene metadata and any persistent entities.
Further chunks hold entities bound to named streaming cells, and their
entity-owned components. Empty authored cells need no separate chunk.
Offsets are contiguous; duplicate names and overlapping ranges are rejected.

## NSLR v2 typed records

Every expanded chunk contains the NSLR signature, u16 major 2, u16 minor 0,
u32 record count, u32 record stride 28, and a 28-byte entry per record.
Fields: u16 kind, u16 version, u64 stable entity ID, u32 payload offset,
u32 payload size, u32 payload CRC32, u32 original record ordinal.

Component kinds: 1 Document, 2 Entity, 3 Layer, 4 Streaming Cell, 5 Volume,
6 Portal, 7 versioned plugin ECS component, 8+ future/opaque records.
An entity component stores type identifier, u32 schema version, u32 binary
length, then its opaque bytes. The reader restores original record order
from ordinal entries, maintaining deterministic save/load.

Unknown kinds and future versions of known kinds are stored without data loss
in Scene::opaque_records. The base SceneObject record still bundles some core
fields; splitting them into independently versioned built-in ECS components
is a future schema migration.

## Plugins and ABI

SDK C++ registry: include/northstarcpp/scene_component_registry.hpp.
Plugin C ABI table: include/northstarcpp/scene_component_plugin.h, ABI 1.
Host can pass the stable C callback table to a plugin; the plugin registers
type names, version ranges and a payload validator. Unregistered or
unsupported versions survive as opaque bytes. Plugins must unregister schema
callbacks before unloading their DLL/SO. Automatic registration during every
engine.* provider load is not implemented yet.

## Direct selective disk reads

- index_scene_chunks(path) reads only the bounded header and chunk index.
- load_scene_chunk(path, cell) reads the selected compressed range, checks its
  compressed CRC32, expands it, checks chunk BLAKE3 and component CRC32.
- Loading cell A does not inflate or depend on cell B's compressed payload.
- Full scene decoding additionally validates global BLAKE3 and semantic graph.

Constraints: max 16 MiB file/expanded data, max 65536 records, max 8193
chunks, max 1 MiB index. Integrity hashes are not authentication signatures.

The SceneCellDiskResidency adapter can now consume SceneWorld load/unload
transitions and fetch or evict independently compressed cell records. It
stages all new loads before committing cache changes, and never inflates
unrelated cell bodies. It is currently synchronous and SceneWorld itself
still starts from an authored Scene in memory. Asynchronous loading, ECS
instantiation, physics/render residency and metadata-only cold startup
remain future integrations.

## Compatibility and verification

Old NSCN v1 and NEF8 Scene v1 remain readable and convert to v2 on
explicit save. The Python starter authoring tools now write v2 and the
game exporter reads v1/v2 component references. Existing project files have
not been rewritten.

Native regression covers plugin C and C++ registration, missing plugin
payload preservation, unknown future versions, direct cell reads, corruption,
legacy migration and deterministic roundtrips. Python tests cover bounded
inflation, index validation, per-cell isolation and v1 compatibility.
Cross-language tests verify Python -> native byte-exact save and v1->v2
migration.

Remaining release checks: clean full build, tests across installed dist,
streaming stress/profiling and controlled production DLL replacement.

## NEF8 v2 / World Partition integration — range ABI and asynchronous residency

The host's optional NorthStarAssetsRangeApi (ABI 3) provides bounded,
exact positioned reads. ABI 1 and archive-mount ABI 2 are unchanged.
The Directory VFS provider implements native ranges for both loose
files and uncompressed FAT/DB archive entries. Other providers can
continue using ABI 1; AssetStore::read_range uses a buffered full-file
fallback if ABI 3 is missing. A compressed FAT/DB entry remains
unsupported by the existing archive provider.

SceneChunkReader caches and verifies the NEF8 v2 NSCK index once,
then reads and decompresses only a requested chunk. It accepts either
a local filesystem path or an AssetStore plus a virtual scene path.
This supports archive-backed ranged reads without reading/decompressing
the complete Scene ListFile.

SceneCellAsyncResidency is a nonblocking, bounded (default four in-flight
jobs) bridge to SceneWorld's current desired cells. The frame thread
polls completed futures, retains only requested decoded cells and drops
late results when cells are unloaded. Corrupt chunks are recorded as
errors rather than partially committed. The Reader/cache lifetime must
remain shorter than the associated VFS AssetStore lifetime.

The shared Editor/Player window.cpp now creates this bridge for NEF8 v2
startup scenes (including VFS-mounted archive startup scenes) and calls
apply() after every SceneWorld::update(). The streaming inspector reports
pending and decoded cell counts. Unsaved editor changes disable on-disk
index use until saved, avoiding use of stale serialized cell records.
The application still initially reads the whole Scene into its authoring
runtime, including its ECS and physics proxies. It does NOT yet create or
free ECS/physics/GPU resources directly from decoded cell records.
Asynchronous cell I/O and resource lifetime management are separate
runtime milestones.

Verification:
- Native VFS CTest covers exact ranges in loose files, combined FAT/DB
  and split FAT/DB, and NEF8 block reading directly inside a FAT archive.
- NEF8 asynchronous VFS CTest covers cached index, global plus two
  sectors, A->B transitions and inactive cell eviction.
- Player and Editor window.cpp compiled with real CMake/Ninja targets
  in the isolated Release build.
- Full Editor/Player linking, installation, packed runtime smoke and
  clean-Windows release verification are separate uncompleted gates.

Relevant source files:
- include/northstarcpp/assets_plugin.h
- engine/northstarcpp/modules/assets/directory/directory.cpp
- engine/northstarcpp/modules/assets/directory/fat_archive.cpp
- engine/northstarcpp/modules/host/assets.cpp
- engine/northstarcpp/modules/host/scene_nef8_chunked.cpp
- engine/northstarcpp/modules/host/scene_chunk_streaming.cpp
- include/northstarcpp/scene_chunk_index.hpp
- engine/northstarcpp/modules/editor/window.cpp
