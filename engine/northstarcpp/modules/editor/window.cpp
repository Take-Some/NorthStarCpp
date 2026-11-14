// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include "viewport.hpp"
#include "editor_tools.hpp"
#include "editor_prefab.hpp"
#include "editor_camera.hpp"
#include "content_browser.hpp"
#include <northstarcpp/scene_world.hpp>
#include <northstarcpp/scene_chunk_index.hpp>
#include <northstarcpp/scene_navigation_cells.hpp>
#include "crash_handler.hpp"
#include "resource_ids.h"

#include <northstarcpp/physics.hpp>
#include <northstarcpp/audio.hpp>
#include <northstarcpp/logger.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/input.hpp>
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/fps_assets.hpp>
#include <northstarcpp/ecs.hpp>
#include <northstarcpp/scripting.hpp>
#include <northstarcpp/save_game.hpp>
#include <fstream>
#include <northstarcpp/scene_animation.hpp>
#include <northstarcpp/frame_clock.hpp>
#include <northstarcpp/provider_inspector.hpp>
#include <northstarcpp/perlin.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <optional>
#include <locale>
#include <map>
#include <unordered_set>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <set>
#include <cstdint>
#include <stdexcept>
#include <string_view>

#ifndef NORTHSTARCPP_EDITOR
#define NORTHSTARCPP_EDITOR 0
#endif

namespace northstarcpp {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr bool editor = NORTHSTARCPP_EDITOR != 0;
constexpr wchar_t window_class[] = L"NorthStarCppWindow";
constexpr wchar_t viewport_class[] = L"NorthStarCppViewport";
constexpr UINT frame_timer = 1;
constexpr UINT smoke_message = WM_APP + 1;

enum Control {
    new_scene = 100, open_scene, save_scene_id, save_as, add_object, delete_object,
    play_scene, camera_timeline, object_list, apply_properties, player_object,
    fps_resume = 200, fps_restart, fps_exit,
    inspector_pages = 300, collision_object,
    menu_undo = 500, menu_redo, menu_duplicate, menu_add_child,
    menu_delete_subtree, menu_layer, menu_cell, menu_volume, menu_portal,
    menu_scene_v3, menu_bind_asset, menu_open_content, menu_inspect_assets,
    menu_validate_scene, menu_streaming_diagnostics,
    menu_streaming_overlay, menu_hlod_diagnostics,
    menu_script_attach, menu_script_remove,
    menu_launch_fps, menu_project_manager, menu_show_content, menu_legacy_content,
    menu_prefab_export, menu_prefab_insert,
    outliner_filter=700, gizmo_move, gizmo_rotate, gizmo_scale,
    gizmo_local, gizmo_snap, gizmo_step, content_bind, content_place,
    content_reveal
};

std::wstring to_wide(std::string_view text)
{
    int const size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (!size)
        return L"";
    std::wstring value(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), value.data(), size);
    return value;
}

std::string to_utf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    int const size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size)
        throw std::runtime_error("Object name contains invalid characters");
    std::string value(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        value.data(), size, nullptr, nullptr);
    return value;
}

std::wstring get_text(HWND control)
{
    int const length = GetWindowTextLengthW(control);
    std::wstring text(length + 1, L'\0');
    text.resize(GetWindowTextW(control, text.data(), length + 1));
    return text;
}

float parse_number(HWND control)
{
    std::wistringstream stream(get_text(control));
    stream.imbue(std::locale::classic());
    float number = 0.f;
    if (!(stream >> number) || !(stream >> std::ws).eof())
        throw std::runtime_error("Enter a number using a decimal point");
    return number;
}

void set_number(HWND control, float number)
{
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(8) << number;
    SetWindowTextW(control, stream.str().c_str());
}

struct Options {
    fs::path scene_path;
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
    bool abby = false;
    bool abby_smoke = false;
    bool fps = false;
    bool explore = false;
    bool fps_ui_smoke = false;
    bool packaged_game = false;
    bool inspect_providers = false;
};

Options options()
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
        else if (arg == L"--abby")
            result.abby = true;
        else if (arg == L"--abby-smoke") {
            result.abby = true;
            result.abby_smoke = true;
        }
        else if (arg == L"--runtime-init-smoke")
            result.runtime_init_smoke = true;
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
                "Options: --inspect-providers, --warp, --smoke, --check-scene, --fps, --explore, --abby, --abby-smoke, --smoke-output FILE, --new-scene FILE (editor), --check-shared-assets DIR, --shared-assets DIR, --asset-zones FILE.");
    }
    if ((!result.archive_path.empty() && result.scene_path.empty()) ||
        (!result.archive_data.empty() && result.archive_path.empty()))
        throw std::runtime_error("Use --archive FILE [--archive-data FILE] --scene VIRTUAL_PATH");
    if (result.check_scene && (editor || result.smoke || result.scene_path.empty()))
        throw std::runtime_error("--check-scene requires a player and --scene FILE");
    if (result.runtime_init_smoke && (editor || result.smoke ||
        result.check_scene || result.scene_path.empty()))
        throw std::runtime_error("--runtime-init-smoke requires a player and --scene FILE");
    if (result.fps && editor)
        throw std::runtime_error("FPS mode is available in the standalone player");
    if (result.fps_ui_smoke && (!result.fps || result.scene_path.empty() || editor))
        throw std::runtime_error("--fps-ui-smoke requires a player, --fps and --scene");
    if (editor && result.smoke && result.smoke_output.empty())
        throw std::runtime_error("Editor smoke requires --smoke-output FILE");
    return result;
}

// Two existing paths may differ lexically yet resolve to the same assets
// directory (relative paths, Windows case, junctions or symlinks). The VFS
// deliberately rejects duplicate mounts, so game systems must avoid them
// when an explicit --shared-assets root equals project/assets.
bool same_asset_directory(fs::path const& a,fs::path const& b) {
    if(a.empty() || b.empty())return false;
    std::error_code ec;
    bool same=fs::equivalent(a,b,ec);
    return !ec && same;
}

Scene read_document(fs::path const& path)
{
    AssetManager assets;
    auto const absolute = fs::absolute(path);
    assets.mount_directory("", absolute.parent_path());
    return *assets.request_scene(absolute.filename().u8string()).get();
}


