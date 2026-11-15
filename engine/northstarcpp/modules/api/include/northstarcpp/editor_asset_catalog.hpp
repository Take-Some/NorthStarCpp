// NorthStar Editor asset catalog; no Win32, Qt, or platform shell dependency.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace northstarcpp::editor {
enum class AssetKind {
    folder, mesh, material, texture, audio, scene, script, animation, shader, other, prefab, character, material_function
};
struct AssetRecord {
    std::string virtual_path;  // UTF-8, '/' separated, root-relative
    std::string filename;
    AssetKind kind{AssetKind::other};
    bool directory=false;
    std::uintmax_t size=0;
    // Parsed on the catalog's background indexing worker. Invalid prefab
    // files stay visible but never masquerade as a valid scene template.
    std::size_t prefab_actor_count=0;
    bool prefab_valid=false;
    // True for the separate project/prefabs collection. Ordinary .nprefab
    // files inside assets/ retain their original Content-relative VFS paths.
    bool external_prefab=false;
    // For NEF8 YDD/YTD the physical source dictionary is a virtual folder.
    // Its children are read-only virtual files, not filesystem paths.
    std::string container_path;
    std::string entry_name;
    bool is_virtual_entry() const noexcept {return !entry_name.empty();}
    bool is_list_file() const noexcept {
        return !container_path.empty() && entry_name.empty();
    }
    std::string asset_reference() const {
        return is_virtual_entry()?container_path+"@"+entry_name:virtual_path;
    }
};
char const* kind_name(AssetKind kind) noexcept;
AssetKind kind_from_extension(std::filesystem::path const& path) noexcept;
bool safe_virtual_path(std::string const& name) noexcept;

class AssetCatalog {
public:
    // Recursion never follows symlinks and is bounded by max_records.
    // Scan errors on individual entries do not make the editor unavailable.
    void scan(std::filesystem::path root, std::size_t max_records=100000);
    std::filesystem::path const& root() const noexcept { return root_; }
    std::vector<AssetRecord> const& entries() const noexcept { return entries_; }
    bool truncated() const noexcept { return truncated_; }
    // Physical filesystem paths only; virtual children cannot be opened as
    // ordinary files or modified through filesystem operations.
    std::filesystem::path resolve(std::string const& virtual_path) const;
    AssetRecord const* find(std::string const& virtual_path) const noexcept;
    // Physical file owning the selected virtual child (or normal asset).
    std::filesystem::path backing_file(std::string const& virtual_path) const;
    std::string asset_reference(std::string const& virtual_path) const;
private:
    std::filesystem::path root_;
    std::vector<AssetRecord> entries_;
    bool truncated_=false;
};
} // namespace northstarcpp::editor
