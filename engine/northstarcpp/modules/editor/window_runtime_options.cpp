#include <northstarcpp/asset_roots.hpp>
#include "window_runtime_options.hpp"
#include <shellapi.h>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace northstarcpp::runtime_options {
Options parse_options(bool editor)
{
    int count = 0;
    auto* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args)
        throw std::runtime_error("Cannot parse application arguments");
    std::unique_ptr<wchar_t*, decltype(&LocalFree)> const owner(args, LocalFree);
    Options result;
    for (int i = 1; i < count; ++i) {
        std::wstring_view const arg(args[i]);
        if (arg == L"--warp")
            result.warp = true;
        else if (arg == L"--smoke")
            result.smoke = true;
        else if (arg == L"--inspect-providers")
            result.inspect_providers = true;
        else if (arg == L"--check-scene")
            result.check_scene = true;
        else if (!editor && arg == L"--transition-scene" && i + 1 < count)
            result.transition_scene=args[++i];
        else if (arg == L"--character-smoke")
            result.character_smoke = true;
        else if (arg == L"--runtime-init-smoke")
            result.runtime_init_smoke = true;
        else if (!editor && arg == L"--embed-parent" && i+1<count) {
            std::wstring const value(args[++i]);
            std::size_t end=0;
            auto const parsed=std::stoull(value,&end,0);
            if(end!=value.size() || !parsed)
                throw std::invalid_argument("Invalid --embed-parent HWND");
            result.embed_parent=reinterpret_cast<HWND>(
                static_cast<std::uintptr_t>(parsed));
        }
        else if (!editor && arg == L"--embed-handshake-smoke")
            result.embed_handshake_smoke = true;
        else if (arg == L"--shared-assets" && i + 1 < count)
            result.shared_asset_root = args[++i];
        else if (arg == L"--asset-zones" && i + 1 < count)
            result.asset_zones_path = args[++i];
        else if (arg == L"--check-shared-assets" && i + 1 < count) {
            result.check_shared_assets = true;
            result.shared_asset_root = args[++i];
        } else if (arg == L"--fps")
            result.fps = true;
        else if (arg == L"--explore") {
            result.explore = true;
            result.fps = true;
        }
        else if (arg == L"--packaged-game")
            result.packaged_game = true;
        else if (arg == L"--fps-ui-smoke")
            result.fps_ui_smoke = true;
        else if (!editor && arg == L"--archive" && i + 1 < count)
            result.archive_path = args[++i];
        else if (!editor && arg == L"--archive-data" && i + 1 < count)
            result.archive_data = args[++i];
        else if ((arg == L"--scene" || (editor && arg == L"--new-scene")) && i + 1 < count) {
            if (!result.scene_path.empty())
                throw std::runtime_error("Specify only one scene path");
            result.new_document = arg == L"--new-scene";
            result.scene_path = args[++i];
        } else if (arg == L"--smoke-output" && i + 1 < count)
            result.smoke_output = args[++i];
        else
            throw std::runtime_error("Usage: --scene FILE, or --archive FILE [--archive-data FILE] --scene VIRTUAL_PATH. "
                "Options: --inspect-providers, --warp, --smoke, --check-scene, --fps, --explore, --character-smoke, --smoke-output FILE, --new-scene FILE (editor), --check-shared-assets DIR, --shared-assets DIR, --asset-zones FILE.");
    }
    if ((!result.archive_path.empty() && result.scene_path.empty()) ||
        (!result.archive_data.empty() && result.archive_path.empty()))
        throw std::runtime_error("Use --archive FILE [--archive-data FILE] --scene VIRTUAL_PATH");
    if (!result.transition_scene.empty() && (editor || result.smoke ||
        result.scene_path.empty() || result.check_scene || result.runtime_init_smoke ||
        result.character_smoke || result.fps_ui_smoke))
        throw std::invalid_argument(
            "--transition-scene requires interactive Player, --scene and a target .nscn");
    if (result.check_scene && (editor || result.smoke || result.scene_path.empty()))
        throw std::runtime_error("--check-scene requires a player and --scene FILE");
    if (result.runtime_init_smoke && (editor || result.smoke ||
        result.check_scene || result.scene_path.empty()))
        throw std::runtime_error("--runtime-init-smoke requires a player and --scene FILE");
    if (result.fps && editor)
        throw std::runtime_error("FPS mode is available in the standalone player");
    if(result.embed_handshake_smoke && !result.embed_parent)
        throw std::invalid_argument("--embed-handshake-smoke requires --embed-parent");
    if(result.embed_parent) {
        if(editor || result.smoke || result.check_scene || result.runtime_init_smoke ||
           result.character_smoke || result.fps_ui_smoke || result.packaged_game)
            throw std::invalid_argument(
                "--embed-parent requires interactive Player mode");
        if(!IsWindow(result.embed_parent))
            throw std::invalid_argument("Editor game surface HWND is invalid");
        DWORD parent_pid=0;
        GetWindowThreadProcessId(result.embed_parent,&parent_pid);
        if(!parent_pid || parent_pid==GetCurrentProcessId())
            throw std::invalid_argument("Embedded Player requires a foreign Editor HWND");
    }
    if (result.fps_ui_smoke && (!result.fps || result.scene_path.empty() || editor))
        throw std::runtime_error("--fps-ui-smoke requires a player, --fps and --scene");
    if (editor && result.smoke && result.smoke_output.empty())
        throw std::runtime_error("Editor smoke requires --smoke-output FILE");
    return result;
}

fs::path discover_shared_assets(Options const& config, fs::path const& project_root) {
    if (!config.shared_asset_root.empty()) {
        if (!fs::is_directory(config.shared_asset_root))
            throw std::runtime_error("Configured sharedAssets directory is missing");
        return fs::absolute(config.shared_asset_root);
    }
    return asset_paths::locate_shared_root(project_root,
        asset_paths::executable_dir());
}

} // namespace northstarcpp::runtime_options