std::vector<std::string> script_args(std::string_view json) {
    if (json.empty() || json.front()!='[' ||
        json.size()>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
        throw std::invalid_argument("Invalid engine API argument array");
    boost::property_tree::ptree root;
    std::istringstream in{std::string(json)};
    boost::property_tree::read_json(in,root);
    std::vector<std::string> result;
    for (auto const& entry : root) {
        if (!entry.first.empty() || result.size()>32)
            throw std::invalid_argument("Invalid engine API argument");
        result.push_back(entry.second.get_value<std::string>());
    }
    return result;
}
std::size_t script_index(std::string const& value) {
    if (value.empty() || std::any_of(value.begin(),value.end(),
        [](unsigned char ch){return ch<'0' || ch>'9';}))
        throw std::invalid_argument("Script entity index must be unsigned decimal");
    std::size_t end=0;
    auto const index=std::stoull(value,&end);
    if (end!=value.size()) throw std::invalid_argument("Invalid index");
    return static_cast<std::size_t>(index);
}
double script_number(std::string const& value) {
    std::size_t end=0;
    auto number=std::stod(value,&end);
    if (end!=value.size() || !std::isfinite(number))
        throw std::invalid_argument("Invalid scripting number");
    return number;
}

// Projects may place their startup scene in a nested level/ directory.
// The package exporter flattens the startup scene into game/main.nscn (legacy releases may use .nscene).
// Validate VFS paths even when scripts are inside a FAT/DB archive.
void validate_script_virtual_path(fs::path const& virtual_path) {
    auto const name=virtual_path.generic_u8string();
    if (name.rfind("scripts/",0)!=0 || name.find(':')!=std::string::npos ||
        name.find('\\')!=std::string::npos ||
        (virtual_path.extension()!=".js" && virtual_path.extension()!=".mjs"))
        throw std::runtime_error("Invalid project script virtual path");
    for (auto const& part:virtual_path)
        if (part.empty() || part=="." || part==".." || part.is_absolute())
            throw std::runtime_error("Script path must remain in project scripts/");
}

fs::path validated_script_file(fs::path const& root,fs::path const& virtual_path) {
    validate_script_virtual_path(virtual_path);
    auto cursor=fs::absolute(root);
    for (auto const& part:virtual_path) {
        if (part.empty() || part=="." || part==".." || part.is_absolute())
            throw std::runtime_error("Script path must remain in project scripts/");
        cursor/=part;
        if (fs::is_symlink(cursor))
            throw std::runtime_error("Script path cannot contain redirected components");
    }
    auto canonical_root=fs::weakly_canonical(root);
    auto canonical_file=fs::weakly_canonical(cursor);
    auto inside=canonical_file.lexically_relative(canonical_root);
    if (inside.empty() || inside.is_absolute() ||
        *inside.begin()==fs::path("..") || !fs::is_regular_file(cursor))
        throw std::runtime_error("Script path escapes project root or file is missing");
    if (fs::file_size(cursor)>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
        throw std::runtime_error("Script source exceeds 1 MiB");
    return cursor;
}

fs::path script_project_root(fs::path const& scene_path) {
    auto const fallback=fs::absolute(scene_path).parent_path();
    auto cursor=fallback;
    for (unsigned depth=0;depth<8;++depth) {
        if (fs::is_regular_file(cursor/"northstarcpp.project.json"))
            return cursor;
        if (cursor==cursor.root_path() || cursor==cursor.parent_path())
            break;
        cursor=cursor.parent_path();
    }
    return fallback;
}

// Game saves belong to the current Windows user, never to an exported
// read-only game package or to the source tree. Source/release names match.
SaveGameStore project_save_store(Options const& config, fs::path const& scene_path) {
    auto const source = config.packaged_game
        ? fs::absolute(scene_path).parent_path().parent_path()/"manifest.json"
        : script_project_root(scene_path)/"northstarcpp.project.json";
    std::string game_name = scene_path.stem().u8string();
    if (fs::is_regular_file(source) && !fs::is_symlink(source) &&
        fs::file_size(source) <= 128u*1024u) {
        boost::property_tree::ptree meta;
        std::ifstream in(source, std::ios::binary);
        if (in) {
            try {
                boost::property_tree::read_json(in,meta);
                auto const key = config.packaged_game ? "game_name" : "name";
                auto name = meta.get<std::string>(key,"");
                if (!name.empty() && name.size() <= 128)
                    game_name = std::move(name);
            } catch (boost::property_tree::json_parser_error const&) {
                // Old projects without metadata retain the scene name.
            }
        }
    }
    std::uint64_t hash=14695981039346656037ull;
    std::string safe;
    for (unsigned char ch : game_name) {
        hash=(hash^ch)*1099511628211ull;
        if (safe.size()<40) {
            if ((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') ||
                (ch>='0' && ch<='9') || ch=='-' || ch=='_')
                safe.push_back(static_cast<char>(ch));
            else if (ch==' ' && !safe.empty() && safe.back()!='-')
                safe.push_back('-');
        }
    }
    if (safe.empty()) safe="project";
    std::ostringstream namespace_name;
    namespace_name << safe << '-' << std::hex << std::setw(16)
                   << std::setfill('0') << hash;
    // Deliberately fail save APIs instead of silently writing into CWD.
    std::wstring appdata(32768,L'\0');
    DWORD count=GetEnvironmentVariableW(L"LOCALAPPDATA",appdata.data(),
                                         static_cast<DWORD>(appdata.size()));
    if (!count || count>=appdata.size())
        throw std::runtime_error("LOCALAPPDATA is required for game saves");
    appdata.resize(count);
    return SaveGameStore(fs::path(appdata)/L"Kaylas Systems"/L"NorthStarCpp"/
                         L"Saves"/fs::u8path(namespace_name.str()));
}

std::string script_json_quote(std::string_view input) {
    static constexpr char hex[]="0123456789abcdef";
    std::string result(1,'"');
    for (unsigned char c:input) {
        if (c=='"' || c=='\\') {result+='\\';result+=static_cast<char>(c);}
        else if (c<32) {result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
        else result+=static_cast<char>(c);
    }
    result+='"';
    return result;
}
std::string script_object_payload(SceneObject const& obj,std::size_t index,float dt=-1.f) {
    std::string payload="{\"id\":" + script_json_quote(std::to_string(obj.node.id)) +
        ",\"index\":" + std::to_string(index) + ",\"script\":" +
        script_json_quote(obj.node.script);
    if (dt>=0.f) payload+=",\"deltaTime\":" + std::to_string(dt);
    return payload+"}";
}

class App {
public:
    explicit App(Options settings) : config(std::move(settings))
    {
        file_path = config.scene_path;
        if (config.new_document && fs::exists(file_path))
            config.new_document = false;
        if (!file_path.empty() && !config.new_document)
            document.load(load_startup_scene());
        else if (!config.smoke || !editor)
            document.load(make_startup_scene());
        dirty = config.new_document;
        if (!file_path.empty() &&
            !config.smoke && !config.fps_ui_smoke && !config.check_scene) {
            auto const root = config.archive_path.empty()
                ? script_project_root(file_path) : fs::path{};
            if (config.archive_path.empty())
                scripted_game=fs::is_regular_file(root/"scripts"/"main.js");
            else {
                AssetStore archive_assets;
                archive_assets.mount_archive("",config.archive_path,
                    config.archive_data.empty() ? config.archive_path : config.archive_data);
                try {
                    auto const bytes=archive_assets.size("scripts/main.js");
                    if (bytes>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                        throw std::runtime_error("Archived main.js exceeds 1 MiB");
                    scripted_game=true;
                } catch (std::runtime_error const& e) {
                    if (std::string_view(e.what()).find("exceeds 1 MiB")!=std::string_view::npos)
                        throw;
                }
            }
            if (!scripted_game) {
                auto const& objects=document.snapshot().objects;
                scripted_game=std::any_of(objects.begin(),objects.end(),
                    [](SceneObject const& object) {return !object.node.script.empty();});
            }
            scripted_first_person=scripted_game && config.fps;
            if (scripted_game && !scripted_first_person && config.archive_path.empty()) {
                auto const manifest=root/"northstarcpp.project.json";
                if (fs::is_regular_file(manifest)) {
                    boost::property_tree::ptree metadata;
                    boost::property_tree::read_json(manifest.u8string(),metadata);
                    auto const mode=metadata.get<std::string>("game_mode","scene");
                    scripted_first_person=(mode=="fps" || mode=="explore");
                }
            }
        }
        if (!editor) {
            if (!config.abby && !config.smoke && !config.check_scene && !config.fps_ui_smoke &&
                config.archive_path.empty() && !file_path.empty()) {
                auto const root = script_project_root(file_path);
                auto const manifest = root / "northstarcpp.project.json";
                if (fs::is_regular_file(manifest)) {
                    boost::property_tree::ptree metadata;
                    boost::property_tree::read_json(manifest.u8string(), metadata);
                    config.abby = metadata.get<std::string>("player_character","") == "abby";
                }
            }
            if (config.abby && !config.fps && !scripted_first_person) {
                config.fps = true;
                config.explore = true;
            }
            if (config.abby && scripted_game) scripted_first_person = true;
            auto initial_scene = document.snapshot();
            if (config.fps) {
                // A FPS project can supply its data through engine.assets.
                // Both source projects and exported games contain <scene>/assets/.
                // Data is never read directly from disk by the gameplay subsystem.
                if (config.archive_path.empty() && !file_path.empty()) {
                    fs::path const asset_root = fs::absolute(file_path).parent_path() / "assets";
                    if (fs::exists(asset_root)) {
                        if (!fs::is_directory(asset_root))
                            throw std::runtime_error("FPS asset root is not a directory");
                        fps_assets = std::make_unique<AssetManager>();
                        fps_assets->mount_directory("", asset_root);
                        if (!scripted_game && !config.explore) {
                            auto tuning = fps_assets->request_fps_tuning("gameplay/fps.json");
                            fps_shot = fps_assets->request_audio_clip("audio/shot.wav", AssetPriority::background);
                            fps_reload = fps_assets->request_audio_clip("audio/reload.wav", AssetPriority::background);
                            fps_tuning = *tuning.get();
                        }
                        // Older FPS projects may omit camera.json; malformed present
                        // content remains a hard error, not an unnoticed fallback.
                        bool has_camera_asset = false;
                        try { (void)fps_assets->size("gameplay/camera.json"); has_camera_asset = true; }
                        catch (std::exception const&) {}
                        if (has_camera_asset)
                            fps_camera_settings = load_camera_settings(*fps_assets);
                    }
                }
                if (!scripted_first_person)
                    fps = std::make_unique<FpsArena>(initial_scene,
                        fps_tuning, config.explore);
            }
            preview = std::make_unique<PhysicsScene>(initial_scene);
            initialize_asset_zones();
            if (fps || scripted_first_person) {
                fps_camera = std::make_unique<CameraDirector>(fps_camera_settings);
                if (config.abby)
                    fps_camera->set_mode(CameraMode::third_person,0.f);
                if (fps) update_fps_camera(0.f, false);
                else update_script_camera(0.f);
            }
            update_asset_zones();
        }
        rebuild_scene_world();
        if (!editor && !config.smoke && !config.check_scene && !config.fps_ui_smoke &&
            !file_path.empty())
            initialize_project_scripts();
    }

    ~App() {
        stop_project_scripts();
    }

    Scene load_startup_scene() const
    {
        if (config.archive_path.empty())
            return read_document(file_path);
        AssetManager assets;
        assets.mount_archive("", config.archive_path,
            config.archive_data.empty() ? config.archive_path : config.archive_data);
        return *assets.request_scene(file_path.generic_u8string()).get();
    }

    int run(HINSTANCE instance)
    {
        WNDCLASSEXW type{};
        type.cbSize = sizeof(type);
        type.hInstance = instance;
        type.lpszClassName = viewport_class;
        type.lpfnWndProc = viewport_procedure;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_NORTHSTARCPP_APP));
        type.hIconSm = static_cast<HICON>(LoadImageW(instance,
            MAKEINTRESOURCEW(IDI_NORTHSTARCPP_APP), IMAGE_ICON, 16, 16, LR_SHARED));
        if (!RegisterClassExW(&type))
            throw std::runtime_error("Cannot register viewport window");

        type.lpszClassName = window_class;
        type.lpfnWndProc = procedure;
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        if (!RegisterClassExW(&type))
            throw std::runtime_error("Cannot register application window");
        window = CreateWindowExW(WS_EX_CONTROLPARENT, window_class,
            editor ? L"NorthStarCpp Editor" : L"NorthStarCpp Player",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
            1450, 870, nullptr, nullptr, instance, this);
        if (!window)
            throw std::runtime_error("Cannot create application window");

        try {
            input = std::make_unique<InputDevice>(reinterpret_cast<uintptr_t>(window));
            surface = child(viewport_class, L"", WS_CLIPSIBLINGS, 0);
            if (editor) {
                restore_workspace();
                create_editor_controls();
                CheckMenuItem(GetMenu(window),menu_legacy_content,
                    MF_BYCOMMAND|(content_visible?MF_CHECKED:MF_UNCHECKED));
                ACCEL table[]{
                    {static_cast<BYTE>(FVIRTKEY|FCONTROL), 'Z',menu_undo},
                    {static_cast<BYTE>(FVIRTKEY|FCONTROL), 'Y',menu_redo},
                    {static_cast<BYTE>(FVIRTKEY|FCONTROL), 'D',menu_duplicate},
                    {static_cast<BYTE>(FVIRTKEY|FCONTROL), 'S',save_scene_id},
                    {static_cast<BYTE>(FVIRTKEY|FCONTROL|FSHIFT), 'B',menu_show_content}
                };
                accelerators=CreateAcceleratorTableW(table,5);
                if (!accelerators)
                    throw std::runtime_error("Cannot create editor keyboard shortcuts");
            }
            if (fps)
                create_fps_menu();
            layout();
            graphics = std::make_unique<Viewport>(surface, config.warp);
            if (!file_path.empty()) {
                graphics->set_asset_sources(
                    config.archive_path.empty()?script_project_root(file_path):fs::path{},
                    config.archive_path,config.archive_data);
            }
            if (config.abby) load_abby();
            if (fps_assets) {
                bool skydome_available = false;
                try {
                    (void)fps_assets->size("models/skydome.ydd");
                    skydome_available = true;
                } catch (std::runtime_error const&) {
                    // Older FPS projects contain no authored sky.
                }
                if (skydome_available) {
                    auto sky = fps_assets->request_drawable_resources(
                        "models/skydome.ydd@skydome_high", AssetPriority::critical).get();
                    graphics->set_sky(*sky);
                }
            }
            refresh();
            ShowWindow(window, SW_SHOWNORMAL);
            UpdateWindow(window);
            if (!script_window_title.empty())
                SetWindowTextW(window,script_window_title.c_str());
            if (!editor && (fps || scripted_first_person)) {
                SetFocus(window);
                capture_fps_cursor();
                update_fps_title();
            }
            if (!SetTimer(window, frame_timer, 16, nullptr))
                throw std::runtime_error("Cannot start application frame timer");
            if (!editor && !scripted_game && !config.smoke &&
                !config.check_scene && !config.fps_ui_smoke)
                play_start_sound();
            if ((config.smoke || config.check_scene || config.fps_ui_smoke || config.abby_smoke) &&
                !PostMessageW(window, smoke_message, 0, 0))
                throw std::runtime_error("Cannot start application smoke test");
        } catch (...) {
            release_fps_cursor();
            graphics.reset();
            DestroyWindow(window);
            throw;
        }

        MSG message{};
        for (BOOL next; (next = GetMessageW(&message, nullptr, 0, 0)) != 0;) {
            if (next < 0)
                throw std::runtime_error("Application message loop failed");
            if (editor && accelerators &&
                TranslateAcceleratorW(window,accelerators,&message))
                continue;
            if (editor && IsDialogMessageW(window, &message))
                continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (accelerators)
            DestroyAcceleratorTable(accelerators);
        return static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK viewport_procedure(HWND handle,UINT msg,
                                                 WPARAM wp,LPARAM lp)
    {
        auto* parent=GetParent(handle);
        auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(parent,GWLP_USERDATA));
        if (!app)return DefWindowProcW(handle,msg,wp,lp);
        try {return app->viewport_message(handle,msg,wp,lp);}
        catch (std::exception const& error) {
            app->gizmo_dragging=false;
            app->gizmo_scene.reset();
            if(GetCapture()==handle)ReleaseCapture();
            app->report(error.what());
            return 0;
        }
    }

    using ScreenPoint=editor_tools::ScreenPoint;
    static float axis_dot(Vector3 a,Vector3 b) {
        return a.x*b.x+a.y*b.y+a.z*b.z;
    }
    static Vector3 axis_cross(Vector3 a,Vector3 b) {
        return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    }
    static Vector3 unit(Vector3 value) {
        float const magnitude=std::sqrt(axis_dot(value,value));
        if(magnitude<1e-6f)return {1.f,0.f,0.f};
        return {value.x/magnitude,value.y/magnitude,value.z/magnitude};
    }
    std::optional<ScreenPoint> project_world(Vector3 world,Vector3) const
    {
        RECT area{};GetClientRect(surface,&area);
        return editor_camera.project(world,static_cast<float>(area.right),
            static_cast<float>(area.bottom));
    }
    ViewportGizmo active_gizmo(Scene const& scene) const
    {
        ViewportGizmo gizmo;
        gizmo.mode=static_cast<unsigned>(gizmo_mode);
        auto const index=current_selection_index();
        if(index<scene.objects.size()) {
            auto const& at=scene.objects[index].position;
            auto const& eye=editor_camera.pose.position;
            float const dx=at.x-eye.x,dy=at.y-eye.y,dz=at.z-eye.z;
            float const distance=std::sqrt(dx*dx+dy*dy+dz*dz);
            gizmo.length=std::clamp(distance*.095f,.8f,3000.f);
        }
        gizmo.active_axis=gizmo_dragging?gizmo_drag_axis:-1;
        if(index<scene.objects.size())
            for(int axis=0;axis<3;++axis)
                gizmo.axes[axis]=editor_tools::gizmo_axis(scene.objects[index],
                    static_cast<editor_tools::GizmoAxis>(axis),gizmo_settings.local);
        return gizmo;
    }
    static float segment_distance(ScreenPoint p,ScreenPoint a,ScreenPoint b)
    {
        float const vx=b.x-a.x,vy=b.y-a.y;
        float const t=std::clamp(((p.x-a.x)*vx+(p.y-a.y)*vy)/
            std::max(.01f,vx*vx+vy*vy),0.f,1.f);
        float const x=p.x-(a.x+t*vx),y=p.y-(a.y+t*vy);
        return std::sqrt(x*x+y*y);
    }
    int pick_gizmo_axis(Scene const& scene,ScreenPoint mouse) const
    {
        auto const index=current_selection_index();
        if(index>=scene.objects.size())return -1;
        auto const center=scene.objects[index].position;
        auto origin=project_world(center,center);
        if(!origin)return -1;
        auto const handle_length=active_gizmo(scene).length;
        float closest=14.f;
        int result=-1;
        for(int axis=0;axis<3;++axis) {
            auto const direction=editor_tools::gizmo_axis(scene.objects[index],
                static_cast<editor_tools::GizmoAxis>(axis),gizmo_settings.local);
            float distance=100000.f;
            if(gizmo_mode==editor_tools::GizmoMode::rotate) {
                auto const u=unit(axis_cross(direction,
                    std::abs(direction.y)>.9f ? Vector3{0.f,0.f,1.f} :
                        Vector3{0.f,1.f,0.f}));
                auto const v=axis_cross(direction,u);
                constexpr float tau=6.283185307179586f;
                std::optional<ScreenPoint> last;
                for(int i=0;i<=40;++i) {
                    float const angle=tau*float(i)/40.f;
                    Vector3 const point{center.x+handle_length*.8333333f*(u.x*std::cos(angle)+v.x*std::sin(angle)),
                        center.y+handle_length*.8333333f*(u.y*std::cos(angle)+v.y*std::sin(angle)),
                        center.z+handle_length*.8333333f*(u.z*std::cos(angle)+v.z*std::sin(angle))};
                    auto screen=project_world(point,center);
                    if(last && screen)distance=std::min(distance,
                        segment_distance(mouse,*last,*screen));
                    last=screen;
                }
            } else {
                Vector3 const tip{center.x+direction.x*handle_length,
                    center.y+direction.y*handle_length,center.z+direction.z*handle_length};
                auto const projected=project_world(tip,center);
                if(projected)distance=segment_distance(mouse,*origin,*projected);
            }
            if(distance<closest) {closest=distance;result=axis;}
        }
        return result;
    }
    int pick_actor(Scene const& scene,ScreenPoint mouse) const
    {
        RECT area{};GetClientRect(surface,&area);
        auto const ray=editor_camera.screen_ray(mouse.x,mouse.y,
            static_cast<float>(area.right),static_cast<float>(area.bottom));
        auto const hit=editor_tools::pick_actor(scene,ray);
        return hit ? static_cast<int>(*hit) : -1;
    }
    LRESULT viewport_message(HWND handle,UINT msg,WPARAM wp,LPARAM lp)
    {
        if(!editor || !surface)return DefWindowProcW(handle,msg,wp,lp);
        if(msg==WM_SETCURSOR && LOWORD(lp)==HTCLIENT) {
            SetCursor(LoadCursorW(nullptr,gizmo_dragging?IDC_SIZEALL:IDC_CROSS));
            return TRUE;
        }
        if (msg==WM_MOUSEWHEEL && !preview) {
            float const steps=static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp))/
                static_cast<float>(WHEEL_DELTA);
            editor_camera.dolly(steps*std::max(0.4f,camera_move_speed));
            return 0;
        }
        if (msg==WM_RBUTTONDOWN && !preview) {
            SetFocus(handle);
            camera_looking=true;
            camera_pointer={float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))};
            SetCapture(handle);
            return 0;
        }
        if (msg==WM_MOUSEMOVE && camera_looking && (wp&MK_RBUTTON)) {
            ScreenPoint const pt{float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))};
            editor_camera.look(pt.x-camera_pointer.x,pt.y-camera_pointer.y);
            camera_pointer=pt;
            return 0;
        }
        if (msg==WM_RBUTTONUP && camera_looking) {
            camera_looking=false;
            if(GetCapture()==handle)ReleaseCapture();
            return 0;
        }
        if(msg==WM_KEYDOWN && !preview) {
            if(wp==VK_ESCAPE && gizmo_dragging) {
                gizmo_dragging=false;gizmo_scene.reset();
                if(GetCapture()==handle)ReleaseCapture();
                return 0;
            }
            if(wp=='F' && valid_selection()) {
                auto const object=document.object(selected);
                editor_camera.frame(object.position,
                    std::clamp(object.size*6.f,6.f,150.f));
                return 0;
            }
            if(!camera_looking && (wp=='W' || wp=='E' || wp=='R')) {
                gizmo_mode=wp=='W' ? editor_tools::GizmoMode::translate :
                    wp=='E' ? editor_tools::GizmoMode::rotate :
                              editor_tools::GizmoMode::scale;
                update_gizmo_buttons();
                return 0;
            }
        }
        if(msg==WM_CAPTURECHANGED) {
            camera_looking=false;
            if(gizmo_dragging) {
                gizmo_dragging=false;gizmo_scene.reset();
            }
            return 0;
        }
        if(msg==WM_LBUTTONDOWN && !preview) {
            SetFocus(handle);
            ScreenPoint const pt{float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))};
            auto const scene=document.snapshot();
            auto const hit=pick_gizmo_axis(scene,pt);
            if(hit>=0) {
                gizmo_baseline=scene;
                gizmo_drag_axis=hit;
                gizmo_start=pt;
                gizmo_dragging=true;
                gizmo_drag_amount=0.f;
                gizmo_scene.reset();
                SetCapture(handle);
                return 0;
            }
            auto const actor=pick_actor(scene,pt);
            if(actor>=0) {
                select_index(static_cast<std::size_t>(actor));
                refresh();
            }
            return 0;
        }
        if(msg==WM_MOUSEMOVE && gizmo_dragging && (wp&MK_LBUTTON)) {
            auto const idx=current_selection_index();
            if(idx>=gizmo_baseline.objects.size())return 0;
            ScreenPoint const pt{float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))};
            auto const& actor=gizmo_baseline.objects[idx];
            auto const direction=editor_tools::gizmo_axis(actor,
                static_cast<editor_tools::GizmoAxis>(gizmo_drag_axis),
                gizmo_settings.local);
            auto const origin=project_world(actor.position,actor.position);
            auto const handle_length=active_gizmo(gizmo_baseline).length;
            Vector3 const endpoint{actor.position.x+direction.x*handle_length,
                actor.position.y+direction.y*handle_length,
                actor.position.z+direction.z*handle_length};
            auto const tip=project_world(endpoint,actor.position);
            if(!origin || !tip)return 0;
            if(gizmo_mode==editor_tools::GizmoMode::rotate) {
                constexpr float to_degrees=57.29577951308232f;
                float const start=std::atan2(gizmo_start.y-origin->y,gizmo_start.x-origin->x);
                float const now=std::atan2(pt.y-origin->y,pt.x-origin->x);
                float degrees=(now-start)*to_degrees;
                if(degrees>180.f)degrees-=360.f;
                if(degrees< -180.f)degrees+=360.f;
                gizmo_drag_amount=degrees;
            } else {
                float const vx=tip->x-origin->x,vy=tip->y-origin->y;
                float const square=std::max(64.f,vx*vx+vy*vy);
                float const offset=((pt.x-gizmo_start.x)*vx+
                    (pt.y-gizmo_start.y)*vy)/square*handle_length;
                gizmo_drag_amount=gizmo_mode==editor_tools::GizmoMode::scale ?
                    offset*.35f : offset;
            }
            gizmo_scene=gizmo_baseline;
            editor_tools::apply_gizmo(*gizmo_scene,idx,gizmo_mode,
                static_cast<editor_tools::GizmoAxis>(gizmo_drag_axis),
                gizmo_drag_amount,gizmo_settings);
            return 0;
        }
        if(msg==WM_LBUTTONUP && gizmo_dragging) {
            gizmo_dragging=false;
            if(GetCapture()==handle)ReleaseCapture();
            if(gizmo_scene && gizmo_drag_amount!=0.f) {
                auto scene=std::move(*gizmo_scene);
                gizmo_scene.reset();
                commit_scene(std::move(scene),current_selection_index());
            } else gizmo_scene.reset();
            return 0;
        }
        return DefWindowProcW(handle,msg,wp,lp);
    }

    static LRESULT CALLBACK procedure(HWND handle, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        if (msg == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (!app)
            return DefWindowProcW(handle, msg, wparam, lparam);
        try {
            return app->message(handle, msg, wparam, lparam);
        } catch (std::exception const& error) {
            if ((msg == WM_COMMAND || msg == WM_CLOSE) && !app->config.smoke) {
                app->report(error.what());
                return 0;
            }
            app->report(error.what());
            app->exit_status = 1;
            DestroyWindow(handle);
            return 0;
        }
    }

    void initialize_asset_zones()
    {
        auto manifest = config.asset_zones_path;
        if (manifest.empty() && config.archive_path.empty() && !file_path.empty()) {
            auto const candidate = fs::absolute(file_path).parent_path() / "assets/streaming/zones.json";
            if (fs::is_regular_file(candidate))
                manifest = candidate;
        }
        if (manifest.empty())
            return;

        manifest = fs::absolute(manifest);
        zone_assets = std::make_unique<AssetManager>();
        auto root = config.shared_asset_root;
        if (root.empty()) {
            std::array<wchar_t, 32768> executable{};
            auto const length = GetModuleFileNameW(nullptr, executable.data(),
                static_cast<DWORD>(executable.size()));
            if (length > 0 && length < executable.size()) {
                for (auto parent = fs::path(executable.data()).parent_path(); !parent.empty();) {
                    auto const candidate = parent / "sharedAssets";
                    if (fs::is_directory(candidate)) {
                        root = candidate;
                        break;
                    }
                    auto const next = parent.parent_path();
                    if (next == parent)
                        break;
                    parent = next;
                }
            }
        }
        auto const project = (!file_path.empty() && config.archive_path.empty())
            ? fs::absolute(file_path).parent_path() / "assets" : fs::path{};
        // The project root wins if both configuration slots alias the
        // same directory. Mount once, not at two different priorities.
        if (!root.empty() && !same_asset_directory(root,project))
            zone_assets->mount_shared_assets(root);
        if (!project.empty()) {
            if (fs::is_directory(project))
                zone_assets->mount_directory("", project);
        } else if (!config.archive_path.empty()) {
            zone_assets->mount_archive("", config.archive_path,
                config.archive_data.empty() ? config.archive_path : config.archive_data);
        }
        zone_assets->mount_directory("streaming-manifest", manifest.parent_path());
        auto const source = zone_assets->request_bytes(
            "streaming-manifest/" + manifest.filename().u8string(), AssetPriority::critical).get();
        asset_zones = std::make_unique<AssetZones>(*zone_assets,
            parse_asset_zones(std::string(source->begin(), source->end())));
    }

    // Scene v3 streaming is an engine-owned visibility stage. It preserves
    // original entity indices, so Bullet/Jolt physics and Flecs ABI remain
    // independent from render culling. Collision eviction is NOT performed here.
    void rebuild_scene_world()
    {
        // Destroy old asynchronous readers before changing VFS mounts.
        scene_cell_residency.reset();
        auto snapshot = document.snapshot();
        if (snapshot.version == 3) {
            scene_world = std::make_unique<SceneWorld>(std::move(snapshot));
            navigation_cells = std::make_unique<SceneNavigationCells>(
                scene_world->source());
            stream_previous_valid = false;
            setup_scene_asset_streaming();
            if ((!editor || !dirty) && !file_path.empty()) {
                try {
                    bool v2=false;
                    if (!config.archive_path.empty() && scene_assets) {
                        auto bytes=scene_assets->read_range(file_path.generic_u8string(),0,16);
                        v2=bytes.size()==16 && bytes[0]=='N' && bytes[1]=='E' &&
                           bytes[2]=='F' && bytes[3]=='8' && bytes[10]==2 &&
                           bytes[12]==2;
                        if(v2)
                            scene_cell_residency=std::make_unique<SceneCellAsyncResidency>(
                                *scene_assets,file_path.generic_u8string());
                    } else if (fs::is_regular_file(file_path)) {
                        std::ifstream source(file_path,std::ios::binary);
                        std::array<char,16> signature{};
                        source.read(signature.data(),signature.size());
                        v2=source.gcount()==16 && signature[0]=='N' &&
                           signature[1]=='E' && signature[2]=='F' &&
                           signature[3]=='8' && signature[10]==2 &&
                           signature[12]==2;
                        if(v2)
                            scene_cell_residency=std::make_unique<SceneCellAsyncResidency>(
                                file_path);
                    }
                } catch(std::exception const& error) {
                    OutputDebugStringA(error.what());
                }
            }
            if(scene_cell_residency)
                scene_world->enable_payload_gate(true);
            update_scene_world(0.f);
        } else {
            scene_cell_residency.reset();
            navigation_cells.reset();
            scene_world.reset();
            scene_asset_requests.clear();
            scene_asset_completed.clear();
            scene_asset_failed.clear();
            scene_assets.reset();
            scene_asset_source.clear();
        }
    }

    void setup_scene_asset_streaming()
    {
        if (!scene_world || file_path.empty())
            return;
        auto const source = fs::absolute(file_path).lexically_normal();
        if (scene_assets && scene_asset_source == source)
            return;

        scene_asset_requests.clear();
        scene_asset_completed.clear();
        scene_asset_failed.clear();
        scene_assets.reset();
        scene_asset_source.clear();
        auto store = std::make_unique<AssetManager>();
        bool mounted=false;
        fs::path mounted_project;

        if (!config.archive_path.empty()) {
            store->mount_archive("",config.archive_path,
                config.archive_data.empty()?config.archive_path:config.archive_data);
            mounted=true;
        } else {
            auto const project = source.parent_path() / "assets";
            if (fs::is_directory(project)) {
                store->mount_directory("",project);
                mounted_project=project;
                mounted=true;
            }
        }

        auto common = config.shared_asset_root;
        if (common.empty()) {
            // Same sharedAssets discovery as the standard Asset Manager.
            for (auto parent=source.parent_path();!parent.empty();) {
                auto candidate=parent/"sharedAssets";
                if (fs::is_directory(candidate)) {common=candidate;break;}
                auto next=parent.parent_path();
                if (next==parent) break;
                parent=next;
            }
        }
        if (!common.empty() && fs::is_directory(common) &&
            !same_asset_directory(common,mounted_project)) {
            store->mount_shared_assets(common);
            mounted=true;
        }
        if (mounted) {
            store->set_streaming_budget(256ull*1024*1024);
            scene_asset_source=source;
            scene_assets=std::move(store);
        }
    }

    void prefetch_scene_assets()
    {
        if (!scene_assets || !scene_world)
            return;

        // Bound GPU/source residency pressure and avoid duplicate queued work.
        // A request is retained only while its cell belongs to the resident set.
        constexpr std::size_t request_limit=32;
        auto needed=scene_world->asset_dependencies();
        if (needed.size()>request_limit)
            needed.resize(request_limit);
        std::unordered_set<std::string> selected(needed.begin(),needed.end());
        for (auto it=scene_asset_requests.begin();it!=scene_asset_requests.end();) {
            if (selected.count(it->first)==0) {
                scene_asset_completed.erase(it->first);
                it=scene_asset_requests.erase(it);
            } else ++it;
        }
        for (auto const& path:needed) {
            if (scene_asset_failed.count(path)!=0)
                continue;
            if (scene_asset_requests.count(path)==0) {
                auto priority=AssetPriority::normal;
                if (scene_asset_requests.size()<8)
                    priority=AssetPriority::critical;
                scene_asset_requests.emplace(path,
                    scene_assets->request_drawable_resources(path,priority));
            }
        }
        for (auto it=scene_asset_requests.begin();it!=scene_asset_requests.end();) {
            if (!it->second.ready() || scene_asset_completed.count(it->first)) {
                ++it;
                continue;
            }
            try {
                (void)it->second.get();
                scene_asset_completed.insert(it->first);
                ++it;
            } catch (std::exception const& error) {
                scene_asset_failed.insert(it->first);
                OutputDebugStringA(error.what());
                it=scene_asset_requests.erase(it);
            }
        }
    }

    void update_scene_world(float seconds)
    {
        if (!scene_world)
            return;
        Vector3 position{0.f,0.f,0.f};
        std::string room;
        auto const scene = preview ? preview->scene() : document.snapshot();
        for (auto const& object : scene.objects)
            if (object.player) {
                position = object.position;
                room = object.node.room;
                break;
            }
        if (fps_camera)
            position = fps_pose.position;
        else if (editor && !preview && valid_selection()) {
            // In authoring mode stream the sector under the inspected actor,
            // rather than keeping the world locked around the player spawn.
            position=document.object(selected).position;
            room=document.object(selected).node.room;
        }
        Vector3 velocity{0.f,0.f,0.f};
        if (stream_previous_valid && seconds >= 0.0001f &&
            std::isfinite(seconds)) {
            auto const inverse=1.f / seconds;
            velocity.x=std::clamp((position.x - stream_previous.x)*inverse,-5000.f,5000.f);
            velocity.y=std::clamp((position.y - stream_previous.y)*inverse,-5000.f,5000.f);
            velocity.z=std::clamp((position.z - stream_previous.z)*inverse,-5000.f,5000.f);
        }
        scene_world->update(position,velocity,room,&scene);
        if (scene_cell_residency) {
            scene_cell_residency->apply(*scene_world);
            // A WorldPartition transition is only an I/O request. Static ECS,
            // collision and visual residency may activate on the next frame
            // once the NEF8 chunk is actually decoded and verified.
            for (auto const& cell:scene_world->source().cells) {
                bool const ready=!scene_cell_residency->has_chunk(cell.name) ||
                    scene_cell_residency->loaded(cell.name);
                if (scene_world->cell_payload_ready(cell.name)!=ready)
                    scene_world->set_cell_payload_ready(cell.name,ready);
            }
        }
        if(navigation_cells)
            navigation_cells->sync(*scene_world);
        if (preview)
            (void)preview->sync_scene_residency(*scene_world,position);
        prefetch_scene_assets();
        stream_previous=position;
        stream_previous_valid=true;
    }

    void inspect_scene_streaming()
    {
        if (!scene_world) {
            MessageBoxW(window,L"Enable Scene V3 to use World Streaming diagnostics.",
                        L"NorthStarCpp Streaming Inspector",MB_OK|MB_ICONINFORMATION);
            return;
        }
        auto const& data=scene_world->streaming_diagnostics();
        std::wstring report =
            L"World Partition / Scene V3\n"
            L"---------------------------\n"
            L"Cells loaded: " + std::to_wstring(data.loaded_cells) + L"/" +
                std::to_wstring(scene_world->source().cells.size()) +
            L"\nPending loads: " + std::to_wstring(data.pending_loads) +
            L"\nPending unloads: " + std::to_wstring(data.pending_unloads) +
            L"\nResident entities: " + std::to_wstring(data.resident_entities) +
            L"\nVisible entities: " + std::to_wstring(data.visible_entities) +
            L"\nUpdate generation: " + std::to_wstring(data.generation) +
            L"\nAsync scene blocks pending: " +
                 std::to_wstring(scene_cell_residency ? scene_cell_residency->pending_cell_count() : 0) +
            L"\nDecoded scene blocks: " +
                 std::to_wstring(scene_cell_residency ? scene_cell_residency->resident_cell_count() : 0) +
            L"\nDrawable requests: " + std::to_wstring(scene_asset_requests.size()) +
            L"\nNavigation cells (coarse): " +
                std::to_wstring(navigation_cells ? navigation_cells->resident_count() : 0) +
            L"\nResolved assets: " + std::to_wstring(scene_asset_completed.size()) +
            L"\nFailed assets: " + std::to_wstring(scene_asset_failed.size()) +
            L"\n\nLast cell transitions:\n";
        auto const& transitions=scene_world->cell_transitions();
        if (transitions.empty())
            report+=L"  (none)\n";
        else {
            auto const count=std::min<std::size_t>(transitions.size(),24);
            for (std::size_t i=0;i<count;++i) {
                auto const& transition=transitions[i];
                report+=L"  " + std::wstring(transition.loaded?L"[IN] ":L"[OUT] ") +
                    to_wide(transition.name) +
                    (transition.forced?L" (pinned)\n":L"\n");
            }
            if (transitions.size()>count)
                report+=L"  ... and " + std::to_wstring(transitions.size()-count) +
                    L" more\n";
        }
        report+=L"\nHLOD clusters: " +
            std::to_wstring(scene_world->hlod_cluster_count()) +
            L"\nActive HLOD proxies: " +
            std::to_wstring(scene_world->hlod_proxies().size()) + L"\n";
        if (preview)
            report+=L"Physics residency extension: " +
                std::wstring(preview->supports_scene_residency() ? L"supported" :
                                                           L"provider fallback") + L"\n";
        report+=L"\nThis diagnostic reports logical residency and render visibility. "
                L"It does not assert GPU or physics resource eviction.";
        MessageBoxW(window,report.c_str(),L"NorthStarCpp Streaming Inspector",
                    MB_OK|MB_ICONINFORMATION);
    }

    void update_asset_zones()
    {
        if (!asset_zones)
            return;
        Vector3 position{0.f, 0.f, 0.f};
        if (fps_camera) {
            position = fps_pose.position;
        } else {
            for (auto const& object : preview->scene().objects)
                if (object.player) {
                    position = object.position;
                    break;
                }
        }
        asset_zones->update({position.x, position.y, position.z});
    }

    void update_fps_title()
    {
        if (!fps) return;
        if (config.explore) {
            SetWindowTextW(window,
                fps_menu_visible ? L"NorthStar First Person Platform | PAUSED"
                : L"NorthStar First Person Platform | WASD Move | Mouse Look | Space Jump | E Push");
            return;
        }
        std::wstring title = L"NorthStar FPS Arena | " +
            std::to_wstring(fps->hits()) + L"/" + std::to_wstring(fps->targets()) +
            L" hits | HP " + std::to_wstring(fps->health()) +
            L" | Ammo " + std::to_wstring(fps->ammunition()) + L"/" +
            std::to_wstring(fps->reserve_ammunition()) +
            (fps->reloading() ? L" | RELOADING" : L"") +
            (fps_menu_visible ? (fps->alive() ? L" | PAUSED" : L" | GAME OVER") : L"");
        SetWindowTextW(window, title.c_str());
    }

    void create_fps_menu()
    {
        fps_caption = child(L"STATIC", L"NORTHSTAR FPS  /  PAUSED",
                            SS_CENTER, 0);
        wchar_t const* labels[]{L"RESUME", L"RESTART", L"EXIT GAME"};
        for (int i = 0; i < 3; ++i) {
            fps_buttons[i] = child(L"BUTTON", labels[i],
                BS_PUSHBUTTON | WS_TABSTOP, fps_resume + i);
            ShowWindow(fps_buttons[i], SW_HIDE);
        }
        ShowWindow(fps_caption, SW_HIDE);
    }

    void show_fps_menu(bool show)
    {
        if (!fps || !fps_caption || fps_menu_visible == show) return;
        fps_menu_visible = show;
        if (show) release_fps_cursor();
        SetWindowTextW(fps_caption,
            !fps->alive() ? L"GAME OVER  /  RESTART" :
            fps->won() ? L"ARENA COMPLETE  /  RESTART" :
            config.explore ? L"FIRST PERSON PLATFORM  /  PAUSED" : L"NORTHSTAR FPS  /  PAUSED");
        ShowWindow(fps_caption, show ? SW_SHOW : SW_HIDE);
        for (HWND button : fps_buttons)
            ShowWindow(button, show ? SW_SHOW : SW_HIDE);
        if (show && (!fps->alive() || fps->won()))
            EnableWindow(fps_buttons[0], FALSE);
        else
            EnableWindow(fps_buttons[0], TRUE);
        if (show) {
            for (auto* button : fps_buttons)
                SetWindowPos(button, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        if (GetForegroundWindow() == window) {
            SetFocus(window);
            if (!show) capture_fps_cursor();
        }
        frame_clock.start();
        update_fps_title();
    }

    void load_abby()
    {
        AssetStore assets;
        auto const project = file_path.empty() ? fs::path{} : script_project_root(file_path);
        if (!project.empty() && fs::is_directory(project/"assets"/"models"/"abby")) {
            assets.mount_directory("",project/"assets");
            graphics->load_character(assets,"");
            return;
        }
        auto common = config.shared_asset_root;
        if (common.empty()) {
            for (auto parent = project; !parent.empty(); parent = parent.parent_path()) {
                if (fs::is_regular_file(parent/"sharedAssets/models/abby/abby.ydd")) {
                    common = parent/"sharedAssets";
                    break;
                }
                if (parent == parent.parent_path()) break;
            }
        }
        if (common.empty()) {
            std::array<wchar_t,32768> executable{};
            auto const length = GetModuleFileNameW(nullptr,executable.data(),
                static_cast<DWORD>(executable.size()));
            if (length && length < executable.size())
                common = fs::path(executable.data()).parent_path().parent_path()/"sharedAssets";
        }
        if (!fs::is_directory(common))
            throw std::runtime_error("Cannot locate Abby shared assets");
        assets.mount_directory("",common);
        graphics->load_character(assets,"");
    }

    void update_abby(Scene const& scene, float yaw, float dt)
    {
        if (!config.abby) return;
        if (fps_camera && fps_camera->mode() == CameraMode::first_person) {
            graphics->hide_character();
            return;
        }
        auto const player = std::find_if(scene.objects.begin(),scene.objects.end(),
            [](auto const& object) { return object.player; });
        if (player == scene.objects.end())
            throw std::runtime_error("An Abby scene must have a player spawn");
        auto const foot = player->position;
        bool moving = false, airborne = false;
        if (abby_previous && dt > 0.f) {
            auto const dx = foot.x - abby_previous->x, dz = foot.z - abby_previous->z;
            moving = std::hypot(dx,dz) > dt * .15f;
            airborne = std::abs(foot.y - abby_previous->y) > dt * .35f;
        }
        abby_previous = foot;
        graphics->pose_character(foot,yaw,dt,moving,airborne);
    }

    void update_fps_camera(float dt, bool aiming)
    {
        if (!fps_camera || !fps || !preview) return;
        fps_camera->set_zoom(aiming ? 1.f : 0.f);
        CameraTarget target;
        target.eye = fps->eye(preview->scene());
        target.yaw = fps->yaw();
        target.pitch = fps->pitch();
        target.heading = target.yaw;
        target.ignore_object = fps->player_index();
        fps_pose = fps_camera->update(dt, target, &preview->scene(), preview.get());
    }

    void update_script_camera(float dt) {
        if (!scripted_first_person || !preview || !fps_camera) return;
        auto const& scene=preview->scene();
        auto it=std::find_if(scene.objects.begin(),scene.objects.end(),
            [](SceneObject const& obj) {return obj.player;});
        if (it==scene.objects.end()) return;
        auto index=static_cast<std::size_t>(it-scene.objects.begin());
        CameraTarget target{};
        target.eye=it->position;
        target.eye.y+=script_eye_height;
        target.yaw=script_yaw;
        target.pitch=script_pitch;
        target.heading=target.yaw;
        target.ignore_object=index;
        fps_pose=fps_camera->update(dt,target,&scene,preview.get());
    }

    void restart_fps()
    {
        if (!fps) return;
        Scene const initial = document.snapshot();
        auto reset_physics = std::make_unique<PhysicsScene>(initial);
        auto reset_game = std::make_unique<FpsArena>(initial,
            fps_tuning, config.explore);
        preview = std::move(reset_physics);
        fps = std::move(reset_game);
        if (fps_camera) fps_camera->reset();
        update_fps_camera(0.f, false);
        fps_previous_jump = fps_previous_reload = fps_previous_menu = false;
        explore_previous_use = false;
        fps_menu_visible = true;
        show_fps_menu(false);
        update_fps_title();
    }

    void capture_fps_cursor()
    {
        if (editor || (!fps && !scripted_first_person) || fps_captured ||
            fps_menu_visible || script_paused || !window ||
            GetForegroundWindow() != window) return;
        RECT client{};
        GetClientRect(window, &client);
        POINT top{client.left, client.top}, bottom{client.right, client.bottom};
        ClientToScreen(window, &top);
        ClientToScreen(window, &bottom);
        RECT bounds{top.x, top.y, bottom.x, bottom.y};
        if (!ClipCursor(&bounds)) return;
        SetCapture(window);
        ShowCursor(FALSE);
        fps_captured = true;
    }

    void release_fps_cursor()
    {
        if (!fps_captured) return;
        ClipCursor(nullptr);
        if (GetCapture() == window) ReleaseCapture();
        ShowCursor(TRUE);
        fps_captured = false;
    }

    LRESULT message(HWND handle, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        if (input)
            input->feed_message(msg, static_cast<uintptr_t>(wparam), static_cast<intptr_t>(lparam));
        switch (msg) {
        case WM_ACTIVATEAPP:
            if (fps || scripted_first_person) {
                if (!wparam) release_fps_cursor();
                else if (!fps_menu_visible) capture_fps_cursor();
            }
            break;
        case WM_KILLFOCUS:
            if (fps || scripted_first_person) release_fps_cursor();
            break;
        case WM_SETFOCUS:
            if (fps || scripted_first_person) capture_fps_cursor();
            break;
        case WM_GETMINMAXINFO: {
            auto* bounds = reinterpret_cast<MINMAXINFO*>(lparam);
            bounds->ptMinTrackSize.x = editor ? 1060 : 480;
            bounds->ptMinTrackSize.y = editor ? 610 : 460;
            return 0;
        }
        case WM_SIZE:
            if (surface && wparam != SIZE_MINIMIZED)
                layout();
            if ((fps || scripted_first_person) && fps_captured) {
                release_fps_cursor();
                capture_fps_cursor();
            }
            return 0;
        case WM_NOTIFY:
            if (editor && content_browser &&
                content_browser->on_notify(reinterpret_cast<NMHDR const*>(lparam)))
                return 0;
            break;
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        case WM_SETCURSOR:
            if (editor && content_browser && content_browser->dragging()) {
                if (msg==WM_MOUSEMOVE || msg==WM_SETCURSOR) {
                    SetCursor(LoadCursorW(nullptr,IDC_HAND));
                    return TRUE;
                }
                if (msg==WM_LBUTTONUP) {
                    auto dragged=content_browser->finish_drag();
                    if(dragged && !preview) {
                        POINT pt{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
                        drop_project_asset(*dragged,pt);
                    }
                    return 0;
                }
            }
            if (editor && handle_splitter(msg,wparam,lparam))
                return TRUE;
            break;
        case WM_COMMAND:
            if (editor) {
                if (content_browser &&
                    content_browser->on_command(LOWORD(wparam),HIWORD(wparam)))
                    return 0;
                command(LOWORD(wparam), HIWORD(wparam));
                return 0;
            }
            if (fps && HIWORD(wparam) == BN_CLICKED) {
                switch (LOWORD(wparam)) {
                case fps_resume:
                    if (fps->alive() && !fps->won()) show_fps_menu(false);
                    return 0;
                case fps_restart:
                    restart_fps();
                    return 0;
                case fps_exit:
                    DestroyWindow(window);
                    return 0;
                }
            }
            break;
        case WM_APP+27:
            if (scripted_game && preview && !editor) {
                stop_project_scripts();
                preview=std::make_unique<PhysicsScene>(document.snapshot());
                script_yaw=script_pitch=0.f;
                if(fps_camera) fps_camera->reset();
                update_script_camera(0.f);
                initialize_project_scripts();
                capture_fps_cursor();
            }
            return 0;
        case WM_TIMER:
            if (editor && content_browser)
                content_browser->poll_thumbnails();
            if (wparam == frame_timer && !config.smoke && !config.check_scene && !config.abby_smoke && graphics && !IsIconic(window))
                frame();
            return 0;
        case smoke_message:
            if (config.abby_smoke) {
                auto const& scene = preview->scene();
                auto const player = std::find_if(scene.objects.begin(),scene.objects.end(),
                    [](auto const& object) { return object.player; });
                if (player == scene.objects.end()) throw std::runtime_error("Missing Abby spawn");
                auto const* camera = fps_camera ? &fps_pose : nullptr;
                for (unsigned i = 0; i < 8; ++i) {
                    graphics->pose_character(player->position,0.f,0.05f,i < 4,i >= 4);
                    graphics->draw(scene,-2,false,preview.get(),nullptr,false,camera,
                        scene_world.get(),false);
                }
                std::puts("Abby textured skeletal draw: PASS");
            } else if (config.fps_ui_smoke) {
                if (!fps || !graphics || !fps_caption || !fps_buttons[0])
                    throw std::runtime_error("FPS menu was not initialized");
                show_fps_menu(true);
                if (!fps_menu_visible)
                    throw std::runtime_error("FPS menu did not pause");
                Scene visual = preview->scene();
                fps->tint(visual);
                graphics->draw(visual,-2,true,preview.get(),fps.get(),true);
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(fps_resume,BN_CLICKED),
                             reinterpret_cast<LPARAM>(fps_buttons[0]));
                if (fps_menu_visible)
                    throw std::runtime_error("FPS Resume did not unpause");
                show_fps_menu(true);
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(fps_restart,BN_CLICKED),
                             reinterpret_cast<LPARAM>(fps_buttons[1]));
                if (fps_menu_visible || !fps->alive() || fps->ammunition() != fps->magazine_size())
                    throw std::runtime_error("FPS Restart did not reset state");
            } else if (config.check_scene) {
                if (asset_zones)
                    asset_zones->wait();
                if (!preview || !graphics)
                    throw std::runtime_error("Cannot initialize scene preview");
                if (fps) {
                    Scene visual = preview->scene();
                    fps->tint(visual);
                    graphics->draw(visual, -2, true, preview.get(), fps.get());
                } else {
                    graphics->draw(preview->scene(), -2, true, preview.get());
                }
            } else {
                smoke();
            }
            DestroyWindow(handle);
            return 0;
        case WM_CLOSE:
            if (!editor || confirm_discard()) {
                if(editor)persist_workspace();
                DestroyWindow(handle);
            }
            return 0;
        case WM_DESTROY:
            release_fps_cursor();
            KillTimer(handle, frame_timer);
            input.reset();
            graphics.reset();
            PostQuitMessage(exit_status);
            return 0;
        }
        return DefWindowProcW(handle, msg, wparam, lparam);
    }

    HWND child(wchar_t const* type, wchar_t const* label, DWORD style, int id)
    {
        HWND const result = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | style,
            0, 0, 1, 1, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        if (!result)
            throw std::runtime_error("Cannot create editor control");
        SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return result;
    }

    void create_editor_menu()
    {
        auto menu = CreateMenu();
        if (!menu)
            throw std::runtime_error("Cannot create editor menu");
        auto category = [menu](wchar_t const* caption) {
            auto popup=CreatePopupMenu();
            if (!popup)
                throw std::runtime_error("Cannot create editor menu group");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), caption);
            return popup;
        };
        auto command = [](HMENU popup, int id, wchar_t const* caption) {
            AppendMenuW(popup, MF_STRING, id, caption);
        };
        auto file=category(L"&File");
        command(file,new_scene,L"&New Scene");
        command(file,open_scene,L"&Open Scene...");
        command(file,save_scene_id,L"&Save Scene");
        command(file,save_as,L"Save &As...");
        AppendMenuW(file,MF_SEPARATOR,0,nullptr);
        command(file,menu_project_manager,L"Project &Manager...");

        auto edit=category(L"&Edit");
        command(edit,menu_undo,L"&Undo\tCtrl+Z");
        command(edit,menu_redo,L"&Redo\tCtrl+Y");
        AppendMenuW(edit,MF_SEPARATOR,0,nullptr);
        command(edit,add_object,L"&Add Cube");
        command(edit,menu_duplicate,L"D&uplicate\tCtrl+D");
        command(edit,menu_add_child,L"Add &Child");
        command(edit,delete_object,L"Delete &Entity");
        command(edit,menu_delete_subtree,L"Delete &Subtree");

        auto world=category(L"&World");
        command(world,menu_scene_v3,L"Enable Scene &v3");
        command(world,menu_layer,L"Create &Layer for Selection");
        command(world,menu_cell,L"Create Streaming &Cell");
        command(world,menu_volume,L"Create Trigger &Volume");
        command(world,menu_portal,L"Create &Portal");
        command(world,menu_validate_scene,L"&Validate Scene");
        command(world,menu_streaming_diagnostics,L"Streaming &Diagnostics...");
        command(world,menu_streaming_overlay,L"Show Streaming &Cells (3D)");

        auto assets=category(L"&Assets");
        command(assets,menu_bind_asset,L"&Bind Asset to Selection...");
        command(assets,menu_open_content,L"Open &Content Folder");
        command(assets,menu_inspect_assets,L"Inspect Shared &Assets");
        AppendMenuW(assets,MF_SEPARATOR,0,nullptr);
        command(assets,menu_prefab_export,L"Export Selection as &Prefab...");
        command(assets,menu_prefab_insert,L"&Instantiate Prefab...");

        auto scripting=category(L"&Scripting");
        command(scripting,menu_script_attach,L"Attach &Script Component...");
        command(scripting,menu_script_remove,L"&Remove Script Component");

        auto play=category(L"&Play");
        command(play,play_scene,L"&Play / Stop Physics");
        command(play,menu_launch_fps,L"Launch &FPS Player");

        auto view=category(L"&View");
        command(view,menu_show_content,L"Open Native &Content Browser...\tCtrl+Shift+B");
        command(view,menu_legacy_content,L"Legacy Asset Panel (Compatibility)");
        auto tools=category(L"&Tools");
        command(tools,camera_timeline,L"&Camera Timeline...");
        if (!SetMenu(window,menu))
            throw std::runtime_error("Cannot assign editor menu");
        DrawMenuBar(window);
    }

    fs::path workspace_preferences() const
    {
        std::array<wchar_t,32768> base{};
        DWORD const length=GetEnvironmentVariableW(L"LOCALAPPDATA",
            base.data(),static_cast<DWORD>(base.size()));
        if (!length || length>=base.size())return {};
        return fs::path(base.data())/L"KaylasSystems"/L"NorthStarCpp"/L"workspace.ini";
    }
    void restore_workspace()
    {
        if(config.smoke)return;
        auto const file=workspace_preferences();
        if(file.empty())return;
        auto const read=[&](wchar_t const* name,int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(
                L"Layout",name,fallback,file.c_str()));
        };
        left_width=std::clamp(read(L"Outliner",240),185,600);
        right_width=std::clamp(read(L"Details",295),240,650);
        content_height=std::clamp(read(L"ContentHeight",250),168,700);
        content_visible=read(L"LegacyContentVisible",0)!=0;
        // Compatibility-only visual regression: production defaults to Qt.
        if(GetEnvironmentVariableW(L"NORTHSTARCPP_TEST_LEGACY_CONTENT",nullptr,0)>0)
            content_visible=true;
        gizmo_mode=static_cast<editor_tools::GizmoMode>(
            std::clamp(read(L"GizmoMode",0),0,2));
        gizmo_settings.local=read(L"Local",0)!=0;
        gizmo_settings.snap=read(L"Snap",1)!=0;
        auto number=[&](wchar_t const* name,float initial)->float {
            wchar_t data[96]{};
            GetPrivateProfileStringW(L"Camera",name,L"",
                data,96,file.c_str());
            if(!data[0])return initial;
            wchar_t* end=nullptr;
            float const value=std::wcstof(data,&end);
            return end && *end==0 && std::isfinite(value) &&
                std::abs(value)<100000.f ? value : initial;
        };
        auto& pose=editor_camera.pose;
        pose.position.x=number(L"X",pose.position.x);
        pose.position.y=number(L"Y",pose.position.y);
        pose.position.z=number(L"Z",pose.position.z);
        pose.yaw=number(L"Yaw",pose.yaw);
        pose.pitch=std::clamp(number(L"Pitch",pose.pitch),-1.52f,1.52f);
        camera_move_speed=std::clamp(number(L"Speed",8.f),.5f,100.f);
    }
    void persist_workspace() const
    {
        if(config.smoke)return;
        auto const file=workspace_preferences();
        if(file.empty())return;
        std::error_code ec;
        fs::create_directories(file.parent_path(),ec);
        if(ec)return;
        auto write=[&](wchar_t const* group,wchar_t const* name,
                       std::wstring value) {
            WritePrivateProfileStringW(group,name,value.c_str(),file.c_str());
        };
        write(L"Layout",L"Outliner",std::to_wstring(left_width));
        write(L"Layout",L"Details",std::to_wstring(right_width));
        write(L"Layout",L"ContentHeight",std::to_wstring(content_height));
        write(L"Layout",L"LegacyContentVisible",std::to_wstring(content_visible?1:0));
        write(L"Layout",L"GizmoMode",std::to_wstring(static_cast<int>(gizmo_mode)));
        write(L"Layout",L"Local",std::to_wstring(gizmo_settings.local?1:0));
        write(L"Layout",L"Snap",std::to_wstring(gizmo_settings.snap?1:0));
        write(L"Camera",L"X",std::to_wstring(editor_camera.pose.position.x));
        write(L"Camera",L"Y",std::to_wstring(editor_camera.pose.position.y));
        write(L"Camera",L"Z",std::to_wstring(editor_camera.pose.position.z));
        write(L"Camera",L"Yaw",std::to_wstring(editor_camera.pose.yaw));
        write(L"Camera",L"Pitch",std::to_wstring(editor_camera.pose.pitch));
        write(L"Camera",L"Speed",std::to_wstring(camera_move_speed));
    }
    void create_editor_controls()
    {
        wchar_t const* names[]{L"New", L"Open...", L"Save", L"Save As...", L"Add Cube", L"Delete", L"Play", L"Camera..."};
        for (int i = 0; i < static_cast<int>(buttons.size()); ++i)
            buttons[i] = child(L"BUTTON", names[i], WS_TABSTOP | BS_PUSHBUTTON, new_scene + i);
        create_editor_menu();
        inspector_tabs=child(L"COMBOBOX",L"",WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,inspector_pages);
        for (auto const* tab: {L"Object",L"Transform",L"Hierarchy / Asset",
                              L"LOD / Streaming",L"Streaming Budget"})
            SendMessageW(inspector_tabs,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(tab));
        SendMessageW(inspector_tabs,CB_SETCURSEL,0,0);
        list = child(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | WS_BORDER, object_list);
        wchar_t const* fields[]{L"Name", L"X", L"Y", L"Z", L"Size", L"Mass (kg)"};
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            labels[i] = child(L"STATIC", fields[i], 0, 0);
            edits[i] = child(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0);
            SendMessageW(edits[i], EM_SETLIMITTEXT, i == 0 ? 96 : 32, 0);
        }
        player = child(L"BUTTON", L"Player-controlled", WS_TABSTOP | BS_AUTOCHECKBOX, player_object);
        collision_flag = child(L"BUTTON", L"Collision", WS_TABSTOP | BS_AUTOCHECKBOX, collision_object);
        apply = child(L"BUTTON", L"Apply", WS_TABSTOP | BS_PUSHBUTTON, apply_properties);
        status = child(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0);
        outliner_title=child(L"STATIC",L"WORLD OUTLINER",SS_LEFTNOWORDWRAP,0);
        outliner_search=child(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,outliner_filter);
        SendMessageW(outliner_search,EM_SETCUEBANNER,0,
                     reinterpret_cast<LPARAM>(L"Filter actors by name..."));
        details_title=child(L"STATIC",L"DETAILS  /  PROPERTIES",SS_LEFTNOWORDWRAP,0);
        viewport_title=child(L"STATIC",L"PERSPECTIVE  /  SCENE VIEW",SS_LEFTNOWORDWRAP,0);
        content_title=child(L"STATIC",L"CONTENT BROWSER  /  PROJECT ASSETS",SS_LEFTNOWORDWRAP,0);
        wchar_t const* modes[]{L"W Move",L"E Rotate",L"R Scale"};
        int const ids[]{gizmo_move,gizmo_rotate,gizmo_scale};
        for (int i=0;i<3;++i)
            gizmo_buttons[i]=child(L"BUTTON",modes[i],WS_TABSTOP|BS_AUTOCHECKBOX|BS_PUSHLIKE,ids[i]);
        local_button=child(L"BUTTON",L"Local",WS_TABSTOP|BS_AUTOCHECKBOX,gizmo_local);
        snap_button=child(L"BUTTON",L"Snap",WS_TABSTOP|BS_AUTOCHECKBOX,gizmo_snap);
        SendMessageW(snap_button,BM_SETCHECK,BST_CHECKED,0);
        step_edit=child(L"EDIT",L"1",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,gizmo_step);
        SendMessageW(step_edit,EM_SETLIMITTEXT,12,0);
        content_browser=std::make_unique<EditorContentBrowser>(window);
        bind_button=child(L"BUTTON",L"Bind to Actor",WS_TABSTOP|BS_PUSHBUTTON,content_bind);
        place_button=child(L"BUTTON",L"Place Mesh",WS_TABSTOP|BS_PUSHBUTTON,content_place);
        reveal_button=child(L"BUTTON",L"Show Folder",WS_TABSTOP|BS_PUSHBUTTON,content_reveal);
        update_gizmo_buttons();
    }

    void update_gizmo_buttons()
    {
        for (int i=0;i<3;++i)
            SendMessageW(gizmo_buttons[i],BM_SETCHECK,
                static_cast<int>(gizmo_mode)==i ? BST_CHECKED : BST_UNCHECKED,0);
        SendMessageW(local_button,BM_SETCHECK,
            gizmo_settings.local ? BST_CHECKED : BST_UNCHECKED,0);
        SendMessageW(snap_button,BM_SETCHECK,
            gizmo_settings.snap ? BST_CHECKED : BST_UNCHECKED,0);
        float const step=gizmo_mode==editor_tools::GizmoMode::translate ?
            gizmo_settings.translation_step :
            gizmo_mode==editor_tools::GizmoMode::rotate ?
                gizmo_settings.rotation_step : gizmo_settings.scale_step;
        set_number(step_edit,step);
    }

    bool handle_splitter(UINT message,WPARAM,LPARAM parameter)
    {
        RECT rect{};GetClientRect(window,&rect);
        int const width=rect.right,height=rect.bottom;
        POINT pt{};
        if (message==WM_SETCURSOR) {
            GetCursorPos(&pt);
            ScreenToClient(window,&pt);
        } else {
            pt.x=GET_X_LPARAM(parameter);pt.y=GET_Y_LPARAM(parameter);
        }
        int const left=std::clamp(left_width,185,std::max(185,width/3));
        int const right=std::clamp(right_width,240,std::max(240,width/3));
        int const content_top=height-34-
            (content_visible ? std::clamp(content_height,168,std::max(168,height/2)) : 0);
        auto hover=Splitter::none;
        if (pt.y>=73 && pt.y<height-33 &&
            std::abs(pt.x-(left+13))<=5)
            hover=Splitter::left;
        else if (pt.y>=73 && pt.y<content_top-8 &&
                 std::abs(pt.x-(width-right-13))<=5)
            hover=Splitter::right;
        else if (content_visible && pt.x>=left+18 &&
                 std::abs(pt.y-(content_top-4))<=5)
            hover=Splitter::bottom;
        if (message==WM_SETCURSOR) {
            auto const active=splitter_drag==Splitter::none?hover:splitter_drag;
            if (active==Splitter::none)return false;
            SetCursor(LoadCursorW(nullptr,active==Splitter::bottom?IDC_SIZENS:IDC_SIZEWE));
            return true;
        }
        if (message==WM_LBUTTONDOWN && hover!=Splitter::none) {
            splitter_drag=hover;
            SetCapture(window);
            return true;
        }
        if (message==WM_MOUSEMOVE && splitter_drag!=Splitter::none) {
            if (splitter_drag==Splitter::left)
                left_width=std::clamp(static_cast<int>(pt.x)-13,185,std::max(185,width/3));
            if (splitter_drag==Splitter::right)
                right_width=std::clamp(width-static_cast<int>(pt.x)-13,240,std::max(240,width/3));
            if (splitter_drag==Splitter::bottom)
                content_height=std::clamp(height-34-static_cast<int>(pt.y),168,std::max(168,height/2));
            layout();
            return true;
        }
        if (message==WM_LBUTTONUP && splitter_drag!=Splitter::none) {
            splitter_drag=Splitter::none;
            ReleaseCapture();
            persist_workspace();
            return true;
        }
        return false;
    }

    void layout()
    {
        RECT rect{};
        GetClientRect(window, &rect);
        int const width = rect.right;
        int const height = rect.bottom;
        if (!editor) {
            MoveWindow(surface, 0, 0, width, height, TRUE);
            if (fps_caption) {
                int const mid = width / 2;
                int const top = std::max(30, height / 2 - 125);
                MoveWindow(fps_caption, mid - 200, top, 400, 42, TRUE);
                for (int i = 0; i < 3; ++i)
                    MoveWindow(fps_buttons[i], mid - 135, top + 50 + i * 54,
                               270, 42, TRUE);
            }
            return;
        }
        // Win32 can send WM_SIZE during child creation, before docking widgets
        // are constructed. Do not lay out the editor until all panes exist.
        if (!content_browser)
            return;
        int x=8;
        for (auto* button:buttons) {
            MoveWindow(button,x,7,72,30,TRUE);
            x+=75;
        }
        int gx=x+10;
        for (auto* button:gizmo_buttons) {
            MoveWindow(button,gx,7,73,30,TRUE);
            gx+=76;
        }
        MoveWindow(local_button,gx+2,8,58,29,TRUE);
        MoveWindow(snap_button,gx+63,8,58,29,TRUE);
        MoveWindow(step_edit,gx+127,10,69,25,TRUE);

        // Resizable three-column workspace, with a bottom asset dock.
        int const left=std::clamp(left_width,185,std::max(185,width/3));
        int const right=std::clamp(right_width,240,std::max(240,width/3));
        int const left_x=8, mid_x=left+18, right_x=width-right-8;
        int const mid_w=std::max(110,right_x-mid_x-10);
        int const content_h=content_visible ? std::clamp(content_height,168,std::max(168,height/2)) : 0;
        int const content_top=height-34-content_h;
        int const upper_bottom=content_visible ? content_top-8 : height-32;
        int const workspace_y=47, frame_y=73;
        int const upper_h=std::max(120,upper_bottom-frame_y);
        MoveWindow(outliner_title,left_x,workspace_y,left,23,TRUE);
        MoveWindow(outliner_search,left_x,frame_y,left,27,TRUE);
        MoveWindow(list,left_x,frame_y+32,left,std::max(95,height-frame_y-67),TRUE);
        MoveWindow(viewport_title,mid_x,workspace_y,mid_w,22,TRUE);
        MoveWindow(surface,mid_x,frame_y,mid_w,upper_h,TRUE);
        MoveWindow(details_title,right_x,workspace_y,right,22,TRUE);
        MoveWindow(inspector_tabs,right_x,frame_y,right,350,TRUE);
        for (int i=0;i<static_cast<int>(labels.size());++i) {
            int const y=frame_y+39+i*39;
            MoveWindow(labels[i],right_x+3,y+3,100,23,TRUE);
            MoveWindow(edits[i],right_x+108,y,std::max(70,right-111),27,TRUE);
        }
        int const actions_y=frame_y+39+6*39+6;
        MoveWindow(player,right_x+3,actions_y,100,27,TRUE);
        MoveWindow(collision_flag,right_x+105,actions_y,98,27,TRUE);
        MoveWindow(apply,right_x+right-90,actions_y+32,85,27,TRUE);
        if (content_visible) {
            int const content_x=mid_x, content_w=std::max(240,width-content_x-8);
            ShowWindow(content_title,SW_SHOW);
            MoveWindow(content_title,content_x,content_top,content_w-300,23,TRUE);
            MoveWindow(bind_button,content_x+content_w-296,content_top-2,101,26,TRUE);
            MoveWindow(place_button,content_x+content_w-190,content_top-2,90,26,TRUE);
            MoveWindow(reveal_button,content_x+content_w-95,content_top-2,95,26,TRUE);
            content_browser->layout(content_x,content_top+26,content_w,content_h-27);
        }
        content_browser->set_visible(content_visible);
        for (auto h:{bind_button,place_button,reveal_button})
            ShowWindow(h,content_visible ? SW_SHOW : SW_HIDE);
        if (!content_visible)ShowWindow(content_title,SW_HIDE);
        MoveWindow(status,8,height-26,std::max(1,width-16),21,TRUE);
    }

    int selected_index() const
    {
        auto ids = document.entities();
        auto it = std::find(ids.begin(),ids.end(),selected);
        return it == ids.end() ? -1 : static_cast<int>(it-ids.begin());
    }

    bool valid_selection() const
    {
        return selected != 0 && document.alive(selected);
    }

    void refresh_outliner(Scene const& view)
    {
        SendMessageW(list,WM_SETREDRAW,FALSE,0);
        SendMessageW(list,LB_RESETCONTENT,0,0);
        outliner_indices.clear();
        auto filter=get_text(outliner_search);
        std::transform(filter.begin(),filter.end(),filter.begin(),
            [](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
        int selected_row=-1;
        int const chosen=selected_index();
        for (std::size_t i=0;i<view.objects.size();++i) {
            auto name=to_wide(editor_tools::hierarchy_label(view,i));
            if (view.objects[i].player)name+=L"  [Player]";
            if (!view.objects[i].node.script.empty())name+=L"  [JS]";
            auto match=name;
            std::transform(match.begin(),match.end(),match.begin(),
                [](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
            if (!filter.empty() && match.find(filter)==std::wstring::npos)
                continue;
            if (static_cast<int>(i)==chosen)
                selected_row=static_cast<int>(outliner_indices.size());
            outliner_indices.push_back(i);
            SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
        }
        SendMessageW(list,LB_SETCURSEL,selected_row,0);
        SendMessageW(list,WM_SETREDRAW,TRUE,0);
        InvalidateRect(list,nullptr,TRUE);
    }

    void refresh()
    {
        if (!editor)
            return;
        // A selection/inspector refresh is not a scene edit. Keep streaming
        // queues and pinned-cell state alive across ordinary UI updates.
        auto const view=document.snapshot();
        refresh_outliner(view);
        if (content_browser)
            content_browser->set_project(file_path);
        update_inspector();

        std::wstring title = L"NorthStarCpp Editor - ";
        title += file_path.empty() ? L"Untitled" : file_path.filename().wstring();
        if (dirty)
            title += L" *";
        SetWindowTextW(window, title.c_str());
        SetWindowTextW(buttons[play_scene - new_scene], preview ? L"Stop" : L"Play");
        for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
            if (i == play_scene - new_scene || i == camera_timeline - new_scene)
                continue;
            EnableWindow(buttons[i],
                !preview && (i != delete_object - new_scene || valid_selection()));
        }
        EnableWindow(list, !preview);
        std::wstring summary=preview ? L"Play mode | WASD / arrows | Esc to stop"
            : (L"Editor | ECS: "+to_wide(document.provider_name())+
                L" | Scene v"+std::to_wstring(document.snapshot().version)+
                L" | Entities: "+std::to_wstring(document.size())+
                L" | Undo: "+(history.can_undo()?L"yes":L"no"));
        if (scene_world) {
            auto const& diagnostics=scene_world->streaming_diagnostics();
            summary+=L" | Cells: "+std::to_wstring(diagnostics.loaded_cells)+L"/"+
                std::to_wstring(scene_world->source().cells.size())+
                L" | Queue: "+std::to_wstring(diagnostics.pending_loads)+L" in / "+
                std::to_wstring(diagnostics.pending_unloads)+L" out"+
                L" | Visible: "+std::to_wstring(diagnostics.visible_entities);
        }
        SetWindowTextW(status,summary.c_str());
    }

    void update_inspector()
    {
        if (!editor)
            return;
        static wchar_t const* captions[5][6] = {
            {L"Name",L"X",L"Y",L"Z",L"Size",L"Mass (kg)"},
            {L"Rotation X",L"Rotation Y",L"Rotation Z",L"Scale X",L"Scale Y",L"Scale Z"},
            {L"Entity ID",L"Parent ID",L"Layer",L"Cell",L"Room",L"Asset"},
            {L"LOD Near",L"LOD Far",L"Load Radius",L"Unload",L"Lookahead",L"World Name"},
            {L"Loads / Frame",L"Unloads / Frame",L"Load Radius",L"Unload",L"Lookahead",L"World Name"}
        };
        bool const active = valid_selection();
        for(int i=0;i<6;++i) {
            SetWindowTextW(labels[i],captions[inspector_page][i]);
            EnableWindow(edits[i],active && !preview &&
                !(inspector_page==2 && i==0));
        }
        bool const is_basic=inspector_page==0;
        bool const is_metadata=inspector_page==2 || inspector_page==3;
        ShowWindow(player,is_basic||is_metadata ? SW_SHOW : SW_HIDE);
        ShowWindow(collision_flag,is_metadata ? SW_SHOW : SW_HIDE);
        EnableWindow(player,active && !preview);
        EnableWindow(collision_flag,active && !preview);
        EnableWindow(apply,active && !preview);
        EnableWindow(buttons[delete_object-new_scene],active && !preview);
        SetWindowTextW(player,is_basic ? L"Player" : L"Visible");
        if (!active) {
            for(auto* edit:edits)SetWindowTextW(edit,L"");
            SendMessageW(player,BM_SETCHECK,BST_UNCHECKED,0);
            SendMessageW(collision_flag,BM_SETCHECK,BST_UNCHECKED,0);
            return;
        }
        auto const scene = document.snapshot();
        auto const object = document.object(selected);
        auto const& node = object.node;
        if (inspector_page==0) {
            SetWindowTextW(edits[0],to_wide(object.name).c_str());
            set_number(edits[1],object.position.x);
            set_number(edits[2],object.position.y);
            set_number(edits[3],object.position.z);
            set_number(edits[4],object.size);
            set_number(edits[5],object.mass);
        } else if (inspector_page==1) {
            set_number(edits[0],node.rotation.x);
            set_number(edits[1],node.rotation.y);
            set_number(edits[2],node.rotation.z);
            set_number(edits[3],node.scale.x);
            set_number(edits[4],node.scale.y);
            set_number(edits[5],node.scale.z);
        } else if (inspector_page==2) {
            SetWindowTextW(edits[0],std::to_wstring(node.id).c_str());
            SetWindowTextW(edits[1],std::to_wstring(node.parent).c_str());
            SetWindowTextW(edits[2],to_wide(node.layer).c_str());
            SetWindowTextW(edits[3],to_wide(node.cell).c_str());
            SetWindowTextW(edits[4],to_wide(node.room).c_str());
            SetWindowTextW(edits[5],to_wide(node.asset).c_str());
        } else if (inspector_page==3) {
            set_number(edits[0],node.lod_near);
            set_number(edits[1],node.lod_far);
            set_number(edits[2],scene.streaming.load_radius);
            set_number(edits[3],scene.streaming.unload_radius);
            set_number(edits[4],scene.streaming.lookahead_seconds);
            SetWindowTextW(edits[5],to_wide(scene.name).c_str());
        } else {
            SetWindowTextW(edits[0],
                std::to_wstring(scene.streaming.max_cell_loads_per_update).c_str());
            SetWindowTextW(edits[1],
                std::to_wstring(scene.streaming.max_cell_unloads_per_update).c_str());
            set_number(edits[2],scene.streaming.load_radius);
            set_number(edits[3],scene.streaming.unload_radius);
            set_number(edits[4],scene.streaming.lookahead_seconds);
            SetWindowTextW(edits[5],to_wide(scene.name).c_str());
        }
        SendMessageW(player,BM_SETCHECK,
            (is_basic ? object.player : node.visible) ? BST_CHECKED : BST_UNCHECKED,0);
        SendMessageW(collision_flag,BM_SETCHECK,
            node.collision ? BST_CHECKED : BST_UNCHECKED,0);
    }

    fs::path choose_file(bool write)
    {
        std::array<wchar_t, 32768> name{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter = write
            ? L"NorthStar Binary Scene (*.nscn)\0*.nscn\0\0"
            : L"NorthStar Binary Scene (*.nscn)\0*.nscn\0Legacy Text Scene (*.nscene)\0*.nscene\0\0";
        dialog.lpstrDefExt = L"nscn";
        dialog.lpstrFile = name.data();
        dialog.nMaxFile = name.size();
        dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
            (write ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        BOOL const result = write ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
        if (!result && CommDlgExtendedError())
            throw std::runtime_error("Cannot open the scene file dialog");
        return result ? fs::path(name.data()) : fs::path{};
    }

    bool save(bool ask_path)
    {
        auto const target = ask_path || file_path.empty() ? choose_file(true) : file_path;
        if (target.empty())
            return false;
        if (config.new_document && !ask_path && target == config.scene_path && fs::exists(target) &&
            MessageBoxW(window, L"A scene already exists at this path. Replace it?",
                L"NorthStarCpp Editor", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return false;
        if (target.extension()==L".nscene")
            throw std::runtime_error("Legacy .nscene is read-only. Use Save As to create a .nscn binary scene.");
        save_scene(document.snapshot(), target);
        config.new_document = false;
        file_path = target;
        dirty = false;
        if (scene_world)
            rebuild_scene_world(); // Refresh on-disk NEF8 index after Save As.
        refresh();
        return true;
    }

    bool confirm_discard()
    {
        if (!dirty)
            return true;
        auto const result = MessageBoxW(window, L"Save the changes to this scene?",
            L"NorthStarCpp Editor", MB_YESNOCANCEL | MB_ICONQUESTION);
        return result == IDNO || (result == IDYES && save(false));
    }

    void launch_camera_timeline()
    {
        std::array<wchar_t,32768> host{};
        auto const count=GetModuleFileNameW(nullptr,host.data(),
            static_cast<DWORD>(host.size()));
        if (!count || count>=host.size())
            throw std::runtime_error("Cannot locate the camera editor executable directory");
        auto tool=fs::path(host.data()).parent_path() /
            L"northstarcpp_camera_timeline_editor.exe";
        if (!fs::is_regular_file(tool))
            tool=fs::path(host.data()).parent_path()/L"qt_tools"/
                L"northstarcpp_camera_timeline_editor.exe";
        if (!fs::is_regular_file(tool))
            throw std::runtime_error("Camera Timeline Editor is missing from bin/");
        std::wstring command_line=L"\"" + tool.wstring() + L"\"";
        if (!file_path.empty()) {
            auto const track=fs::absolute(file_path).parent_path() /
                "assets/cinematics/intro.camera.json";
            if (fs::is_regular_file(track))
                command_line+=L" \"" + track.wstring() + L"\"";
        }
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(tool.c_str(),command_line.data(),
                nullptr,nullptr,FALSE,0,nullptr,tool.parent_path().c_str(),
                &startup,&process))
            throw std::runtime_error("Unable to launch the Camera Timeline Editor");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }

    std::size_t current_selection_index() const
    {
        auto const index=selected_index();
        return index<0 ? std::numeric_limits<std::size_t>::max() :
            static_cast<std::size_t>(index);
    }

    void select_index(std::size_t index)
    {
        selected=index<document.size() ? document.at(index) : 0;
    }

    void commit_scene(Scene modified, std::size_t select)
    {
        validate_scene(modified);
        auto before=document.snapshot();
        auto const before_selected=current_selection_index();
        document.load(modified);
        dirty=true; // Unsaved edits must not reuse the on-disk cell index.
        rebuild_scene_world();
        history.record(std::move(before),before_selected);
        select_index(select);
        refresh();
    }

    void history_step(bool redo)
    {
        Scene restored;
        std::size_t target=0;
        auto const now=document.snapshot();
        auto const index=current_selection_index();
        bool const changed=redo
            ? history.redo(now,index,restored,target)
            : history.undo(now,index,restored,target);
        if (!changed)
            return;
        document.load(restored);
        rebuild_scene_world();
        select_index(target);
        dirty=true;
        refresh();
    }

    void launch_tool(wchar_t const* name, std::wstring arguments = {})
    {
        std::array<wchar_t,32768> exe{};
        auto const n=GetModuleFileNameW(nullptr,exe.data(),
            static_cast<DWORD>(exe.size()));
        if (!n || n>=exe.size())
            throw std::runtime_error("Cannot locate editor installation");
        auto path=fs::path(exe.data()).parent_path()/name;
        if (!fs::is_regular_file(path)) {
            // Qt tools have their own private DLL directory when co-packaged
            // with a UCRT V8 runtime; legacy flat layouts remain supported.
            path=fs::path(exe.data()).parent_path()/L"qt_tools"/name;
        }
        if (!fs::is_regular_file(path))
            throw std::runtime_error("Requested engine executable is not installed");
        auto command_line=L"\"" + path.wstring() + L"\"";
        if(!arguments.empty())
            command_line+=L" "+arguments;
        STARTUPINFOW start{};start.cb=sizeof(start);
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(path.c_str(),command_line.data(),nullptr,nullptr,
                FALSE,0,nullptr,path.parent_path().c_str(),&start,&process))
            throw std::runtime_error("Cannot launch requested engine tool");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }

    void open_content_folder()
    {
        if(file_path.empty())
            throw std::runtime_error("Save the scene in a project before opening content");
        auto content=fs::absolute(file_path).parent_path()/"assets";
        if(!fs::is_directory(content))
            throw std::runtime_error("This scene has no assets directory");
        if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",
             content.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
            throw std::runtime_error("Cannot open project assets folder");
    }

    void bind_selected_asset()
    {
        if(!valid_selection() || file_path.empty())
            throw std::runtime_error("Select an entity and save the scene in a project first");
        auto root=fs::absolute(file_path).parent_path()/"assets";
        if(!fs::is_directory(root))
            throw std::runtime_error("Project assets directory does not exist");
        std::array<wchar_t,32768> name{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize=sizeof(dialog);
        dialog.hwndOwner=window;
        dialog.lpstrFilter=L"Engine assets (*.ydd;*.ymat;*.netd;*.nscn;*.nscene)\0*.ydd;*.ymat;*.netd;*.nscn;*.nscene\0All files (*.*)\0*.*\0";
        dialog.lpstrFile=name.data();
        dialog.nMaxFile=static_cast<DWORD>(name.size());
        dialog.lpstrInitialDir=root.c_str();
        dialog.Flags=OFN_NOCHANGEDIR|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(!GetOpenFileNameW(&dialog)) {
            if(CommDlgExtendedError())
                throw std::runtime_error("Cannot choose project asset");
            return;
        }
        auto absolute=fs::weakly_canonical(fs::path(name.data()));
        auto canonical_root=fs::weakly_canonical(root);
        auto relative=absolute.lexically_relative(canonical_root);
        if(relative.empty() || relative.is_absolute() ||
            *relative.begin()==fs::path(".."))
            throw std::runtime_error("Assets must be inside the project assets directory");
        auto scene=document.snapshot();
        auto const index=current_selection_index();
        editor_tools::promote_to_v3(scene);
        scene.objects[index].node.asset=relative.generic_u8string();
        commit_scene(std::move(scene),index);
    }

    fs::path project_prefab_file(bool for_export) {
        if (file_path.empty())
            throw std::runtime_error("Save the current project scene before using prefabs");
        auto const root=script_project_root(file_path)/"prefabs";
        if (fs::is_symlink(root))
            throw std::runtime_error("Project prefabs directory cannot be a symbolic link");
        if (for_export)
            fs::create_directories(root);
        else if (!fs::is_directory(root))
            throw std::runtime_error("Project has no prefabs/ directory");
        auto dir=root.wstring();
        std::array<wchar_t,32768> selected_name{};
        OPENFILENAMEW picker{};
        picker.lStructSize=sizeof(picker);
        picker.hwndOwner=window;
        picker.lpstrFile=selected_name.data();
        picker.nMaxFile=static_cast<DWORD>(selected_name.size());
        picker.lpstrInitialDir=dir.c_str();
        picker.lpstrFilter=L"NorthStar Prefabs (*.nprefab)\0*.nprefab\0\0";
        picker.lpstrDefExt=L"nprefab";
        picker.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|
            (for_export ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        bool const accepted=for_export ? GetSaveFileNameW(&picker)!=FALSE :
                                         GetOpenFileNameW(&picker)!=FALSE;
        if (!accepted) {
            if (CommDlgExtendedError())
                throw std::runtime_error("Cannot select a project prefab file");
            return {};
        }
        fs::path absolute=fs::weakly_canonical(fs::path(selected_name.data()));
        fs::path base=fs::weakly_canonical(root);
        auto relative=absolute.lexically_relative(base);
        if (relative.empty() || relative.is_absolute() || *relative.begin()==fs::path("..") ||
            absolute.extension()!=L".nprefab" || fs::is_symlink(absolute))
            throw std::runtime_error("Prefab must be a .nprefab file inside project prefabs/");
        return absolute;
    }

    void export_prefab_selection() {
        if (!valid_selection())
            throw std::runtime_error("Select a scene entity to export a prefab");
        auto prefab=editor_tools::capture_prefab(document.snapshot(),
                                                 current_selection_index());
        auto path=project_prefab_file(true);
        if (path.empty()) return;
        save_scene(prefab,path);
        // Prefab capture does not modify the authored scene or undo history.
    }

    void instantiate_project_prefab() {
        auto path=project_prefab_file(false);
        if (path.empty()) return;
        auto prefab=load_scene(path);
        auto scene=document.snapshot();
        editor_tools::promote_to_v3(scene);
        auto const selected=current_selection_index();
        std::uint64_t parent_id=0;
        Vector3 anchor{0.f,0.f,0.f};
        if (selected<scene.objects.size()) {
            auto const& source=scene.objects[selected];
            parent_id=source.node.id;
            anchor=source.position;
            anchor.x+=std::max(1.f,source.size*1.5f);
        }
        auto const index=editor_tools::instantiate_prefab(
            scene,prefab,anchor,parent_id);
        commit_scene(std::move(scene),index);
    }

    void edit_selected_script(bool remove) {
        if (!valid_selection() || file_path.empty())
            throw std::runtime_error("Select an entity in a saved project scene first");
        auto scene=document.snapshot();
        auto const index=current_selection_index();
        editor_tools::promote_to_v3(scene);
        if (remove) {
            scene.objects[index].node.script.clear();
            commit_scene(std::move(scene),index);
            return;
        }
        auto root=script_project_root(file_path)/"scripts";
        if (!fs::is_directory(root) || fs::is_symlink(root))
            throw std::runtime_error("Create a regular scripts/ project directory first");
        std::array<wchar_t,32768> name{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize=sizeof(dialog);
        dialog.hwndOwner=window;
        dialog.lpstrFilter=L"JavaScript files (*.js;*.mjs)\0*.js;*.mjs\0";
        dialog.lpstrFile=name.data();
        dialog.nMaxFile=static_cast<DWORD>(name.size());
        dialog.lpstrInitialDir=root.c_str();
        dialog.Flags=OFN_NOCHANGEDIR|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(!GetOpenFileNameW(&dialog)) {
            if(CommDlgExtendedError())
                throw std::runtime_error("Cannot select project script");
            return;
        }
        auto absolute=fs::weakly_canonical(fs::path(name.data()));
        auto scripts=fs::weakly_canonical(root);
        auto relative=absolute.lexically_relative(scripts);
        if(relative.empty() || relative.is_absolute() ||
            *relative.begin()==fs::path("..") || fs::is_symlink(absolute))
            throw std::runtime_error("Script must be inside the project's scripts directory");
        auto filename=relative.generic_u8string();
        if (!(fs::path(filename).extension()==".js" ||
              fs::path(filename).extension()==".mjs"))
            throw std::runtime_error("Entity scripts must use .js or .mjs");
        scene.objects[index].node.script="scripts/"+filename;
        commit_scene(std::move(scene),index);
    }

    void inspect_assets()
    {
        if (file_path.empty())
            throw std::runtime_error("Save your scene in a project first");
        auto root=fs::absolute(file_path).parent_path()/"assets";
        if (!fs::is_directory(root))
            throw std::runtime_error("Project assets folder is missing");
        AssetManager assets;
        assets.mount_directory("",root);
        std::size_t references=0,missing=0;
        std::wstring missing_list;
        for(auto const& object:document.snapshot().objects) {
            if(object.node.asset.empty())continue;
            ++references;
            auto const at=object.node.asset.find('@');
            auto path=object.node.asset.substr(0,at);
            try {
                (void)assets.size(path);
            } catch(std::exception const&) {
                ++missing;
                if(missing_list.size()<750)
                    missing_list+=L"\n"+to_wide(object.node.asset);
            }
        }
        std::wstring message=L"Active Asset Manager: "+to_wide(assets.provider_name())+
            L"\nBound references: "+std::to_wstring(references)+
            L"\nMissing references: "+std::to_wstring(missing);
        if(missing)
            message+=missing_list;
        MessageBoxW(window,message.c_str(),L"NorthStarCpp Asset Diagnostics",
            MB_OK|(missing?MB_ICONWARNING:MB_ICONINFORMATION));
    }

    void validate_editor_scene()
    {
        auto scene=document.snapshot();
        validate_scene(scene);
        std::wstring message=L"Scene version: "+std::to_wstring(scene.version)+
            L"\nEntities: "+std::to_wstring(scene.objects.size())+
            L"\nLayers: "+std::to_wstring(scene.layers.size())+
            L"\nStreaming cells: "+std::to_wstring(scene.cells.size())+
            L"\nVolumes: "+std::to_wstring(scene.volumes.size())+
            L"\nPortals: "+std::to_wstring(scene.portals.size());
        if(scene.version==3) {
            SceneWorld world(scene);
            auto center=valid_selection() ? document.object(selected).position : Vector3{0.f,0.f,0.f};
            world.update(center);
            message+=L"\nResident entities: "+std::to_wstring(world.resident_indices().size())+
                L"\nVisible entities: "+std::to_wstring(world.visible_indices().size());
        }
        MessageBoxW(window,message.c_str(),L"Scene Validation: PASS",
            MB_OK|MB_ICONINFORMATION);
    }

    void drop_project_asset(std::string const& reference,POINT point)
    {
        if(reference.empty()||preview)return;
        RECT viewport{};
        GetWindowRect(surface,&viewport);
        MapWindowPoints(HWND_DESKTOP,window,reinterpret_cast<POINT*>(&viewport),2);
        RECT outliner{};
        GetWindowRect(list,&outliner);
        MapWindowPoints(HWND_DESKTOP,window,reinterpret_cast<POINT*>(&outliner),2);
        bool const in_viewport=PtInRect(&viewport,point)!=0;
        bool const in_outliner=PtInRect(&outliner,point)!=0;
        if(!in_viewport && !in_outliner)return;
        auto scene=document.snapshot();
        editor_tools::promote_to_v3(scene);
        auto const extension=fs::u8path(reference).extension().wstring();
        if(in_outliner || extension!=L".ydd") {
            if(!valid_selection())return;
            auto const index=current_selection_index();
            scene.objects[index].node.asset=reference;
            commit_scene(std::move(scene),index);
            return;
        }
        if(scene.objects.size()>=max_scene_objects)
            throw std::runtime_error("Scene object limit reached");
        SceneObject actor;
        actor.name=fs::u8path(reference).stem().u8string().substr(0,96);
        if(actor.name.empty())actor.name="Asset";
        actor.node.asset=reference;
        std::uint64_t id=1;
        for(auto const& item:scene.objects)
            id=std::max(id,item.node.id+1);
        actor.node.id=id;
        RECT bounds{};GetClientRect(surface,&bounds);
        float const x=float(point.x-viewport.left),y=float(point.y-viewport.top);
        auto const ray=editor_camera.screen_ray(x,y,
            float(bounds.right),float(bounds.bottom));
        float t=12.f;
        if(ray.direction.y< -.0001f) {
            float const ground=-ray.origin.y/ray.direction.y;
            if(std::isfinite(ground)&&ground>0.f&&ground<20000.f)t=ground;
        }
        actor.position={ray.origin.x+ray.direction.x*t,
                        ray.origin.y+ray.direction.y*t,
                        ray.origin.z+ray.direction.z*t};
        float const step=gizmo_settings.translation_step;
        if(gizmo_settings.snap && std::isfinite(step) && step>.0001f) {
            actor.position.x=std::round(actor.position.x/step)*step;
            actor.position.y=std::round(actor.position.y/step)*step;
            actor.position.z=std::round(actor.position.z/step)*step;
        }
        scene.objects.push_back(std::move(actor));
        auto const index=scene.objects.size()-1;
        commit_scene(std::move(scene),index);
    }

    void command(int id, int notification)
    {
        if (id == object_list && notification == LBN_SELCHANGE) {
            auto const row=static_cast<int>(SendMessageW(list,LB_GETCURSEL,0,0));
            selected=row>=0 && static_cast<std::size_t>(row)<outliner_indices.size() ?
                document.at(outliner_indices[static_cast<std::size_t>(row)]) : 0;
            update_inspector();
            return;
        }
        if (id==outliner_filter && notification==EN_CHANGE) {
            refresh_outliner(document.snapshot());
            return;
        }
        if (id == inspector_pages && notification == CBN_SELCHANGE) {
            auto const page=SendMessageW(inspector_tabs,CB_GETCURSEL,0,0);
            if(page>=0 && page<5) {
                inspector_page=static_cast<int>(page);
                update_inspector();
            }
            return;
        }
        if (id==gizmo_step && notification==EN_KILLFOCUS) {
            float const step=parse_number(step_edit);
            if (!std::isfinite(step) || step<=0.f || step>10000.f)
                throw std::runtime_error("Transform snap step must be between 0 and 10000");
            if (gizmo_mode==editor_tools::GizmoMode::translate)
                gizmo_settings.translation_step=step;
            else if (gizmo_mode==editor_tools::GizmoMode::rotate)
                gizmo_settings.rotation_step=step;
            else gizmo_settings.scale_step=step;
            return;
        }
        if (notification != BN_CLICKED)
            return;
        if (id==menu_show_content) {
            std::wstring arguments;
            if (!file_path.empty())
                arguments=L"--project \"" + fs::absolute(file_path).wstring() + L"\"";
            launch_tool(L"northstarcpp_asset_browser.exe",arguments);
            return;
        }
        if (id==menu_legacy_content) {
            content_visible=!content_visible;
            persist_workspace();
            CheckMenuItem(GetMenu(window),menu_legacy_content,
                MF_BYCOMMAND|(content_visible?MF_CHECKED:MF_UNCHECKED));
            layout();
            return;
        }
        if (id==gizmo_move || id==gizmo_rotate || id==gizmo_scale) {
            gizmo_mode=id==gizmo_move ? editor_tools::GizmoMode::translate :
                id==gizmo_rotate ? editor_tools::GizmoMode::rotate :
                    editor_tools::GizmoMode::scale;
            update_gizmo_buttons();
            return;
        }
        if (id==gizmo_local || id==gizmo_snap) {
            gizmo_settings.local=SendMessageW(local_button,BM_GETCHECK,0,0)==BST_CHECKED;
            gizmo_settings.snap=SendMessageW(snap_button,BM_GETCHECK,0,0)==BST_CHECKED;
            return;
        }
        if (id==content_reveal) {
            content_browser->show_selected_in_explorer();return;
        }
        if (id==content_bind || id==content_place) {
            if (preview)return;
            auto chosen=content_browser->selected_asset();
            if (!chosen)
                throw std::runtime_error("Select a file in Content Browser");
            auto scene=document.snapshot();
            editor_tools::promote_to_v3(scene);
            if (id==content_bind) {
                if (!valid_selection())
                    throw std::runtime_error("Select an actor in World Outliner first");
                auto const index=current_selection_index();
                scene.objects[index].node.asset=*chosen;
                commit_scene(std::move(scene),index);
            } else {
                auto const extension=fs::u8path(*chosen).extension().wstring();
                if (extension!=L".ydd" && extension!=L".YDD")
                    throw std::runtime_error("Place Mesh requires a .ydd drawable asset");
                if (scene.objects.size()>=max_scene_objects)
                    throw std::runtime_error("Scene entity limit reached");
                SceneObject actor;
                actor.name=fs::u8path(*chosen).stem().u8string().substr(0,96);
                if (actor.name.empty())actor.name="Asset";
                actor.node.asset=*chosen;
                std::uint64_t next=1;
                for (auto const& item:scene.objects)
                    next=std::max(next,item.node.id+1);
                actor.node.id=next;
                if (valid_selection()) {
                    auto const& selected_object=scene.objects[current_selection_index()];
                    actor.position=selected_object.position;
                    actor.position.x+=2.f;
                }
                scene.objects.push_back(std::move(actor));
                auto const new_index=scene.objects.size()-1;
                commit_scene(std::move(scene),new_index);
            }
            return;
        }
        if (id == menu_project_manager) {
            launch_tool(L"northstarcpp_project_manager.exe");
            return;
        }
        if (id == menu_validate_scene) {
            validate_editor_scene();
            return;
        }
        if (id == menu_streaming_diagnostics) {
            inspect_scene_streaming();
            return;
        }
        if (id == menu_streaming_overlay) {
            show_streaming_overlay=!show_streaming_overlay;
            auto* menu=GetMenu(window);
            if (menu)
                CheckMenuItem(menu,menu_streaming_overlay,
                    MF_BYCOMMAND | (show_streaming_overlay?MF_CHECKED:MF_UNCHECKED));
            if (surface)
                InvalidateRect(surface,nullptr,FALSE);
            return;
        }
        if (id == menu_open_content) {
            open_content_folder();
            return;
        }
        if (id == menu_inspect_assets) {
            inspect_assets();
            return;
        }
        if (id == menu_launch_fps) {
            if (file_path.empty() || dirty) {
                if (!save(false)) return;
            }
            launch_tool(L"northstarcpp_player.exe",
                L"--fps --scene \"" + file_path.wstring() + L"\"");
            return;
        }
        if (preview && id != play_scene && id != camera_timeline)
            return;
        if (id == menu_undo || id == menu_redo) {
            history_step(id == menu_redo);
            return;
        }
        if (id == menu_prefab_export) {
            export_prefab_selection();
            return;
        }
        if (id == menu_prefab_insert) {
            instantiate_project_prefab();
            return;
        }
        if (id == menu_bind_asset) {
            bind_selected_asset();
            return;
        }
        if (id == menu_script_attach || id == menu_script_remove) {
            edit_selected_script(id == menu_script_remove);
            return;
        }
        if (id == menu_scene_v3 || id == menu_duplicate || id == menu_add_child ||
            id == menu_delete_subtree || id == menu_layer || id == menu_cell ||
            id == menu_volume || id == menu_portal) {
            auto scene=document.snapshot();
            auto const index=current_selection_index();
            if (id == menu_scene_v3) {
                if (scene.version == 3) return;
                editor_tools::promote_to_v3(scene);
                commit_scene(std::move(scene),index);
                return;
            }
            if (!valid_selection())
                throw std::runtime_error("Select a scene entity before using this tool");
            auto result_index=index;
            if (id == menu_duplicate)
                result_index=editor_tools::duplicate(scene,index);
            else if (id == menu_add_child)
                result_index=editor_tools::duplicate(scene,index,true);
            else if (id == menu_delete_subtree)
                result_index=editor_tools::delete_subtree(scene,index);
            else if (id == menu_layer)
                editor_tools::create_layer(scene,index);
            else if (id == menu_cell)
                editor_tools::create_cell(scene,index);
            else if (id == menu_volume)
                editor_tools::create_volume(scene,index);
            else if (id == menu_portal)
                editor_tools::create_portal(scene,index);
            commit_scene(std::move(scene),result_index);
            return;
        }
        if (id == camera_timeline) {
            launch_camera_timeline();
            return;
        }
        if (id == play_scene) {
            if (preview) {
                stop_project_scripts();
                preview.reset();
                if (audio)
                    audio->stop();
            } else {
                preview = std::make_unique<PhysicsScene>(document.snapshot());
                if (scripted_first_person && !fps_camera) {
                    fps_camera=std::make_unique<CameraDirector>(fps_camera_settings);
                    update_script_camera(0.f);
                }
                try {
                    if (!config.smoke) initialize_project_scripts();
                } catch (...) {
                    preview.reset();
                    stop_project_scripts();
                    throw;
                }
                if (!config.smoke)
                    play_start_sound();
            }
            frame_clock.start();
            SetFocus(window);
            refresh();
            return;
        }
        if (preview)
            return;
        switch (id) {
        case new_scene:
            if (!confirm_discard())
                return;
            document.load(Scene{});
            history.clear();
            file_path.clear();
            rebuild_scene_world();
            selected = 0;
            dirty = false;
            break;
        case open_scene: {
            if (!confirm_discard())
                return;
            auto const path = choose_file(false);
            if (path.empty())
                return;
            auto loaded = read_document(path);
            document.load(loaded);
            history.clear();
            file_path = path;
            rebuild_scene_world();
            selected = 0;
            dirty = false;
            break;
        }
        case save_scene_id:
        case save_as:
            save(id == save_as);
            return;
        case add_object: {
            if (document.size() == max_scene_objects)
                throw std::runtime_error("Scene has too many objects");
            SceneObject object;
            object.name = "Cube " + std::to_string(document.size() + 1);
            object.mass = 1.f;
            object.position.x = 1.4f * static_cast<float>(document.size() % 8);
            object.position.z = 1.4f * static_cast<float>(document.size() / 8);
            auto before=document.snapshot();
            auto const previous=current_selection_index();
            selected = document.create(object);
            rebuild_scene_world();
            history.record(std::move(before),previous);
            dirty = true;
            break;
        }
        case delete_object: {
            if (!valid_selection())
                return;
            auto scene=document.snapshot();
            auto const index=editor_tools::delete_subtree(scene,current_selection_index());
            commit_scene(std::move(scene),index);
            return;
        }
        case apply_properties:
            if (!valid_selection())
                return;
            apply_edit();
            break;
        default:
            return;
        }
        refresh();
    }

    void apply_edit()
    {
        auto changed=document.snapshot();
        auto const index=current_selection_index();
        if(index>=changed.objects.size())
            throw std::runtime_error("Selected ECS entity is no longer alive");
        if(inspector_page!=0)
            editor_tools::promote_to_v3(changed);
        auto& object=changed.objects[index];

        if(inspector_page==0) {
            object.name=to_utf8(get_text(edits[0]));
            object.position={parse_number(edits[1]),parse_number(edits[2]),parse_number(edits[3])};
            object.size=parse_number(edits[4]);
            object.mass=parse_number(edits[5]);
            object.player=SendMessageW(player,BM_GETCHECK,0,0)==BST_CHECKED;
            if(object.player) {
                if(object.mass==0.f) object.mass=1.f;
                for(std::size_t i=0;i<changed.objects.size();++i)
                    if(i!=index)changed.objects[i].player=false;
            }
        } else if(inspector_page==1) {
            object.node.rotation={parse_number(edits[0]),parse_number(edits[1]),parse_number(edits[2])};
            object.node.scale={parse_number(edits[3]),parse_number(edits[4]),parse_number(edits[5])};
        } else if(inspector_page==2) {
            auto const number=to_utf8(get_text(edits[1]));
            if(number.empty() ||
               !std::all_of(number.begin(),number.end(),
                  [](unsigned char c) { return c>='0' && c<='9'; }))
                throw std::runtime_error("Parent ID must be an unsigned integer");
            object.node.parent=std::stoull(number);
            object.node.layer=to_utf8(get_text(edits[2]));
            if(object.node.layer.empty()) object.node.layer="Default";
            object.node.cell=to_utf8(get_text(edits[3]));
            object.node.room=to_utf8(get_text(edits[4]));
            object.node.asset=to_utf8(get_text(edits[5]));
            object.node.visible=SendMessageW(player,BM_GETCHECK,0,0)==BST_CHECKED;
            object.node.collision=SendMessageW(collision_flag,BM_GETCHECK,0,0)==BST_CHECKED;
            // Adding a layer is a scene transaction, not a Flecs-specific call.
            if(object.node.layer!="Default" &&
                std::none_of(changed.layers.begin(),changed.layers.end(),
                    [&](SceneLayer const& layer){return layer.name==object.node.layer;}))
                changed.layers.push_back({object.node.layer,true});
        } else if(inspector_page==3) {
            object.node.lod_near=parse_number(edits[0]);
            object.node.lod_far=parse_number(edits[1]);
            changed.streaming.load_radius=parse_number(edits[2]);
            changed.streaming.unload_radius=parse_number(edits[3]);
            changed.streaming.lookahead_seconds=parse_number(edits[4]);
            changed.name=to_utf8(get_text(edits[5]));
            object.node.visible=SendMessageW(player,BM_GETCHECK,0,0)==BST_CHECKED;
            object.node.collision=SendMessageW(collision_flag,BM_GETCHECK,0,0)==BST_CHECKED;
        } else {
            auto parse_budget=[](HWND edit)->std::uint32_t {
                auto const text=to_utf8(get_text(edit));
                if (text.empty() || !std::all_of(text.begin(),text.end(),
                     [](unsigned char c){return c>='0' && c<='9';}))
                    throw std::runtime_error("Cell budget must be an unsigned integer");
                auto const number=std::stoul(text);
                if (number<1 || number>8192)
                    throw std::runtime_error("Cell budget must be between 1 and 8192");
                return static_cast<std::uint32_t>(number);
            };
            changed.streaming.max_cell_loads_per_update=parse_budget(edits[0]);
            changed.streaming.max_cell_unloads_per_update=parse_budget(edits[1]);
            changed.streaming.load_radius=parse_number(edits[2]);
            changed.streaming.unload_radius=parse_number(edits[3]);
            changed.streaming.lookahead_seconds=parse_number(edits[4]);
            changed.name=to_utf8(get_text(edits[5]));
        }
        commit_scene(std::move(changed),index);
    }

    void play_fps_clip(AssetRequest<FpsAudioClip>& request)
    {
        if (!request.ready())
            return;
        try {
            auto const asset = request.get();
            auto const& clip = *asset;
            if (!clip.frames())
                return;
            if (!audio) audio = std::make_unique<AudioOutput>();
            NorthStarPcm16 const pcm{clip.samples.data(), clip.frames(),
                                      clip.sample_rate, clip.channels};
            audio->play(pcm);
        } catch (std::exception const& error) {
            OutputDebugStringA(error.what());
            std::fprintf(stderr, "FPS audio: %s\n", error.what());
            request = {};
        }
    }

    void play_start_sound()
    {
        try {
            if (!audio)
                audio = std::make_unique<AudioOutput>();
            std::array<int16_t, 1920> samples{};
            for (std::size_t i = 0; i < samples.size(); ++i) {
                float const t = static_cast<float>(i) / 24000.f;
                float const envelope = std::sin(3.14159265f * static_cast<float>(i) / samples.size());
                samples[i] = static_cast<int16_t>(2400.f * envelope * std::sin(2.f * 3.14159265f * 523.25f * t));
            }
            NorthStarPcm16 const pcm{samples.data(), static_cast<uint32_t>(samples.size()), 24000, 1};
            audio->play(pcm);
        } catch (std::exception const& error) {
            OutputDebugStringA(error.what());
            std::fprintf(stderr, "%s\n", error.what());
        }
    }



    void configure_script_bindings(ScriptRuntime& runtime) {
        for (auto const& entry : std::array<std::pair<char const*,LogLevel>,4>{{
                {"debug",LogLevel::debug},{"info",LogLevel::info},
                {"warn",LogLevel::warn},{"error",LogLevel::error}}}) {
            runtime.bind("engine.logger",entry.first,
                [this,level=entry.second](std::string_view json) {
                    auto args=script_args(json);
                    if (args.size()!=1 || args[0].size()>8192)
                        throw std::invalid_argument("engine.logger expects one message <= 8192 bytes");
                    if (script_logger) script_logger->write(level,"project.script",args[0]);
                    else if (script_logging_allowed)
                        std::fprintf(stderr,"[project.script] %s\n",args[0].c_str());
                    return std::string("null");
                });
        }
        // Storage is project-scoped and does not grant arbitrary filesystem I/O.
        // The save payload is a JSON string produced by JSON.stringify(...).
        runtime.bind("engine.save","write",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2) throw std::invalid_argument(
                "engine.save.write(slot,JSON.stringify(state))");
            project_save_store(config,file_path).write(args[0],args[1]);
            return std::string("true");
        });
        runtime.bind("engine.save","read",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1) throw std::invalid_argument("engine.save.read(slot)");
            auto value=project_save_store(config,file_path).read(args[0]);
            return value ? script_json_quote(*value) : std::string("null");
        });
        runtime.bind("engine.save","exists",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1) throw std::invalid_argument("engine.save.exists(slot)");
            return project_save_store(config,file_path).exists(args[0])
                ? std::string("true") : std::string("false");
        });
        runtime.bind("engine.save","remove",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1) throw std::invalid_argument("engine.save.remove(slot)");
            return project_save_store(config,file_path).erase(args[0])
                ? std::string("true") : std::string("false");
        });
        runtime.bind("engine.perlin","sample2",
            [this](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=2) throw std::invalid_argument("sample2 expects x,y");
                if (!script_noise) script_noise=std::make_unique<PerlinNoise>();
                return std::to_string(script_noise->sample2(
                    script_number(args[0]),script_number(args[1])));
            });
        runtime.bind("engine.scene","objectCount",
            [this](std::string_view json) {
                if (!script_args(json).empty())
                    throw std::invalid_argument("objectCount expects no arguments");
                return std::to_string(preview ? preview->scene().objects.size() : 0u);
            });
        runtime.bind("engine.scene","setLayerVisible",
            [this](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=2 || !scene_world ||
                    (args[1]!="true" && args[1]!="false"))
                    throw std::invalid_argument("setLayerVisible(name,bool)");
                scene_world->set_layer_visible(args[0],args[1]=="true");
                return std::string("null");
            });
        runtime.bind("engine.physics","applyImpulse",
            [this](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=4 || !preview)
                    throw std::invalid_argument("applyImpulse(index,x,y,z)");
                auto const index=script_index(args[0]);
                if (index>=preview->scene().objects.size())
                    throw std::out_of_range("Invalid scene object index");
                preview->apply_impulse(index,{
                    static_cast<float>(script_number(args[1])),
                    static_cast<float>(script_number(args[2])),
                    static_cast<float>(script_number(args[3]))});
                return std::string("null");
            });
        runtime.bind("engine.input","isDown",
            [this](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=1) throw std::invalid_argument("isDown(keyCode)");
                auto const index=script_index(args[0]);
                if (index>=script_keys.size()) throw std::out_of_range("Key code out of range");
                return script_keys[index] ? std::string("true") : std::string("false");
            });


        runtime.bind("engine.input","mouseDown",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1) throw std::invalid_argument("input.mouseDown(index)");
            auto idx=script_index(args[0]);
            if (idx>=script_mouse_buttons.size()) throw std::out_of_range("Mouse button index");
            return script_mouse_buttons[idx] ? std::string("true") : std::string("false");
        });
        runtime.bind("engine.input","pointerDelta",[this](std::string_view json) {
            if (!script_args(json).empty()) throw std::invalid_argument("input.pointerDelta()");
            return std::string("{\"x\":")+std::to_string(script_mouse_dx)+
                ",\"y\":"+std::to_string(script_mouse_dy)+"}";
        });
        runtime.bind("engine.input","gamepad",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1) throw std::invalid_argument("input.gamepad(index)");
            auto index=script_index(args[0]);
            if(index>=script_gamepads.size()) throw std::out_of_range("Gamepad index");
            auto const& p=script_gamepads[index];
            if(!p.connected) return std::string("null");
            return std::string("{\"buttons\":")+std::to_string(p.buttons)+
                ",\"leftX\":"+std::to_string(p.left_x)+
                ",\"leftY\":"+std::to_string(p.left_y)+
                ",\"rightX\":"+std::to_string(p.right_x)+
                ",\"rightY\":"+std::to_string(p.right_y)+
                ",\"leftTrigger\":"+std::to_string(p.left_trigger)+
                ",\"rightTrigger\":"+std::to_string(p.right_trigger)+"}";
        });
        runtime.bind("engine.camera","setAngles",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !scripted_first_person)
                throw std::invalid_argument("camera.setAngles(yaw,pitch)");
            auto yaw=script_number(args[0]),pitch=script_number(args[1]);
            if (std::abs(yaw)>1000000 || std::abs(pitch)>1.55)
                throw std::invalid_argument("Camera angles outside bounds");
            script_yaw=static_cast<float>(std::remainder(yaw,6.283185307179586));
            script_pitch=static_cast<float>(pitch);
            return std::string("null");
        });
        runtime.bind("engine.camera","angles",[this](std::string_view json) {
            if (!script_args(json).empty()) throw std::invalid_argument("camera.angles()");
            return std::string("{\"yaw\":")+std::to_string(script_yaw)+
                ",\"pitch\":"+std::to_string(script_pitch)+"}";
        });
        runtime.bind("engine.camera","shake",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !fps_camera)
                throw std::invalid_argument("camera.shake(amplitude,seconds)");
            auto a=script_number(args[0]),t=script_number(args[1]);
            if (a<0 || a>1 || t<0 || t>10) throw std::invalid_argument("Camera shake limits");
            fps_camera->shake(static_cast<float>(a),static_cast<float>(t));
            return std::string("null");
        });
        runtime.bind("engine.camera","setZoom",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !fps_camera) throw std::invalid_argument("camera.setZoom(level)");
            auto zoom=script_number(args[0]);
            if (zoom<0 || zoom>1) throw std::invalid_argument("Zoom must be 0..1");
            fps_camera->set_zoom(static_cast<float>(zoom));
            return std::string("null");
        });
        runtime.bind("engine.player","move",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !scripted_game)
                throw std::invalid_argument("player.move(worldX,worldZ)");
            auto x=script_number(args[0]),z=script_number(args[1]);
            if (std::abs(x)>1 || std::abs(z)>1)
                throw std::invalid_argument("Player movement components must be -1..1");
            script_move_x=static_cast<float>(x);
            script_move_z=static_cast<float>(z);
            return std::string("null");
        });
        runtime.bind("engine.player","jump",[this](std::string_view json) {
            if (!script_args(json).empty() || !preview || !scripted_game)
                throw std::invalid_argument("player.jump()");
            preview->jump();
            return std::string("null");
        });
        runtime.bind("engine.player","eye",[this](std::string_view json) {
            if (!script_args(json).empty() || !preview)
                throw std::invalid_argument("player.eye()");
            auto const& objects=preview->scene().objects;
            auto it=std::find_if(objects.begin(),objects.end(),
                [](SceneObject const& obj){return obj.player;});
            if (it==objects.end()) throw std::runtime_error("No player in scene");
            return std::string("{\"x\":")+std::to_string(it->position.x)+
                ",\"y\":"+std::to_string(it->position.y+script_eye_height)+
                ",\"z\":"+std::to_string(it->position.z)+"}";
        });
        runtime.bind("engine.player","setEyeHeight",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1) throw std::invalid_argument("player.setEyeHeight(height)");
            auto height=script_number(args[0]);
            if (height<0 || height>5) throw std::invalid_argument("Eye height must be 0..5");
            script_eye_height=static_cast<float>(height);
            return std::string("null");
        });
        runtime.bind("engine.physics","sweepSphere",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=8 || !preview)
                throw std::invalid_argument("physics.sweepSphere(x,y,z,toX,toY,toZ,radius,ignore)");
            Vector3 begin{static_cast<float>(script_number(args[0])),
                          static_cast<float>(script_number(args[1])),
                          static_cast<float>(script_number(args[2]))};
            Vector3 end{static_cast<float>(script_number(args[3])),
                        static_cast<float>(script_number(args[4])),
                        static_cast<float>(script_number(args[5]))};
            auto radius=script_number(args[6]);
            if (radius<=0 || radius>100) throw std::invalid_argument("Invalid sweep radius");
            auto hit=preview->sweep_sphere(begin,end,static_cast<float>(radius),
                                           script_index(args[7]));
            if (!hit || hit->object_index==UINT32_MAX) return std::string("null");
            return std::string("{\"index\":")+std::to_string(hit->object_index)+
                ",\"fraction\":"+std::to_string(hit->fraction)+"}";
        });
        runtime.bind("engine.scene","objects",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()>2 || !preview) throw std::invalid_argument("scene.objects(start,count)");
            auto start=args.empty()?std::size_t(0):script_index(args[0]);
            auto count=args.size()<2?std::size_t(256):script_index(args[1]);
            if (count>512) throw std::invalid_argument("Scene query page >512");
            std::string result="[";
            auto const& objects=preview->scene().objects;
            for(auto i=start;i<objects.size() && i-start<count;++i) {
                if(i!=start) result+=',';
                auto const& obj=objects[i];
                result+="{\"index\":"+std::to_string(i)+
                    ",\"name\":"+script_json_quote(obj.name)+
                    ",\"id\":"+script_json_quote(std::to_string(obj.node.id))+
                    ",\"script\":"+script_json_quote(obj.node.script)+
                    ",\"mass\":"+std::to_string(obj.mass)+
                    ",\"size\":"+std::to_string(obj.size)+
                    ",\"player\":"+(obj.player?"true":"false")+
                    ",\"position\":{\"x\":"+std::to_string(obj.position.x)+
                    ",\"y\":"+std::to_string(obj.position.y)+
                    ",\"z\":"+std::to_string(obj.position.z)+"}}";
            }
            return result+"]";
        });
        runtime.bind("engine.scene","moveStatic",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=4 || !preview) throw std::invalid_argument("scene.moveStatic(index,x,y,z)");
            auto i=script_index(args[0]);
            auto const& objs=preview->scene().objects;
            if (i>=objs.size() || objs[i].mass!=0.f || objs[i].player)
                throw std::invalid_argument("Scene entity must be static");
            auto x=script_number(args[1]),y=script_number(args[2]),z=script_number(args[3]);
            if (std::max({std::abs(x),std::abs(y),std::abs(z)})>1000000)
                throw std::invalid_argument("Position out of bounds");
            Vector3 pos{static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)};
            preview->move_static(i,pos);
            if(script_ecs && i<script_ecs->size()) {
                NsPosition updated{pos.x,pos.y,pos.z};
                script_ecs->set_component(script_ecs->at(i),NS_COMPONENT_POSITION,
                                          &updated,sizeof(updated));
            }
            return std::string("null");
        });
        runtime.bind("engine.scene","setColor",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=2 || !preview) throw std::invalid_argument("scene.setColor(index,rgb)");
            auto idx=script_index(args[0]),rgb=script_index(args[1]);
            if(idx>=preview->scene().objects.size() || rgb>0xffffff)
                throw std::invalid_argument("Scene object/color invalid");
            script_object_colors[idx]=static_cast<std::uint32_t>(rgb);
            return std::string("null");
        });
        runtime.bind("engine.ui","setTitle",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || args[0].size()>240)
                throw std::invalid_argument("ui.setTitle(text)");
            auto title=to_wide(args[0]);
            script_window_title=title;
            if(window) SetWindowTextW(window,title.c_str());
            return std::string("null");
        });
        runtime.bind("engine.session","quit",[this](std::string_view json) {
            if(!script_args(json).empty()) throw std::invalid_argument("session.quit()");
            if(window) PostMessageW(window,WM_CLOSE,0,0);
            return std::string("null");
        });
        runtime.bind("engine.session","pause",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || (args[0]!="true" && args[0]!="false"))
                throw std::invalid_argument("session.pause(boolean)");
            script_paused=args[0]=="true";
            if(script_paused) release_fps_cursor();
            else capture_fps_cursor();
            return std::string("null");
        });
        runtime.bind("engine.session","restart",[this](std::string_view json) {
            if(!script_args(json).empty()) throw std::invalid_argument("session.restart()");
            if(window) PostMessageW(window,WM_APP+27,0,0);
            return std::string("null");
        });
        runtime.bind("engine.audio","play",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !fps_assets)
                throw std::invalid_argument("audio.play(virtualWavAsset)");
            auto path=fs::u8path(args[0]);
            if(args[0].rfind("audio/",0)!=0 || path.extension()!=".wav" ||
                args[0].size()>256 || args[0].find("..")!=std::string::npos ||
                args[0].find('\\')!=std::string::npos)
                throw std::invalid_argument("Audio asset path must be audio/*.wav");
            auto it=script_audio_cache.find(args[0]);
            if(it==script_audio_cache.end()) {
                auto request=fps_assets->request_audio_clip(args[0]);
                it=script_audio_cache.emplace(args[0],*request.get()).first;
            }
            auto const& sound=it->second;
            if(!audio) audio=std::make_unique<AudioOutput>();
            NorthStarPcm16 pcm{sound.samples.data(),sound.frames(),
                              sound.sample_rate,sound.channels};
            audio->play(pcm);
            return std::string("null");
        });
        runtime.bind("engine.animation","play",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()<2 || args.size()>4 || !preview ||
                !script_assets || !script_animations)
                throw std::invalid_argument(
                    "animation.play(entityId,virtualPath,loop=false,speed=1)");
            auto const id=static_cast<std::uint64_t>(script_index(args[0]));
            auto const& path=args[1];
            if (path.rfind("animations/",0)!=0 || path.size()<=16 ||
                path.compare(path.size()-10,10,".anim.json")!=0 ||
                path.find('\\')!=std::string::npos ||
                path.size()>256)
                throw std::invalid_argument("Animation must be animations/*.anim.json");
            auto rel=fs::u8path(path);
            for (auto const& part:rel)
                if (part.empty() || part=="." || part==".." || part.is_absolute())
                    throw std::invalid_argument("Animation path traversal is forbidden");
            if (!script_source_root.empty()) {
                // Source-project files must not redirect through intermediate
                // directory links, including after hot reload.
                auto cursor=script_source_root;
                for (auto const& part:rel) {
                    cursor/=part;
                    if (fs::is_symlink(cursor))
                        throw std::invalid_argument("Redirected animation asset");
                }
                if (!fs::is_regular_file(cursor))
                    throw std::invalid_argument("Missing animation asset");
            }
            bool loop=false;
            if (args.size()>=3) {
                if (args[2]!="true" && args[2]!="false")
                    throw std::invalid_argument("Animation loop must be boolean");
                loop=args[2]=="true";
            }
            float speed=1.f;
            if (args.size()>=4) speed=static_cast<float>(script_number(args[3]));
            auto clip=load_scene_animation_clip(*script_assets,path);
            script_animations->play(preview->scene(),id,std::move(clip),loop,speed);
            return std::string("null");
        });
        runtime.bind("engine.animation","stop",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !script_animations)
                throw std::invalid_argument("animation.stop(entityId)");
            auto const id=static_cast<std::uint64_t>(script_index(args[0]));
            for (auto const& sample:script_animation_samples)
                if (sample.entity_id==id)
                    script_animation_visuals.erase(sample.scene_index);
            script_animations->stop(id);
            return std::string("null");
        });
        runtime.bind("engine.animation","pause",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !script_animations ||
                (args[1]!="true" && args[1]!="false"))
                throw std::invalid_argument("animation.pause(entityId,boolean)");
            script_animations->pause(script_index(args[0]),args[1]=="true");
            return std::string("null");
        });
        runtime.bind("engine.animation","seek",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !script_animations)
                throw std::invalid_argument("animation.seek(entityId,time)");
            script_animations->seek(script_index(args[0]),
                                    static_cast<float>(script_number(args[1])));
            return std::string("null");
        });
        runtime.bind("engine.animation","state",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !script_animations)
                throw std::invalid_argument("animation.state(entityId)");
            auto id=static_cast<std::uint64_t>(script_index(args[0]));
            if (!script_animations->active(id)) return std::string("null");
            auto state=script_animations->status(id);
            return std::string("{\"time\":")+std::to_string(state.time)+
                ",\"duration\":"+std::to_string(state.duration)+
                ",\"speed\":"+std::to_string(state.speed)+
                ",\"loop\":"+(state.loop?"true":"false")+
                ",\"paused\":"+(state.paused?"true":"false")+
                ",\"finished\":"+(state.finished?"true":"false")+"}";
        });
        runtime.bind("engine.ecs","entities",[this](std::string_view json) {
            if (!script_args(json).empty()) throw std::invalid_argument("entities() expects no arguments");
            std::string result="[";
            bool first=true;
            if (preview) for (auto const& object:preview->scene().objects) {
                if (!object.node.id) continue;
                if (!first) result+=',';
                first=false;
                result+=script_json_quote(std::to_string(object.node.id));
            }
            return result+"]";
        });
        runtime.bind("engine.ecs","query",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !script_ecs)
                throw std::invalid_argument("engine.ecs.query(componentName)");
            static const std::set<std::string> allowed={
                NS_COMPONENT_NAME,NS_COMPONENT_POSITION,NS_COMPONENT_VISUAL,
                NS_COMPONENT_BODY,NS_COMPONENT_CONTROLLER,NS_COMPONENT_MOTION,
                NS_COMPONENT_SCRIPT};
            if (!allowed.count(args[0])) throw std::invalid_argument("Unknown ECS component");
            std::string result="[";
            bool first=true;
            for (auto handle:script_ecs->query({args[0]})) {
                auto it=script_handle_ids.find(handle);
                if (it==script_handle_ids.end() || !it->second) continue;
                if (!first) result+=',';
                first=false;
                result+=script_json_quote(std::to_string(it->second));
            }
            return result+"]";
        });
        runtime.bind("engine.ecs","has",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !script_ecs)
                throw std::invalid_argument("engine.ecs.has(id,componentName)");
            static const std::set<std::string> allowed={
                NS_COMPONENT_NAME,NS_COMPONENT_POSITION,NS_COMPONENT_VISUAL,
                NS_COMPONENT_BODY,NS_COMPONENT_CONTROLLER,NS_COMPONENT_MOTION,
                NS_COMPONENT_SCRIPT};
            if (!allowed.count(args[1])) throw std::invalid_argument("Unknown ECS component");
            auto it=script_entity_indices.find(script_index(args[0]));
            if (it==script_entity_indices.end()) return std::string("false");
            return script_ecs->has_component(script_ecs->at(it->second),args[1])
                ? std::string("true") : std::string("false");
        });
        runtime.bind("engine.ecs","get",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=2 || !preview || !script_ecs)
                throw std::invalid_argument("engine.ecs.get(id,componentName)");
            auto found=script_entity_indices.find(script_index(args[0]));
            if (found==script_entity_indices.end())
                throw std::out_of_range("Unknown stable scene entity ID");
            auto const& obj=preview->scene().objects.at(found->second);
            if (args[1]==NS_COMPONENT_NAME)
                return script_json_quote(obj.name);
            if (args[1]==NS_COMPONENT_POSITION)
                return std::string("{\"x\":")+std::to_string(obj.position.x)+
                    ",\"y\":"+std::to_string(obj.position.y)+
                    ",\"z\":"+std::to_string(obj.position.z)+"}";
            if (args[1]==NS_COMPONENT_VISUAL)
                return std::string("{\"size\":")+std::to_string(obj.size)+
                    ",\"color\":"+std::to_string(obj.color)+"}";
            if (args[1]==NS_COMPONENT_BODY)
                return std::string("{\"mass\":")+std::to_string(obj.mass)+"}";
            if (args[1]==NS_COMPONENT_CONTROLLER)
                return obj.player ? std::string("true") : std::string("false");
            if (args[1]==NS_COMPONENT_SCRIPT)
                return obj.node.script.empty() ? std::string("null") :
                    script_json_quote(obj.node.script);
            if (args[1]==NS_COMPONENT_MOTION) {
                NsMotion motion{};
                script_ecs->get_component(script_ecs->at(found->second),
                    NS_COMPONENT_MOTION,&motion,sizeof(motion));
                return std::string("{\"x\":")+std::to_string(motion.x)+
                    ",\"z\":"+std::to_string(motion.z)+"}";
            }
            throw std::invalid_argument("Unsupported ECS component value type");
        });
        runtime.bind("engine.ecs","getScript",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !preview)
                throw std::invalid_argument("engine.ecs.getScript(id)");
            auto it=script_entity_indices.find(script_index(args[0]));
            if (it==script_entity_indices.end())
                throw std::out_of_range("Unknown stable scene entity ID");
            return script_json_quote(preview->scene().objects.at(it->second).node.script);
        });
        runtime.bind("engine.ecs","getPosition",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !preview)
                throw std::invalid_argument("engine.ecs.getPosition(id)");
            auto it=script_entity_indices.find(script_index(args[0]));
            if (it==script_entity_indices.end())
                throw std::out_of_range("Unknown stable scene entity ID");
            auto const position=preview->scene().objects.at(it->second).position;
            return std::string("{\"x\":")+std::to_string(position.x)+
                ",\"y\":"+std::to_string(position.y)+
                ",\"z\":"+std::to_string(position.z)+"}";
        });
        runtime.bind("engine.ecs","setPosition",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=4 || !preview || !script_ecs)
                throw std::invalid_argument("engine.ecs.setPosition(id,x,y,z)");
            auto it=script_entity_indices.find(script_index(args[0]));
            if (it==script_entity_indices.end())
                throw std::out_of_range("Unknown stable scene entity ID");
            auto const idx=it->second;
            auto const& body=preview->scene().objects.at(idx);
            if (body.mass!=0.f || body.player)
                throw std::invalid_argument("Only static ECS entities may be repositioned");
            auto finite_float=[](std::string const& str) {
                auto number=script_number(str);
                if (std::abs(number)>1000000.0)
                    throw std::invalid_argument("Position exceeds world bounds");
                return static_cast<float>(number);
            };
            Vector3 position{finite_float(args[1]),finite_float(args[2]),finite_float(args[3])};
            preview->move_static(idx,position);
            NsPosition next{position.x,position.y,position.z};
            script_ecs->set_component(script_ecs->at(idx),NS_COMPONENT_POSITION,
                                      &next,sizeof(next));
            return std::string("null");
        });

    }

    // Load project-relative JavaScript through engine.assets, which also
    // works with the exported game/ VFS layout. The scene controls exactly
    // which entity script assets may execute.
    void load_script_files(ScriptRuntime& runtime) {
        if (!script_assets || !preview)
            throw std::runtime_error("Script asset VFS is not mounted");
        if (script_entry_exists) {
            if (config.archive_path.empty())
                (void)validated_script_file(script_source_root,fs::path("scripts/main.js"));
            else
                validate_script_virtual_path(fs::path("scripts/main.js"));
            runtime.evaluate_asset(*script_assets,"scripts/main.js");
        }
        std::set<std::string> unique;
        for (auto const& object:preview->scene().objects)
            if (!object.node.script.empty() && unique.insert(object.node.script).second) {
                auto const relative=fs::u8path(object.node.script);
                validate_script_virtual_path(relative);
                if (config.archive_path.empty())
                    (void)validated_script_file(script_source_root,relative);
                auto bytes=script_assets->read(object.node.script);
                if (bytes.size()>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                    throw std::runtime_error("Entity script source exceeds 1 MiB");
                std::string script(bytes.begin(),bytes.end());
                // Unique lexical scope per script resource; subscriptions
                // persist in the current JS context until reset/hot reload.
                runtime.evaluate("(function(){\n"+script+"\n})();",object.node.script);
            }
    }

    void notify_script_entities(ScriptRuntime& runtime,char const* event,float dt=-1.f) {
        if (!preview) return;
        auto const& scene=preview->scene();
        // Batch JavaScript event dispatch rather than recompiling an individual
        // V8 script for every resident entity every frame. Each batch is
        // bounded independently of scene size and still uses the neutral ABI.
        std::string batch="[";
        std::size_t count=0;
        auto flush=[&] {
            if (!count) return;
            batch+="]";
            runtime.evaluate("for (const entity of "+batch+") engine.emit("+
                script_json_quote(event)+",entity)", "<entity-events>");
            batch="[";
            count=0;
        };
        for (std::size_t i=0;i<scene.objects.size();++i) {
            auto const& object=scene.objects[i];
            if (object.node.script.empty() ||
                (dt>=0.f && scene_world && !scene_world->object_resident(i))) continue;
            if (count) batch+=',';
            batch+=script_object_payload(object,i,dt);
            ++count;
            if (count==256 || batch.size()>=65536) flush();
        }
        flush();
    }

    // Watch only source projects; packaged game scripts are immutable runtime
    // assets, while editor/project source scripts are hot-reloadable.
    bool scripts_changed() {
        if (config.packaged_game || config.archive_path.empty()==false ||
            script_source_root.empty()) return false;
        bool changed=false;
        for (auto& [relative,stamp]:script_file_stamps) {
            std::error_code ec;
            auto next=fs::last_write_time(script_source_root/relative,ec);
            if (ec) next=fs::file_time_type::min();
            if (next!=stamp) { stamp=next; changed=true; }
        }
        return changed;
    }

    void reload_project_scripts() {
        if (!script_runtime) return;
        // Loading a candidate JS context may invoke engine.animation.play().
        // Preserve the previous animation state so failed hot reload cannot
        // destroy the active gameplay simulation.
        auto old_animations=script_animations
            ? std::make_unique<SceneAnimationPlayer>(*script_animations)
            : nullptr;
        auto old_visuals=script_animation_visuals;
        try {
            script_entry_exists=fs::is_regular_file(script_source_root/"scripts"/"main.js");
            auto fresh=std::make_unique<ScriptRuntime>();
            configure_script_bindings(*fresh);
            load_script_files(*fresh);
            fresh->emit_event("start");
            notify_script_entities(*fresh,"entity.start");
            auto new_animations=script_animations
                ? std::make_unique<SceneAnimationPlayer>(*script_animations)
                : nullptr;
            try {
                notify_script_entities(*script_runtime,"entity.stop");
                script_runtime->emit_event("stop");
            } catch (...) { /* old scripts cannot veto a successful reload */ }
            if (new_animations) *script_animations=std::move(*new_animations);
            script_animation_samples.clear();
            script_animation_visuals.clear();
            script_runtime.swap(fresh);
            script_suspended=false;
            if (script_logger)
                script_logger->info("project.script","Project scripts hot reloaded");
        } catch (std::exception const& e) {
            if (old_animations && script_animations)
                *script_animations=std::move(*old_animations);
            script_animation_visuals=std::move(old_visuals);
            // A failed edit must not destroy the currently running context.
            OutputDebugStringA(e.what());
            std::fprintf(stderr,"Project script reload rejected: %s\n",e.what());
        }
    }

    void stop_project_scripts() {
        script_audio_cache.clear();
        script_object_colors.clear();
        script_paused=false;
        script_move_x=script_move_z=0.f;
        if (script_runtime) {
            try {
                notify_script_entities(*script_runtime,"entity.stop");
                script_runtime->emit_event("stop");
            } catch (std::exception const&) {}
        }
        script_runtime.reset();
        script_animations.reset();
        script_animation_samples.clear();
        script_animation_visuals.clear();
        script_ecs.reset();
        script_assets.reset();
        script_entity_indices.clear();
        script_handle_ids.clear();
        script_file_stamps.clear();
        script_source_root.clear();
        script_entry_exists=false;
        script_suspended=false;
    }

    void initialize_project_scripts() {
        if (!preview || file_path.empty()) return;
        bool const archived=!config.archive_path.empty();
        auto const base=archived ? fs::path{} : script_project_root(file_path);
        script_assets=std::make_unique<AssetStore>();
        if (archived) {
            script_assets->mount_archive("",config.archive_path,
                config.archive_data.empty() ? config.archive_path : config.archive_data);
            // Missing optional main.js is not a failure; attached entity scripts
            // are still allowed to start without a global entrypoint.
            try {
                auto const bytes=script_assets->size("scripts/main.js");
                if (bytes>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                    throw std::length_error("Archived main.js exceeds 1 MiB");
                script_entry_exists=true;
            } catch (std::runtime_error const&) {
                script_entry_exists=false;
            }
        } else {
            auto const entry=base/"scripts"/"main.js";
            script_entry_exists=fs::is_regular_file(entry);
            if (fs::is_symlink(base/"scripts") ||
                (script_entry_exists && fs::is_symlink(entry)))
                throw std::runtime_error("Project script paths may not be symbolic links");
            script_assets->mount_directory("",base);
            if (!fps_assets && fs::is_directory(base/"assets")) {
                fps_assets=std::make_unique<AssetManager>();
                fps_assets->mount_directory("",base/"assets");
            }
        }
        auto const& scene=preview->scene();
        bool attached=false;
        for (auto const& object:scene.objects)
            if (!object.node.script.empty()) attached=true;
        if (!script_entry_exists && !attached) {
            script_assets.reset();
            return;
        }
        script_animations=std::make_unique<SceneAnimationPlayer>();
        script_ecs=std::make_unique<EcsWorld>();
        script_ecs->load(scene);
        for (std::size_t i=0;i<scene.objects.size();++i)
            if (scene.objects[i].node.id) {
                auto stable=scene.objects[i].node.id;
                script_entity_indices.emplace(stable,i);
                script_handle_ids.emplace(script_ecs->at(i),stable);
            }
        if (!archived) {
            script_source_root=base;
            auto const entry=base/"scripts"/"main.js";
            script_file_stamps.emplace(fs::path("scripts/main.js"),
                script_entry_exists ? fs::last_write_time(entry) : fs::file_time_type::min());
        }
        for (auto const& object:scene.objects)
            if (!object.node.script.empty()) {
                auto const rel=fs::u8path(object.node.script);
                validate_script_virtual_path(rel);
                if (archived) {
                    auto const bytes=script_assets->size(object.node.script);
                    if (bytes>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                        throw std::length_error("Archived entity script exceeds 1 MiB");
                } else {
                    auto const actual=validated_script_file(base,rel);
                    script_file_stamps.emplace(rel,fs::last_write_time(actual));
                }
            }
        try {
            auto const logging=application_logger_configuration("NorthStarCpp-ProjectScripts");
            script_logging_allowed=logging.enabled;
            if (logging.enabled)
                script_logger=std::make_unique<Logger>(logging.options);
        } catch(std::exception const& e) { OutputDebugStringA(e.what()); }
        auto fresh=std::make_unique<ScriptRuntime>();
        configure_script_bindings(*fresh);
        load_script_files(*fresh);
        fresh->emit_event("start");
        notify_script_entities(*fresh,"entity.start");
        script_runtime=std::move(fresh);
    }

    void advance_scene_animations(float dt) {
        if (!script_animations || !preview) return;
        if (!script_paused && !script_suspended) {
            auto const delta=std::clamp(dt,0.f,0.25f);
            script_animation_samples=script_animations->advance(delta);
            for (auto const& sample:script_animation_samples) {
                if (sample.scene_index>=preview->scene().objects.size())
                    throw std::runtime_error("Animation references stale scene object");
                // Timeline continues offscreen, but no physical relocation or
                // render override is submitted for a partition-unloaded cell.
                if (scene_world && !scene_world->object_resident(sample.scene_index))
                    continue;
                auto const& obj=preview->scene().objects[sample.scene_index];
                if (obj.node.id!=sample.entity_id || obj.mass!=0.f || obj.player)
                    throw std::runtime_error("Animated scene object topology changed");
                if (std::abs(obj.position.x-sample.position.x)>0.00001f ||
                    std::abs(obj.position.y-sample.position.y)>0.00001f ||
                    std::abs(obj.position.z-sample.position.z)>0.00001f) {
                    preview->move_static(sample.scene_index,sample.position);
                    if (script_ecs && sample.scene_index<script_ecs->size()) {
                        NsPosition next{sample.position.x,sample.position.y,sample.position.z};
                        script_ecs->set_component(script_ecs->at(sample.scene_index),
                                                  NS_COMPONENT_POSITION,&next,sizeof(next));
                    }
                }
                // Visual rotation and scale do not change collision shapes until
                // the physics-provider transform/shape ABI is extended.
                script_animation_visuals.insert_or_assign(sample.scene_index,sample);
            }
        }
    }

    void apply_scene_animation_visuals(Scene& visual) const {
        for (auto const& entry:script_animation_visuals) {
            auto const index=entry.first;
            if (index>=visual.objects.size()) continue;
            auto& object=visual.objects[index];
            object.node.rotation=entry.second.rotation;
            object.node.scale=entry.second.scale;
        }
    }

    void frame()
    {
        float seconds = static_cast<float>(frame_clock.restart_seconds());
        if (fps || scripted_first_person) seconds = std::clamp(seconds, 0.f, 0.05f);
        auto const state = input->poll();
        script_mouse_dx=state.mouse_dx;
        script_mouse_dy=state.mouse_dy;
        std::copy_n(std::begin(state.mouse_buttons),script_mouse_buttons.size(),
                    script_mouse_buttons.begin());
        std::copy_n(std::begin(state.gamepads),script_gamepads.size(),
                    script_gamepads.begin());
        if (scripted_first_person && (GetForegroundWindow()!=window ||
            (!fps_captured && !editor))) {
            script_mouse_dx=script_mouse_dy=0.f;
        }
        if (scripted_game) {
            script_move_x=script_move_z=0.f; // no stale movement from prior frames
        }
        if (script_runtime) {
            std::copy_n(std::begin(state.keys),script_keys.size(),script_keys.begin());
            auto const scan_now=Clock::now();
            if (!config.packaged_game &&
                scan_now-script_probe_last>=std::chrono::milliseconds(500)) {
                script_probe_last=scan_now;
                if (scripts_changed()) reload_project_scripts();
            }
            if (!script_suspended) try {
                // PhysicsScene is the live transform source. Query/has only use
                // ECS component presence, so avoid O(N) ECS writes every frame.
                script_runtime->emit_event("update",std::to_string(seconds));
                notify_script_entities(*script_runtime,"entity.update",seconds);
            } catch (std::exception const& e) {
                OutputDebugStringA(e.what());
                std::fprintf(stderr,"Project script update failed: %s\n",e.what());
                script_suspended=true;
            }
        }
        if (script_animations && !script_suspended) {
            try { advance_scene_animations(seconds); }
            catch(std::exception const& error) {
                OutputDebugStringA(error.what());
                std::fprintf(stderr,"Project animation update failed: %s\n",error.what());
                script_suspended=true;
            }
        }
        auto const down = [&state](unsigned code) {
            return code < NORTHSTARCPP_INPUT_MAX_KEYS && state.keys[code] != 0;
        };
        bool const foreground = GetForegroundWindow() == window && state.focused;
        if (config.abby && fps_camera) {
            bool const toggle = foreground && down(VK_F5);
            if (toggle && !abby_camera_key) {
                auto const mode = fps_camera->mode() == CameraMode::first_person
                    ? CameraMode::third_person : CameraMode::first_person;
                fps_camera->set_mode(mode,0.f);
            }
            abby_camera_key = toggle;
        }
        float x = 0.f, z = 0.f;
        if (fps) {
            bool const pause_key = down(NORTHSTAR_INPUT_KEY_ESCAPE) ||
                std::any_of(std::begin(state.gamepads),std::end(state.gamepads),
                    [](auto const& pad) {
                        return pad.connected && (pad.buttons & NORTHSTAR_INPUT_PAD_START);
                    });
            if (pause_key && !fps_previous_menu && foreground &&
                (!fps_menu_visible || (fps->alive() && !fps->won())))
                show_fps_menu(!fps_menu_visible);
            fps_previous_menu = pause_key;
        }

        if (preview && foreground && !scripted_game &&
            (!fps || !fps_menu_visible)) {
            x = float(int(down('D') || down(NORTHSTAR_INPUT_KEY_RIGHT)) -
                      int(down('A') || down(NORTHSTAR_INPUT_KEY_LEFT)));
            z = float(int(down('W') || down(NORTHSTAR_INPUT_KEY_UP)) -
                      int(down('S') || down(NORTHSTAR_INPUT_KEY_DOWN)));
            float look_x = 0.f, look_y = 0.f;
            bool trigger = false, pad_jump = false, pad_reload = false;
            bool stop = !fps && down(NORTHSTAR_INPUT_KEY_ESCAPE);
            for (auto const& pad : state.gamepads) {
                if (!pad.connected) continue;
                stop = stop || (!fps && (pad.buttons & NORTHSTAR_INPUT_PAD_BACK) != 0);
                x = std::clamp(x + pad.left_x +
                    float(!!(pad.buttons & NORTHSTAR_INPUT_PAD_DPAD_RIGHT)) -
                    float(!!(pad.buttons & NORTHSTAR_INPUT_PAD_DPAD_LEFT)), -1.f, 1.f);
                z = std::clamp(z + pad.left_y +
                    float(!!(pad.buttons & NORTHSTAR_INPUT_PAD_DPAD_UP)) -
                    float(!!(pad.buttons & NORTHSTAR_INPUT_PAD_DPAD_DOWN)), -1.f, 1.f);
                look_x = pad.right_x;
                look_y = pad.right_y;
                trigger = pad.right_trigger > 0.35f;
                pad_jump = !!(pad.buttons & NORTHSTAR_INPUT_PAD_A);
                pad_reload = !!(pad.buttons & NORTHSTAR_INPUT_PAD_X);
                break;
            }
            if (stop) {
                if (editor) command(play_scene, BN_CLICKED);
                else { DestroyWindow(window); return; }
                x = z = 0.f;
            }
            if (fps && fps_captured && fps->alive()) {
                fps->look(float(state.mouse_dx),float(state.mouse_dy),
                          look_x,look_y,seconds);
                bool const jumping = down(VK_SPACE) || pad_jump;
                if (jumping && !fps_previous_jump)
                    preview->jump();
                fps_previous_jump = jumping;
                if (config.explore) {
                    bool const use=down('E') || pad_reload;
                    if(use && !explore_previous_use) {
                        auto const origin=fps->eye(preview->scene());
                        auto const direction=fps->direction();
                        auto const end=Vector3{origin.x+direction.x*3.6f,
                                               origin.y+direction.y*3.6f,
                                               origin.z+direction.z*3.6f};
                        auto hit=preview->sweep_sphere(origin,end,0.28f,fps->player_index());
                        if(hit && hit->object_index!=UINT32_MAX &&
                           hit->object_index < preview->scene().objects.size()) {
                            auto const& body=preview->scene().objects[hit->object_index];
                            if(body.mass>0.f && !body.player)
                                preview->apply_impulse(hit->object_index,
                                    {direction.x*9.f,direction.y*9.f+1.2f,direction.z*9.f});
                        }
                    }
                    explore_previous_use=use;
                }
                bool const reload = !config.explore && (down('R') || pad_reload);
                if (reload && !fps_previous_reload && fps->begin_reload())
                    play_fps_clip(fps_reload);
                fps_previous_reload = reload;
            }
        }
        if (!preview) {
            if (editor && camera_looking) {
                float const speed=std::clamp(seconds,0.f,.05f)*camera_move_speed*
                    ((GetKeyState(VK_SHIFT)&0x8000)?4.f:1.f);
                auto down_key=[](int key){return (GetAsyncKeyState(key)&0x8000)!=0;};
                float const sideways=float(down_key('D'))-float(down_key('A'));
                float const vertical=float(down_key('E'))-float(down_key('Q'));
                float const forward=float(down_key('W'))-float(down_key('S'));
                editor_camera.move(sideways*speed,vertical*speed,forward*speed);
            }
            update_scene_world(seconds);
            auto active_scene=gizmo_scene ? *gizmo_scene : document.snapshot();
            auto const overlay=editor ? std::optional<ViewportGizmo>(active_gizmo(active_scene)) :
                std::optional<ViewportGizmo>{};
            graphics->draw(active_scene, selected_index(), false,
                           nullptr,nullptr,false,editor ? &editor_camera.pose : nullptr,
                           scene_world.get(),true,show_streaming_overlay,
                           overlay ? &*overlay : nullptr);
            return;
        }
        if (scripted_first_person) {
            if (!script_paused && !script_suspended)
                preview->step(seconds,script_move_x,script_move_z);
            update_script_camera(script_paused ? 0.f : seconds);
            Scene visual=preview->scene();
            for (auto const& [index,color]:script_object_colors)
                if (index<visual.objects.size()) visual.objects[index].color=color;
            apply_scene_animation_visuals(visual);
            update_scene_world(seconds);
            update_abby(visual,script_yaw,script_paused ? 0.f : seconds);
            graphics->draw(visual,-2,false,preview.get(),nullptr,script_paused,
                           fps_camera ? &fps_pose : nullptr,scene_world.get(),false);
        } else if (fps) {
            if (!fps_menu_visible && foreground && fps->alive()) {
                fps->advance(seconds,preview->scene());
                for (auto const index : fps->moving_indices())
                    preview->move_static(index, fps->moving_position(index));
                auto const m = fps->movement(x,z);
                preview->step(seconds,m.x,m.z);
                if (!config.explore && (state.mouse_buttons[0] ||
                    std::any_of(std::begin(state.gamepads),std::end(state.gamepads),
                        [](auto const& p) {return p.connected && p.right_trigger > 0.35f;})))
                {
                    auto const before = fps->shots();
                    fps->fire(preview->scene());
                    if (fps->shots() != before) {
                        if (fps_camera) fps_camera->shake(0.012f, 0.12f);
                        play_fps_clip(fps_shot);
                    }
                }
                if (!fps->alive() || fps->won()) show_fps_menu(true);
            }
            update_fps_camera(fps_menu_visible ? 0.f : seconds,
                              foreground && !fps_menu_visible && state.mouse_buttons[1]);
            Scene visual = preview->scene();
            fps->tint(visual);
            update_scene_world(seconds);
            update_abby(visual,fps->yaw(),fps_menu_visible ? 0.f : seconds);
            graphics->draw(visual,-2,false,preview.get(),fps.get(),fps_menu_visible,
                           fps_camera ? &fps_pose : nullptr,scene_world.get(),
                           !config.explore);
            update_fps_title();
        } else {
            preview->step(seconds,scripted_game ? script_move_x : x,
                                   scripted_game ? script_move_z : z);
            update_scene_world(seconds);
            Scene visual=preview->scene();
            for (auto const& [index,color]:script_object_colors)
                if (index<visual.objects.size()) visual.objects[index].color=color;
            apply_scene_animation_visuals(visual);
            graphics->draw(visual,-2,false,preview.get(),
                           nullptr,false,nullptr,scene_world.get(),
                           true,show_streaming_overlay && editor);
        }
        update_asset_zones();
    }

    void smoke()

    {
        auto click = [this](int id) { command(id, BN_CLICKED); };
        if (editor) {
            click(add_object);
            click(add_object);
            click(delete_object);
            if (!valid_selection() || document.size() != 1)
                throw std::runtime_error("Editor add/delete commands failed");
            SetWindowTextW(edits[0], L"Smoke Player");
            SetWindowTextW(edits[1], L"2.5");
            SetWindowTextW(edits[2], L"2");
            SetWindowTextW(edits[5], L"1.5");
            SendMessageW(player, BM_SETCHECK, BST_CHECKED, 0);
            click(apply_properties);
            if (document.object(document.at(0)).name != "Smoke Player" ||
                document.object(document.at(0)).position.x != 2.5f ||
                !document.object(document.at(0)).player)
                throw std::runtime_error("Editor inspector did not commit the scene");
            graphics->draw(document.snapshot(), selected_index(), true);
            file_path = config.smoke_output;
            save(false);
            click(play_scene);
        }
        auto const movable = [](SceneObject const& object) { return object.player; };
        if (!preview)
            throw std::runtime_error("Play mode failed to start");
        auto const& objects = preview->scene().objects;
        auto const item = std::find_if(objects.begin(), objects.end(), movable);
        if (item == objects.end())
            throw std::runtime_error("Smoke scene has no player");
        auto const start = item->position;
        preview->step(0.05f, 1.f, 0.f);
        if (!(item->position.x > start.x && item->position.y < start.y))
            throw std::runtime_error("Physics provider did not move the player under input and gravity");
        for (int i = 0; i < 120; ++i)
            preview->step(1.f / 60.f);
        if (std::abs(item->position.y) > 0.035f)
            throw std::runtime_error("Physics player did not settle on the ground");
        graphics->draw(preview->scene(), -2, true, preview.get());
        if (editor) {
            click(play_scene);
            if (preview || document.object(document.at(0)).position.x != start.x ||
                document.object(document.at(0)).position.y != start.y || dirty)
                throw std::runtime_error("Play mode changed the editor document");
        }
    }

    void report(std::string const& error) const
    {
        auto message = to_wide(error);
        if (message.empty())
            message.assign(error.begin(), error.end());
        message += L"\n";
        OutputDebugStringW(message.c_str());
        std::fprintf(stderr, "%s\n", error.c_str());
        if (!config.smoke && !config.check_scene)
            MessageBoxW(window, message.c_str(), L"NorthStarCpp", MB_OK | MB_ICONERROR);
    }

    Options config;
    std::unique_ptr<ScriptRuntime> script_runtime;
    std::unique_ptr<SceneAnimationPlayer> script_animations;
    std::vector<SceneAnimationSample> script_animation_samples;
    std::unordered_map<std::size_t,SceneAnimationSample> script_animation_visuals;
    std::unique_ptr<AssetStore> script_assets;
    std::unique_ptr<EcsWorld> script_ecs;
    std::unordered_map<std::uint64_t,std::size_t> script_entity_indices;
    std::unordered_map<NsEntity,std::uint64_t> script_handle_ids;
    fs::path script_source_root;
    std::map<fs::path,fs::file_time_type> script_file_stamps;
    Clock::time_point script_probe_last{};
    bool script_entry_exists=false;
    bool script_suspended=false;
    std::wstring script_window_title;
    bool scripted_game=false;
    bool scripted_first_person=false;
    bool script_paused=false;
    float script_move_x=0.f, script_move_z=0.f;
    float script_yaw=0.f, script_pitch=0.f, script_eye_height=1.45f;
    float script_mouse_dx=0.f, script_mouse_dy=0.f;
    std::array<std::uint8_t,5> script_mouse_buttons{};
    std::array<NorthStarInputGamepad,NORTHSTARCPP_INPUT_MAX_GAMEPADS> script_gamepads{};
    std::unordered_map<std::size_t,std::uint32_t> script_object_colors;
    std::unordered_map<std::string,FpsAudioClip> script_audio_cache;
    bool script_logging_allowed = true;
    std::unique_ptr<Logger> script_logger;
    std::unique_ptr<PerlinNoise> script_noise;
    std::array<std::uint8_t,NORTHSTARCPP_INPUT_MAX_KEYS> script_keys{};
    HWND window = nullptr, surface = nullptr, list = nullptr;
    HWND player = nullptr, collision_flag = nullptr, inspector_tabs = nullptr;
    HACCEL accelerators = nullptr;
    HWND apply = nullptr, status = nullptr;
    HWND outliner_title=nullptr, outliner_search=nullptr, details_title=nullptr;
    HWND viewport_title=nullptr, content_title=nullptr;
    HWND local_button=nullptr, snap_button=nullptr, step_edit=nullptr;
    HWND bind_button=nullptr, place_button=nullptr, reveal_button=nullptr;
    std::array<HWND,3> gizmo_buttons{};
    std::unique_ptr<EditorContentBrowser> content_browser;
    std::vector<std::size_t> outliner_indices;
    editor_tools::GizmoMode gizmo_mode=editor_tools::GizmoMode::translate;
    editor_tools::GizmoOptions gizmo_settings{};
    bool content_visible=false;
    int left_width=240,right_width=295,content_height=250;
    enum class Splitter {none,left,right,bottom};
    Splitter splitter_drag=Splitter::none;
    editor_tools::EditorCamera editor_camera{};
    bool camera_looking=false;
    ScreenPoint camera_pointer{};
    float camera_move_speed=8.f;
    bool gizmo_dragging=false;
    int gizmo_drag_axis=-1;
    float gizmo_drag_amount=0.f;
    ScreenPoint gizmo_start{};
    Scene gizmo_baseline;
    std::optional<Scene> gizmo_scene;
    int inspector_page = 0;
    editor_tools::History history;
    std::array<HWND, 8> buttons{};
    std::array<HWND, 6> labels{}, edits{};
    std::unique_ptr<Viewport> graphics;
    EcsWorld document;
    std::unique_ptr<SceneWorld> scene_world;
    std::unique_ptr<SceneNavigationCells> navigation_cells;
    std::unique_ptr<AssetManager> scene_assets;
    // Destroy before scene_assets: pending range reads reference its provider.
    std::unique_ptr<SceneCellAsyncResidency> scene_cell_residency;
    fs::path scene_asset_source;
    std::map<std::string,AssetRequest<shared_assets::DrawableResources>> scene_asset_requests;
    std::unordered_set<std::string> scene_asset_completed;
    std::unordered_set<std::string> scene_asset_failed;
    Vector3 stream_previous{0.f,0.f,0.f};
    bool stream_previous_valid=false;
    bool show_streaming_overlay=false;
    std::unique_ptr<PhysicsScene> preview;
    std::unique_ptr<AudioOutput> audio;
    std::unique_ptr<InputDevice> input;
    std::unique_ptr<FpsArena> fps;
    std::unique_ptr<AssetManager> fps_assets;
    std::unique_ptr<AssetManager> zone_assets;
    std::unique_ptr<AssetZones> asset_zones;
    FpsTuning fps_tuning;
    AssetRequest<FpsAudioClip> fps_shot, fps_reload;
    CameraSettings fps_camera_settings{};
    std::optional<Vector3> abby_previous;
    bool abby_camera_key = false;
    std::unique_ptr<CameraDirector> fps_camera;
    CameraPose fps_pose{};
    HWND fps_caption = nullptr;
    std::array<HWND, 3> fps_buttons{};
    bool fps_captured = false, fps_menu_visible = false;
    bool fps_previous_menu = false, fps_previous_jump = false,
         fps_previous_reload = false;
    bool explore_previous_use = false;
    fs::path file_path;
    NsEntity selected = 0;
    int exit_status = 0;
    bool dirty = false;
    timing::FrameClock frame_clock = [] {
        timing::FrameClock clock;
        clock.start();
        return clock;
    }();
};

} // namespace
} // namespace northstarcpp

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    northstarcpp::diagnostics::install_crash_handler();
    bool smoke = false;
    bool arguments_parsed = false;
    std::unique_ptr<northstarcpp::Logger> process_logger;
    try {
        auto settings = northstarcpp::options();
        arguments_parsed = true;
        smoke = settings.smoke || settings.check_scene || settings.runtime_init_smoke ||
                settings.fps_ui_smoke || settings.abby_smoke || settings.check_shared_assets ||
                settings.inspect_providers;
        // Test/validation runs should not create logs in the user's profile.
        if (!smoke || northstarcpp::application_log_directory_overridden()) {
            try {
                auto const logging = northstarcpp::application_logger_configuration(
                    NORTHSTARCPP_EDITOR ? "NorthStarCpp-Editor" : "NorthStarCpp-Player");
                if (logging.enabled) {
                    northstarcpp::prepare_application_log_console(logging);
                    process_logger = std::make_unique<northstarcpp::Logger>(logging.options);
                    process_logger->info("engine", NORTHSTARCPP_EDITOR
                        ? "NorthStarCpp editor starting" : "NorthStarCpp game player starting");
                }
            } catch (std::exception const& error) {
                std::fprintf(stderr, "Logger initialization: %s\n", error.what());
                OutputDebugStringA(error.what());
            }
        }
        if (settings.inspect_providers) {
            // Same ABI negotiation and provider precedence as the Qt Editor
            // inspector. This mode never opens a window or creates a GPU device.
            std::puts("Capability\tRequested\tProvider\tVersion\tABI\tSource\tStatus");
            for (auto const& provider:northstarcpp::inspect_engine_providers()) {
                std::printf("%s\t%s\t%s\t%s\t%u\t%s\t%s\n",
                    provider.capability.c_str(),provider.requested_provider.c_str(),
                    provider.provider.c_str(),provider.version.c_str(),
                    provider.api_abi,provider.source.c_str(),
                    provider.available?"OK":provider.error.c_str());
            }
            return 0;
        }
        if (settings.check_shared_assets) {
            auto const assets = northstarcpp::shared_assets::inspect_directory(settings.shared_asset_root);
            std::printf("Shared assets: %zu materials, %zu drawables, %zu textures, %zu scripts, %zu shaders\n",
                assets.materials, assets.drawables, assets.textures, assets.scripts, assets.shaders);
            std::printf("Dependencies: %zu model materials, %zu material textures resolved; %zu skeleton files unsupported\n",
                assets.resolved_materials, assets.resolved_textures, assets.unsupported_skeletons);
            for (auto const& reference : assets.missing_references)
                std::printf("Missing asset: %s\n", reference.c_str());
            return 0;
        }
#if NORTHSTARCPP_EDITOR
        // Unreal-style entry point: the native Project Manager opens before
        // the editor unless a scene was explicitly requested.
        if (!smoke && settings.scene_path.empty()) {
            std::array<wchar_t, 32768> executable{};
            auto const count = GetModuleFileNameW(nullptr, executable.data(),
                static_cast<DWORD>(executable.size()));
            if (!count || count >= executable.size())
                throw std::runtime_error("Cannot locate editor installation");
            auto manager = std::filesystem::path(executable.data()).parent_path() /
                L"northstarcpp_project_manager.exe";
            if (!std::filesystem::is_regular_file(manager))
                manager=std::filesystem::path(executable.data()).parent_path()/
                    L"qt_tools"/L"northstarcpp_project_manager.exe";
            if (!std::filesystem::is_regular_file(manager))
                throw std::runtime_error("Native Project Manager is missing from editor directory");
            auto const started = reinterpret_cast<INT_PTR>(
                ShellExecuteW(nullptr, L"open", manager.c_str(), nullptr,
                              manager.parent_path().c_str(), SW_SHOWNORMAL));
            if (started <= 32)
                throw std::runtime_error("Unable to start native Project Manager");
            return 0;
        }
#endif
        if(settings.runtime_init_smoke) {
            // Exercise the REAL Player startup pipeline: physics, scene-v3
            // streaming mounts, zones and scripts. Unlike --check-scene,
            // this runs the App constructor but opens no window.
            northstarcpp::App initialized(std::move(settings));
            std::puts("Player runtime asset initialization: PASS");
            return 0;
        }
        int const result = northstarcpp::App(std::move(settings)).run(instance);
        if (process_logger) {
            try {
                if (result)
                    process_logger->error("engine", "Application exited with code " +
                        std::to_string(result));
                else
                    process_logger->info("engine", "Application exited with code 0");
                process_logger->flush();
            } catch (std::exception const&) {}
        }
        return result;
    } catch (std::exception const& error) {
        // Parsing options can fail before normal logger startup. Attempt an
        // emergency logger, but never let its own failure replace the real error.
        if (!process_logger && (!smoke ||
                northstarcpp::application_log_directory_overridden())) {
            try {
                auto const logging=northstarcpp::application_logger_configuration(
                    NORTHSTARCPP_EDITOR ? "NorthStarCpp-Editor" : "NorthStarCpp-Player");
                if (logging.enabled) {
                    northstarcpp::prepare_application_log_console(logging);
                    process_logger=std::make_unique<northstarcpp::Logger>(logging.options);
                }
            } catch(std::exception const& logging_error) {
                std::fprintf(stderr,"Logger emergency initialization: %s\n",
                             logging_error.what());
            }
        }
        if (process_logger) {
            try {
                process_logger->fatal("engine", error.what());
                process_logger->flush();
            } catch (std::exception const&) {}
        }
        OutputDebugStringA(error.what());
        std::fprintf(stderr, "%s\n", error.what());
        // Invalid command lines fail immediately in CI/terminal contexts.
        // A modal dialog here would deadlock unattended release verification.
        if (!smoke && arguments_parsed)
            MessageBoxA(nullptr, error.what(), "NorthStarCpp", MB_OK | MB_ICONERROR);
        return 1;
    }
}
