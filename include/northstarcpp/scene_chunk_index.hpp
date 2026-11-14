// NEF8 v2 Scene ListFile index cache and asynchronous cell residency.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace northstarcpp {
class AssetStore;
class SceneWorld;

struct SceneChunkInfo {
    std::string cell;  // empty: persistent document and scene-level records
    std::uint64_t file_offset=0;
    std::uint32_t compressed_bytes=0;
    std::uint32_t expanded_bytes=0;
    std::uint32_t record_count=0;
};
struct SceneChunkRecord {
    std::uint16_t kind=0;
    std::uint16_t version=1;
    std::uint64_t entity_id=0;
    std::uint32_t ordinal=0;
    std::string bytes;
};
// Legacy one-shot filesystem indexed readers remain supported.
std::vector<SceneChunkInfo> index_scene_chunks(std::filesystem::path const& file);
std::vector<SceneChunkRecord> load_scene_chunk(
    std::filesystem::path const& file, std::string const& cell);

// Cache a validated NEF8 header/index and issue bounded, positioned reads.
// A VFS-backed reader requires AssetStore to outlive all outstanding calls.
class SceneChunkReader final {
public:
    explicit SceneChunkReader(std::filesystem::path file);
    SceneChunkReader(AssetStore const& assets, std::string virtual_path);
    std::vector<SceneChunkInfo> chunks() const;
    std::vector<SceneChunkRecord> load(std::string const& cell) const;
    bool native_range_reads() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

// Existing synchronous adapter. Does not instantiate ECS/physics resources.
class SceneCellDiskResidency final {
public:
    explicit SceneCellDiskResidency(std::filesystem::path file);
    void apply(SceneWorld const& world);
    bool loaded(std::string const& cell) const;
    std::vector<SceneChunkRecord> const* records(std::string const& cell) const;
    std::size_t resident_cell_count() const noexcept {return resident_.size();}
private:
    SceneChunkReader reader_;
    std::unordered_map<std::string,std::vector<SceneChunkRecord>> resident_;
};

// Nonblocking staged I/O driven by SceneWorld's currently desired cell set.
// Must be polled on the owning frame thread. Reads/decompression run in
// background workers (bounded by max_in_flight). Unloaded cells never become
// resident even if a previously queued read completes later.
class SceneCellAsyncResidency final {
public:
    explicit SceneCellAsyncResidency(std::filesystem::path file,
                                     std::size_t max_in_flight=4);
    SceneCellAsyncResidency(AssetStore const& assets, std::string virtual_path,
                            std::size_t max_in_flight=4);
    void apply(SceneWorld const& world);
    bool loaded(std::string const& cell) const;
    bool has_chunk(std::string const& cell) const;
    std::vector<SceneChunkRecord> const* records(std::string const& cell) const;
    std::string error(std::string const& cell) const;
    std::size_t resident_cell_count() const noexcept {return resident_.size();}
    std::size_t pending_cell_count() const noexcept {return pending_.size();}
private:
    std::shared_ptr<SceneChunkReader> source_;
    std::vector<SceneChunkInfo> available_; // immutable cache; avoid per-frame index copies
    std::unordered_map<std::string,std::vector<SceneChunkRecord>> resident_;
    std::unordered_map<std::string,std::future<std::vector<SceneChunkRecord>>> pending_;
    std::unordered_map<std::string,std::string> errors_;
    std::size_t max_in_flight_;
};
} // namespace northstarcpp
