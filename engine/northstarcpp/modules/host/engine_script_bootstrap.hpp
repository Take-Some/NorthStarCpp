// NorthStarCpp immutable Script API bootstrap and project-root override resolver.
// Kept separate from Win32 Player UI and per-frame gameplay logic.
#pragma once
#include <filesystem>
#include <functional>
#include <cstddef>
#include <cstdint>

namespace northstarcpp {
class ScriptRuntime;
class AssetStore;
class Logger;
namespace script_bootstrap {
struct Options {
    std::filesystem::path project_root;
    std::filesystem::path archive_index;
    std::filesystem::path archive_data;
    bool load_project_entry=false;
    // Existing Player validation contract, also used for archived references.
    std::function<void(std::filesystem::path const&)> validate_project_path;
};
struct Report {
    std::size_t embedded=0;
    std::size_t project_overrides=0;
    std::size_t project_entries=0;
    std::uint64_t source_bytes=0;
    std::uint64_t elapsed_us=0;
};
Report load(ScriptRuntime& runtime,AssetStore const& project_assets,
            Options const& options,Logger* logger);
} // namespace script_bootstrap
} // namespace northstarcpp
