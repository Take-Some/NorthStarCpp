// NorthStarCpp — unified asset root discovery contract.
// This layer discovers physical engine content once for every host (Player,
// Qt Editor, Viewport, previews, streaming). Actual VFS provider/overlays
// are owned by AssetStore and remain replaceable through engine.assets ABI.
#pragma once
#include <filesystem>

namespace northstarcpp::asset_paths {

// Return a canonical, non-symlinked engine sharedAssets directory, or {}.
// preferred/fallback may be a project path, executable, content directory,
// or any descendant of the repository. Installed NSP archives are mounted
// separately by AssetStore even when no loose shared assets are installed.
std::filesystem::path locate_shared_root(
    std::filesystem::path const& preferred,
    std::filesystem::path const& fallback = {});

// Process executable's directory, not its current working directory.
std::filesystem::path executable_dir();

} // namespace northstarcpp::asset_paths
