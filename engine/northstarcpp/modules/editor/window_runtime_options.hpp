#pragma once
// Player/Editor command-line contracts, kept independent of runtime frame code.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>

namespace northstarcpp::runtime_options {
namespace fs = std::filesystem;
struct Options {
    fs::path scene_path;
    fs::path transition_scene;
    fs::path archive_path;
    fs::path archive_data;
    fs::path smoke_output;
    fs::path shared_asset_root;
    fs::path asset_zones_path;
    bool check_shared_assets = false;
    bool new_document = false;
    bool warp = false;
    bool smoke = false;
    bool check_scene = false;
    bool runtime_init_smoke = false;
    bool character_smoke = false;
    bool fps = false;
    bool explore = false;
    bool fps_ui_smoke = false;
    bool packaged_game = false;
    bool inspect_providers = false;
    // Embed gameplay as a Win32 child of the Qt Editor full-client overlay.
    // This is a renderer surface parent, not an HWND opened as a separate app.
    HWND embed_parent = nullptr;
    bool embed_handshake_smoke = false;
};

Options parse_options(bool editor);
fs::path discover_shared_assets(Options const&, fs::path const&);
} // namespace northstarcpp::runtime_options
