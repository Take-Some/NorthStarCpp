// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include "viewport.hpp"
#include "editor_tools.hpp"
#include "qt_pie_protocol.hpp"
#include "qt_pie_command_state.hpp"
#include "editor_prefab.hpp"
#include "editor_visual_prefab.hpp"
#include "editor_camera.hpp"
#include "content_browser.hpp"
#include "window_scene_streaming.hpp"
#include "window_runtime_options.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include "crash_handler.hpp"
#include "../shared/frame_profiler.hpp"
#include "../shared/startup_report.hpp"
#include "resource_ids.h"

#include <northstarcpp/physics.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/player_controller.hpp>
#include <northstarcpp/player_camera_runtime.hpp>
#include <northstarcpp/physics_surface.hpp>
#include <northstarcpp/audio.hpp>
#include <northstarcpp/nac.hpp>
#include <northstarcpp/logger.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/input.hpp>
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/camera_cutscene.hpp>
#include <northstarcpp/camera_track.hpp>
#include <northstarcpp/fps_assets.hpp>
#include <northstarcpp/ecs.hpp>
#include <northstarcpp/scripting.hpp>
#include "../host/engine_script_bootstrap.hpp"
#include <northstarcpp/save_game.hpp>
#include <fstream>
#include <northstarcpp/scene_animation.hpp>
#include <northstarcpp/frame_clock.hpp>
#include <northstarcpp/provider_inspector.hpp>
#include <northstarcpp/perlin.hpp>
#include <northstarcpp/vehicle_service.hpp>
#include "../host/vehicle_rig_xml.hpp"
#include <boost/property_tree/json_parser.hpp>

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
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

using runtime_options::Options;
using runtime_options::discover_shared_assets;
Options options() { return runtime_options::parse_options(editor); }

Scene read_document(fs::path const& path)
{
    AssetManager assets;
    auto const absolute = fs::absolute(path);
    assets.mount_directory("", absolute.parent_path());
    return *assets.request_scene(absolute.filename().u8string()).get();
}


using runtime_script_contracts::script_args;
using runtime_script_contracts::parse_script_ui_document;
using runtime_script_contracts::script_index;
using runtime_script_contracts::script_number;
using runtime_script_contracts::parse_script_character;

using runtime_project_files::validate_script_virtual_path;
using runtime_project_files::validated_script_file;
using runtime_project_files::script_project_root;
using runtime_project_files::project_save_store;
using runtime_project_files::script_json_quote;
using runtime_project_files::script_object_payload;

class App {
private:
    // Defined before the constructor because placeholder return-type
    // deduction is not available until this template definition is seen.
    template<class F>
    decltype(auto) bootstrap_stage(char const* phase,F&& fn) {
        if(startup_report_)
            return startup_report_->measure(phase,std::forward<F>(fn));
        return std::forward<F>(fn)();
    }
public:
    explicit App(Options settings, diagnostics::StartupReport* startup_report=nullptr)
        : config(std::move(settings)), startup_report_(startup_report)
    {
        auto const profile_frames = diagnostics::FrameProfiler::requested_frames();
        if (profile_frames)
            frame_profiler = std::make_unique<diagnostics::FrameProfiler>(profile_frames);
        file_path = config.scene_path;
        bootstrap_stage("scene.load", [&] {
            if (config.new_document && fs::exists(file_path))
                config.new_document = false;
            if (!file_path.empty() && !config.new_document)
                document.load(load_startup_scene());
            else if (!config.smoke || !editor)
                document.load(make_startup_scene());
        });
        if(!editor && !config.new_document && !file_path.empty())
            bootstrap_stage("scene.visual_prefabs",[&] {
                auto candidate=document.snapshot();
                auto const refs=candidate.version==3 ?
                    editor::visual_prefab::links(candidate) :
                    std::vector<std::pair<std::uint64_t,std::string>>{};
                if(refs.empty())return;
                AssetStore store;
                if(config.archive_path.empty()) {
                    auto base=script_project_root(file_path);
                    store.mount_project_assets(base,
                        discover_shared_assets(config,base));
                }else {
                    store.mount_runtime_archives();
                    store.mount_archive("",config.archive_path,
                        config.archive_data.empty() ?
                            config.archive_path : config.archive_data);
                }
                std::size_t applied=0;
                for(auto const& [id,ref]:refs) {
                    try {
                        auto bytes=store.read(ref);
                        if(bytes.size()>4u*1024u*1024u)
                            throw std::length_error("Visual prefab exceeds 4 MiB");
                        auto prefab=parse_scene_data(
                            std::string(bytes.begin(),bytes.end()));
                        auto proposed=candidate;
                        editor::visual_prefab::apply(proposed,id,prefab);
                        candidate=std::move(proposed);
                        ++applied;
                    }catch(std::exception const& e) {
                        bootstrap_warning("scene.visual_prefabs",
                            "Unable to resolve "+ref+": "+e.what());
                    }
                }
                if(applied)document.load(std::move(candidate));
            });
        dirty = config.new_document;
        if(startup_report_) {
            auto const snapshot=document.snapshot();
            startup_report_->info("scene.summary",
                "version="+std::to_string(snapshot.version)+
                " objects="+std::to_string(snapshot.objects.size())+
                " components="+std::to_string(snapshot.components.size())+
                " cells="+std::to_string(snapshot.cells.size())+
                " layers="+std::to_string(snapshot.layers.size()));
        }
        bootstrap_stage("script.discovery", [&] {
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
                auto const source=document.snapshot();
                scripted_game=std::any_of(source.objects.begin(),source.objects.end(),
                    [&](SceneObject const& object) {
                        return scene_pawn::is_local(source,object) ||
                               !object.node.script.empty();
                    });
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
        });
        if(startup_report_)
            startup_report_->info("script.mode",
                std::string("scripted=")+(scripted_game?"true":"false")+
                " first_person="+(scripted_first_person?"true":"false"));
        if (!editor) {
            auto initial_scene = document.snapshot();
            if (config.fps) bootstrap_stage("fps.assets", [&] {
                // A FPS project can supply its data through engine.assets.
                // Both source projects and exported games contain <scene>/assets/.
                // Data is never read directly from disk by the gameplay subsystem.
                if (config.archive_path.empty() && !file_path.empty()) {
                    fs::path const asset_root = fs::absolute(file_path).parent_path() / "assets";
                    if (fs::exists(asset_root)) {
                        if (!fs::is_directory(asset_root))
                            throw std::runtime_error("FPS asset root is not a directory");
                        fps_assets = std::make_unique<AssetManager>();
                        auto const base=script_project_root(file_path);
                        fps_assets->mount_project_assets(base,
                            discover_shared_assets(config,base));
                        if (!scripted_game && !config.explore) {
                            auto tuning = fps_assets->request_fps_tuning("gameplay/fps.json");
                            fps_shot = fps_assets->request_audio_clip("audio/shot.wav", AssetPriority::background);
                            fps_reload = fps_assets->request_audio_clip("audio/reload.wav", AssetPriority::background);
                            try { fps_tuning = *tuning.get(); }
                            catch(std::exception const& e) {
                                bootstrap_warning("fps.assets",
                                    std::string("Skipping invalid gameplay/fps.json: ")+e.what());
                            }
                        }
                        // Optional authoring camera presets do not prevent
                        // otherwise valid FPS scenes from loading.
                        bool has_camera_asset = false;
                        try { (void)fps_assets->size("gameplay/camera.json"); has_camera_asset = true; }
                        catch (std::exception const&) {}
                        if (has_camera_asset) {
                            try { fps_camera_settings=load_camera_settings(*fps_assets); }
                            catch(std::exception const& e) {
                                bootstrap_warning("fps.assets",
                                    std::string("Skipping invalid gameplay/camera.json: ")+e.what());
                            }
                        }
                    }
                }
                if (!scripted_first_person)
                    fps = std::make_unique<FpsArena>(initial_scene,
                        fps_tuning, config.explore);
            });
            bootstrap_stage("physics.provider", [&] {
                preview=std::make_unique<PhysicsScene>(initial_scene);
                if(scripted_first_person && preview->controlled_entity()) {
                    auto const rig=gameplay::camera_rig(preview->scene(),
                        preview->controlled_entity());
                    script_eye_height=rig.eye_height;
                    fps_camera_settings.default_fov_y=rig.fov_degrees;
                }
            });
            if(startup_report_)
                startup_report_->info("physics.provider",
                    "selected="+preview->provider_name()+
                    " version="+preview->provider_version());
            bootstrap_stage("streaming.zones", [&] {
                try {
                    initialize_asset_zones();
                } catch(std::exception const& e) {
                    bootstrap_warning("streaming.zones",
                        std::string("Skipping invalid streaming zone manifest: ")+e.what());
                    asset_zones.reset();
                    zone_assets.reset();
                }
            });
            if(startup_report_)
                startup_report_->info("streaming.zones",
                    std::string("enabled=")+(asset_zones?"true":"false"));
            if (fps || scripted_first_person) bootstrap_stage("camera.initialize", [&] {
                fps_camera = std::make_unique<CameraDirector>(fps_camera_settings);
                // Camera mode is chosen by project scripts, not by the Player.
                if (fps) update_fps_camera(0.f, false);
                else update_script_camera(0.f);
            });
            bootstrap_stage("streaming.update_initial", [&] {update_asset_zones();});
        }
        bootstrap_stage("scene.partition", [&] {rebuild_scene_world();});
        if(startup_report_)
            startup_report_->info("scene.partition",
                std::string("enabled=")+(scene_streaming.world()?"true":"false")+
                " asset_store="+(scene_streaming.assets()?scene_streaming.assets()->provider_name():"none"));
        if (!editor && !config.smoke && !config.check_scene && !config.fps_ui_smoke &&
            !file_path.empty())
            bootstrap_stage("scripts.initialize", [&] {initialize_project_scripts();});
        if(startup_report_) {
            startup_report_->info("scripts.provider",
                std::string("state=")+(script_runtime?"running":"inactive")+
                (script_runtime?
                    " selected="+script_runtime->provider_name()+
                    " version="+script_runtime->provider_version():""));
            startup_report_->info("ecs.provider",
                std::string("state=")+(script_ecs?"running":"inactive")+
                (script_ecs?" selected="+script_ecs->provider_name()+
                    " entities="+std::to_string(script_ecs->size()):""));
            startup_report_->info("assets.provider",
                std::string("fps=")+(fps_assets?fps_assets->provider_name():"none")+
                " zones="+(zone_assets?zone_assets->provider_name():"none")+
                " scripts="+(script_assets?script_assets->provider_name():"none"));
            startup_report_->info("audio.provider",
                "initialization=deferred-until-first-play");
        }
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
        bootstrap_stage("window.classes", [&] {
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
        });
        bootstrap_stage("window.create", [&] {
        // In Play In Editor mode the Player still owns its GPU swapchain,
        // world and input, but its native HWND is parented to the full
        // Editor overlay. Never request exclusive fullscreen or a popup.
        RECT target{0,0,1450,870};
        if(config.embed_parent)GetClientRect(config.embed_parent,&target);
        window = CreateWindowExW(config.embed_parent?0:WS_EX_CONTROLPARENT,
            window_class,
            editor ? L"NorthStarCpp Editor" : L"NorthStarCpp Player",
            config.embed_parent?(WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN):
                                (WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN),
            config.embed_parent?0:CW_USEDEFAULT,
            config.embed_parent?0:CW_USEDEFAULT,
            config.embed_parent?std::max(1L,target.right-target.left):1450,
            config.embed_parent?std::max(1L,target.bottom-target.top):870,
            config.embed_parent,nullptr,instance,this);
        if (!window)
            throw std::runtime_error("Cannot create application window");
        });

        try {
            bootstrap_stage("input.provider", [&] {
                input=std::make_unique<InputDevice>(reinterpret_cast<uintptr_t>(window));
            });
            if(startup_report_)
                startup_report_->info("input.provider",
                    "selected="+input->provider_name()+
                    " version="+input->provider_version());
            bootstrap_stage("window.surface", [&] {
                surface=child(viewport_class, L"", WS_CLIPSIBLINGS, 0);
            });
            if (editor) bootstrap_stage("editor.controls", [&] {
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
            });
            if (fps)
                bootstrap_stage("fps.menu", [&] {create_fps_menu();});
            bootstrap_stage("window.layout", [&] {layout();});
            bootstrap_stage("renderer.provider", [&] {
                graphics=std::make_unique<Viewport>(surface,config.warp);
            graphics->set_detail_level(graphics_detail_level);
                if(!script_ui_document_.nodes.empty())
                    graphics->set_ui_document(script_ui_document_);
                for(auto const& [id,document]:script_world_ui_documents_)
                    graphics->set_ui_document(document);
            });
            if(startup_report_)
                startup_report_->info("renderer.provider",
                    "selected="+graphics->renderer_provider_name()+
                    " version="+graphics->renderer_provider_version()+
                    " warp="+(config.warp?"true":"false"));
            if (!file_path.empty()) bootstrap_stage("renderer.asset_mounts", [&] {
                graphics->set_asset_sources(
                    config.archive_path.empty()?script_project_root(file_path):fs::path{},
                    config.archive_path,config.archive_data,
                    discover_shared_assets(config,file_path.empty()
                        ? fs::path{} : script_project_root(file_path)));
            });
            if (fps_assets) bootstrap_stage("renderer.skydome", [&] {
                bool skydome_available = false;
                try {
                    (void)fps_assets->size("models/skydome.ydd");
                    skydome_available = true;
                } catch (std::runtime_error const&) {
                    // Older FPS projects contain no authored sky.
                }
                if (skydome_available) {
                    try {
                        auto sky = fps_assets->request_drawable_resources(
                            "models/skydome.ydd@skydome_high", AssetPriority::critical).get();
                        graphics->set_sky(*sky);
                    } catch(std::exception const& e) {
                        bootstrap_warning("renderer.skydome",
                            std::string("Skipping invalid skydome asset: ")+e.what());
                    }
                }
            });
            bootstrap_stage("window.show", [&] {
                refresh();
                ShowWindow(window,config.embed_parent?SW_SHOW:SW_SHOWNORMAL);
                UpdateWindow(window);
            });
            if (!script_window_title.empty())
                SetWindowTextW(window,script_window_title.c_str());
            if (!editor && (fps || scripted_first_person)) {
                SetFocus(window);
                capture_fps_cursor();
                update_fps_title();
            }
            bootstrap_stage("frame.timer", [&] {
                if (!SetTimer(window,frame_timer,16,nullptr))
                    throw std::runtime_error("Cannot start application frame timer");
            });
            if(startup_report_)
                startup_report_->info("frame.timer","interval_ms=16");
            if (!editor && !scripted_game && !config.smoke &&
                !config.check_scene && !config.fps_ui_smoke)
                bootstrap_stage("audio.start_sound", [&] {
                    play_start_sound();
                    if(startup_report_)
                        startup_report_->info("audio.provider",
                            std::string("state=")+(audio?"active":"unavailable")+
                            (audio?" selected="+audio->provider_name():""));
                });
            if ((config.smoke || config.check_scene || config.fps_ui_smoke || config.character_smoke || !config.transition_scene.empty()) &&
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
    void bootstrap_warning(char const* phase,std::string const& message) const noexcept {
        if(startup_report_)startup_report_->warning(phase,message);
        else std::fprintf(stderr,"[WARN] [engine.assets] %s\n",message.c_str());
    }
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
        // The GPU swapchain is a child HWND. It receives pointer messages,
        // whereas gameplay input uses the main HWND. Only explicit interactive
        // UI nodes intercept these messages, and never during FPS capture.
        if(!editor && graphics && !fps_captured) {
            float const mx=static_cast<float>(GET_X_LPARAM(lp));
            float const my=static_cast<float>(GET_Y_LPARAM(lp));
            if(msg==WM_LBUTTONDOWN) {
                if(graphics->ui_hit_test(mx,my)) {
                    ui_mouse_down=true;
                    SetFocus(handle);
                    SetCapture(handle);
                    graphics->ui_pointer(mx,my,1);
                    return 0;
                }
                // A click on the viewport outside interactive UI returns
                // focus to gameplay after an OS activation or an alt-tab.
                // Consume that click so it cannot also fire a weapon.
                if ((fps || scripted_first_person) && !script_paused &&
                    !fps_menu_visible && game_has_foreground()) {
                    restore_gameplay_input_focus();
                    if (fps_captured) return 0;
                }
            } else if(msg==WM_MOUSEMOVE) {
                graphics->ui_pointer(mx,my,2);
                if(ui_mouse_down)return 0;
            } else if(msg==WM_LBUTTONUP && ui_mouse_down) {
                graphics->ui_pointer(mx,my,0);
                ui_mouse_down=false;
                if(GetCapture()==handle)ReleaseCapture();
                return 0;
            } else if(msg==WM_KEYDOWN && wp==VK_BACK) {
                if(graphics->ui_key(8,true))return 0;
            } else if(msg==WM_CHAR) {
                // WM_CHAR is decoded by Win32. Do not synthesize characters
                // from virtual keys: this preserves keyboard layout semantics.
                wchar_t code=static_cast<wchar_t>(wp);
                if(code>=0xd800 && code<=0xdfff)return 0;
                char utf8[8]{};
                int const size=WideCharToMultiByte(CP_UTF8,0,&code,1,
                    utf8,static_cast<int>(sizeof(utf8)),nullptr,nullptr);
                if(size>0 && graphics->ui_key(0,true,
                       std::string_view(utf8,static_cast<std::size_t>(size))))
                    return 0;
            }
        }
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
            // A Win32 MessageBox runs a nested message pump. If a frame
            // throws, stop dispatching WM_TIMER before opening the dialog:
            // otherwise the same failing frame can recursively open dozens
            // of modal errors and leave the Player unresponsive.
            app->runtime_faulted = true;
            KillTimer(handle, frame_timer);
            app->exit_status = 1;
            if(app->startup_report_ && !app->startup_report_->completed()) {
                app->startup_report_->failure("window.message",error.what());
                app->startup_report_->complete("FAILED");
            }
            app->report(error.what());
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
        zone_assets->mount_runtime_archives();
        auto root = config.shared_asset_root;
        if (root.empty()) {
            std::array<wchar_t, 32768> executable{};
            auto const length = GetModuleFileNameW(nullptr, executable.data(),
                static_cast<DWORD>(executable.size()));
            if (length > 0 && length < executable.size()) {
                for (auto parent = fs::path(executable.data()).parent_path(); !parent.empty();) {
                    auto const candidate = parent / "engine/northstarcpp/resources/sharedAssets";
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
        if (!root.empty() && !window_detail::same_asset_directory(root,project))
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

    void rebuild_scene_world()
    {
        scene_streaming.rebuild(document.snapshot(),
            {file_path,config.archive_path,config.archive_data,config.shared_asset_root},
            !editor || !dirty);
        update_scene_world(0.f);
    }

    void update_scene_world(Scene const& scene,float seconds)
    {
        if(!scene_streaming.world())return;
        Vector3 position{0.f,0.f,0.f};
        std::string room;
        for(auto const& object:scene.objects)
            if(scene_pawn::is_local(scene,object)) {
                position=object.position;
                room=object.node.room;
                break;
            }
        if(fps_camera) {
            position=fps_pose.position;
        } else if(editor && !preview && valid_selection()) {
            auto const& object=document.object(selected);
            position=object.position;
            room=object.node.room;
        }
        scene_streaming.update(scene,position,room,seconds,preview.get());
    }

    void update_scene_world(float seconds)
    {
        if(!scene_streaming.world())return;
        if(preview)update_scene_world(preview->authored_scene(),seconds);
        else update_scene_world(document.snapshot(),seconds);
    }

    void inspect_scene_streaming()
    {
        if (!scene_streaming.world()) {
            MessageBoxW(window,L"Enable Scene V3 to use World Streaming diagnostics.",
                        L"NorthStarCpp Streaming Inspector",MB_OK|MB_ICONINFORMATION);
            return;
        }
        auto const& data=scene_streaming.world()->streaming_diagnostics();
        const auto stream_stats=scene_streaming.stats();
        std::wstring report =
            L"World Partition / Scene V3\n"
            L"---------------------------\n"
            L"Cells loaded: " + std::to_wstring(data.loaded_cells) + L"/" +
                std::to_wstring(scene_streaming.world()->source().cells.size()) +
            L"\nPending loads: " + std::to_wstring(data.pending_loads) +
            L"\nPending unloads: " + std::to_wstring(data.pending_unloads) +
            L"\nResident entities: " + std::to_wstring(data.resident_entities) +
            L"\nVisible entities: " + std::to_wstring(data.visible_entities) +
            L"\nUpdate generation: " + std::to_wstring(data.generation) +
            L"\nAsync scene blocks pending: " +
                 std::to_wstring(stream_stats.pending_chunks) +
            L"\nDecoded scene blocks: " +
                 std::to_wstring(stream_stats.decoded_chunks) +
            L"\nAsset requests: " + std::to_wstring(stream_stats.requests) +
            L"\nNavigation cells (coarse): " +
                std::to_wstring(stream_stats.navigation_cells) +
            L"\nResolved assets: " + std::to_wstring(stream_stats.resolved) +
            L"\nFailed assets: " + std::to_wstring(stream_stats.failed) +
            L"\n\nLast cell transitions:\n";
        auto const& transitions=scene_streaming.world()->cell_transitions();
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
            std::to_wstring(scene_streaming.world()->hlod_cluster_count()) +
            L"\nActive HLOD proxies: " +
            std::to_wstring(scene_streaming.world()->hlod_proxies().size()) + L"\n";
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
                if (scene_pawn::is_local(preview->scene(),object)) {
                    position = object.position;
                    break;
                }
        }
        try {
            asset_zones->update({position.x, position.y, position.z});
        } catch(std::exception const& e) {
            std::fprintf(stderr,
                "[WARN] [engine.assets] Disabling invalid streaming zone: %s\n",
                e.what());
            asset_zones.reset(); // Prevent a warning storm on later frames.
        }
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

    void apply_script_character(Scene const& scene, float dt)
    {
        if(!graphics) return;
        if(script_character_dirty) {
            script_character_renderable=false;
            if(script_character) {
                try {
                    if(!script_assets)
                        throw std::runtime_error("No mounted character asset store");
                    graphics->load_character(*script_assets,*script_character);
                    script_character_renderable=true;
                } catch(std::exception const& e) {
                    std::fprintf(stderr,
                        "[WARN] [engine.assets] Skipping invalid scripted character: %s\n",
                        e.what());
                    // Invalid character visuals must not stop scene updates.
                    // Keep the descriptor for script pose() validation.
                    script_character_pose.reset();
                    graphics->clear_character();
                }
            } else {
                graphics->clear_character();
            }
            script_character_dirty=false;
        }
        if(!script_character || !script_character_renderable) return;
        if(!script_character_pose) {
            graphics->hide_character();
            return;
        }
        auto const& pose=*script_character_pose;
        if(pose.scene_index>=scene.objects.size()) {
            std::fprintf(stderr,
                "[WARN] [engine.assets] Skipping stale character pose index %zu\n",
                pose.scene_index);
            script_character_pose.reset();
            graphics->hide_character();
            return;
        }
        try {
            if(pose.first_person_arms && fps_camera) {
                graphics->pose_character(scene.objects[pose.scene_index].position,
                    fps_pose.yaw,dt,pose.animation,true,
                    fps_pose.position,fps_pose.pitch);
            } else {
                graphics->pose_character(scene.objects[pose.scene_index].position,
                    pose.heading,dt,pose.animation);
            }
        } catch(std::exception const& e) {
            std::fprintf(stderr,
                "[WARN] [engine.assets] Skipping invalid character animation: %s\n",
                e.what());
            script_character_pose.reset();
            graphics->hide_character();
        }
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
        auto controlled=preview->controlled_index();
        if(!controlled || *controlled>=scene.objects.size())return;
        auto const& actor=scene.objects[*controlled];
        auto index=*controlled;
        CameraTarget target{};
        target.eye=gameplay::camera_anchor(
            scene,actor.node.id,script_eye_height);
        target.yaw=script_yaw;
        target.pitch=script_pitch;
        target.heading=target.yaw;
        target.ignore_object=index;
        fps_pose=fps_camera->update(dt,target,&scene,preview.get());
        // Camera/animation/audio/dialogue/gameplay cue tracks share the same
        // director clock. Deliver one bounded batch on the V8 game thread,
        // not from a worker or renderer callback. Authored project scripts
        // decide how each cue maps to sounds/animations/subtitles.
        auto cues=fps_camera->drain_cutscene_cues_json();
        if(script_runtime && !script_suspended && cues!="[]") {
            try {script_runtime->emit_event("camera.cues",cues);}
            catch(std::exception const& error) {
                std::fprintf(stderr,"[WARN] [camera.cues] %s\n",error.what());
                script_suspended=true;
            }
        }
    }

    void update_cinematic_postfx() {
        if(!graphics || !fps_camera || cinematic_postfx_unavailable_)return;
        try {
            if(fps_camera->cutscene_lens_active()) {
                if(!graphics->supports_postfx()) {
                    cinematic_postfx_unavailable_=true;
                    std::fprintf(stderr,
                        "[WARN] [camera.lens] Render provider has no Filmic PostFX ABI\n");
                    return;
                }
                if(!cinematic_postfx_original_)
                    cinematic_postfx_original_=graphics->postfx_settings();
                if(!cinematic_postfx_original_)return;
                float const exposure=std::clamp(
                    cinematic_postfx_original_->exposure_ev+
                    fps_camera->cutscene_exposure_ev(),-5.f,5.f);
                if(std::abs(exposure-cinematic_postfx_applied_ev_)<0.001f)return;
                auto settings=*cinematic_postfx_original_;
                settings.exposure_ev=exposure;
                graphics->set_postfx_settings(settings);
                cinematic_postfx_applied_ev_=exposure;
            } else if(cinematic_postfx_original_) {
                graphics->set_postfx_settings(*cinematic_postfx_original_);
                cinematic_postfx_original_.reset();
                cinematic_postfx_applied_ev_=1000.f;
            }
        }catch(std::exception const& error) {
            std::fprintf(stderr,"[WARN] [camera.lens] %s\n",error.what());
            cinematic_postfx_unavailable_=true;
            cinematic_postfx_original_.reset();
        }
    }

    // Generic transition; the game owns scene names and scripting policy.
    // Called only after the script event completes, on the Player thread.
    void switch_project_scene(fs::path const& requested)
    {
        if(editor || !scripted_game || !preview || file_path.empty())
            throw std::runtime_error("Scene transition requires a running game");
        if(requested.empty() || requested.is_absolute() ||
           requested.has_root_name() || requested.extension()!=L".nscn")
            throw std::invalid_argument("Scene path must be relative .nscn");
        for(auto const& part:requested)
            if(part=="." || part==".." || part.empty())
                throw std::invalid_argument("Scene path traversal is forbidden");

        fs::path next_path=requested;
        Scene next_scene;
        if(config.archive_path.empty()) {
            auto const root=fs::canonical(script_project_root(file_path));
            auto const candidate=fs::canonical(root/requested);
            auto const relative=fs::relative(candidate,root);
            if(relative.empty() || relative.is_absolute())
                throw std::invalid_argument("Scene path is outside the project");
            for(auto const& part:relative)
                if(part=="..")
                    throw std::invalid_argument("Scene path escapes the project");
            if(!fs::is_regular_file(candidate))
                throw std::invalid_argument("Requested scene is not a file");
            next_path=candidate;
            next_scene=read_document(next_path);
        } else {
            AssetManager assets;
            assets.mount_archive("",config.archive_path,
                config.archive_data.empty()?config.archive_path:config.archive_data);
            next_scene=*assets.request_scene(requested.generic_u8string()).get();
        }
        if(next_path==file_path)return;
        auto prepared=std::make_unique<PhysicsScene>(next_scene);
        auto const prior_file=file_path;
        auto const prior_scene=document.snapshot();
        stop_project_scripts();
        auto old_physics=std::move(preview);
        try {
            file_path=std::move(next_path);
            document.load(std::move(next_scene));
            preview=std::move(prepared);
            script_yaw=script_pitch=0.f;
            if(scripted_first_person && preview->controlled_entity()) {
                auto const rig=gameplay::camera_rig(preview->scene(),
                    preview->controlled_entity());
                script_eye_height=rig.eye_height;
                fps_camera_settings.default_fov_y=rig.fov_degrees;
            }
            if(fps_camera) {
                fps_camera=std::make_unique<CameraDirector>(fps_camera_settings);
                update_script_camera(0.f);
            }
            rebuild_scene_world();
            initialize_project_scripts();
            frame_clock.start();
            restore_gameplay_input_focus();
            std::fprintf(stderr,"[INFO] [engine.session] scene loaded: %s\n",
                file_path.u8string().c_str());
        } catch(...) {
            auto failure=std::current_exception();
            try {
                stop_project_scripts();
                file_path=prior_file;
                document.load(prior_scene);
                preview=std::move(old_physics);
                if(fps_camera) {
                    fps_camera->reset();
                    update_script_camera(0.f);
                }
                rebuild_scene_world();
                initialize_project_scripts();
                frame_clock.start();
            } catch(std::exception const& rollback_error) {
                std::fprintf(stderr,"[ERROR] [engine.session] rollback: %s\n",
                    rollback_error.what());
            }
            std::rethrow_exception(failure);
        }
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

    // A child Player HWND never becomes the OS foreground window while
    // Play In Editor is active: Windows reports the Qt Editor top-level HWND.
    // Keep the same foreground policy for cursor capture and script input.
    bool game_has_foreground() const
    {
        auto const foreground=GetForegroundWindow();
        return foreground && (foreground==window ||
            (config.embed_parent && IsWindow(config.embed_parent) &&
             foreground==GetAncestor(config.embed_parent,GA_ROOT)));
    }

    void capture_fps_cursor()
    {
        bool const active=game_has_foreground();
        if (editor || (!fps && !scripted_first_person) || fps_captured ||
            fps_menu_visible || script_paused || pie_paused || pie_ejected ||
            !window || !active) return;
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

    // UI clicks focus the GPU child viewport, causing WM_KILLFOCUS on the
    // main Player HWND. Restore actual keyboard focus, not just cursor clip.
    // Do not steal the foreground from unrelated applications.
    void restore_gameplay_input_focus()
    {
        if (editor || (!fps && !scripted_first_person) || !window ||
            fps_menu_visible || script_paused || pie_paused || pie_ejected ||
            !game_has_foreground()) return;
        if (GetFocus() != window) {
            SetFocus(window); // WM_SETFOCUS re-enables the native input provider.
            if (GetFocus() != window) return;
        }
        capture_fps_cursor();
    }

    LRESULT message(HWND handle, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        // Versioned, editor-only cross-process controls. Processed on the
        // Player window thread, so the renderer/physics state is never mutated
        // concurrently with a frame. SendMessageTimeout returns 1 only after
        // the requested command has been applied.
        if(config.embed_parent && !editor) {
            static UINT const pause=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::pause);
            static UINT const resume=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::resume);
            static UINT const step=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::step);
            static UINT const eject=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::eject);
            static UINT const possess=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::possess);
            if(pause && msg==pause) {
                pie_stepper.reset();
                pie_paused=true;
                release_fps_cursor();
                return 1;
            }
            if(resume && msg==resume) {
                pie_stepper.reset();
                pie_paused=false;
                frame_clock.start(); // do not integrate time spent paused
                if(!pie_ejected)capture_fps_cursor();
                return 1;
            }
            if(step && msg==step) {
                // Only acknowledge a genuine queued frame. Multiple rapid
                // clicks before the next render are coalesced to one tick.
                return pie_stepper.request(pie_paused,script_paused)?1:0;
            }
            if(eject && msg==eject) {
                pie_ejected=true;
                release_fps_cursor();
                return 1;
            }
            if(possess && msg==possess) {
                pie_ejected=false;
                if(!pie_paused)capture_fps_cursor();
                return 1;
            }
        }
        if(config.embed_parent && msg==WM_KEYDOWN &&
           !(lparam&(1LL<<30))) {
            bool const shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
            wchar_t const* request=nullptr;
            if(wparam==VK_F7)
                request=shift?::northstarcpp::editor::pie_protocol::resume_request:
                              ::northstarcpp::editor::pie_protocol::pause_request;
            else if(wparam==VK_F9)
                request=::northstarcpp::editor::pie_protocol::step_request;
            else if(wparam==VK_F8)
                request=::northstarcpp::editor::pie_protocol::eject_request;
            else if(wparam==VK_F6 && shift)
                request=::northstarcpp::editor::pie_protocol::exit_request;
            if(request) {
                UINT const notify=RegisterWindowMessageW(request);
                if(notify)PostMessageW(config.embed_parent,notify,0,0);
                return 0;
            }
        }
        if(config.embed_parent && msg==WM_KEYDOWN &&
           wparam==VK_ESCAPE && !(lparam&(1LL<<30))) {
            // Escape always exits Play In Editor, not an in-game pause
            // menu. Send the request to the owning Qt Editor HWND before
            // any game script, UI controller or provider consumes the key.
            release_fps_cursor();
            UINT const notify=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::exit_request);
            if(!notify || !PostMessageW(config.embed_parent,notify,0,0))
                PostMessageW(window,WM_CLOSE,0,0);
            return 0;
        }
        if (input)
            input->feed_message(msg, static_cast<uintptr_t>(wparam), static_cast<intptr_t>(lparam));
        // The Qt overlay initially owns focus. A click in the embedded
        // native game window transfers input focus and enables relative mouse.
        if (config.embed_parent && msg==WM_LBUTTONDOWN &&
            (fps || scripted_first_person) && !pie_paused && !pie_ejected &&
            game_has_foreground()) {
            if (GetFocus()!=window) SetFocus(window);
            capture_fps_cursor();
        }
        switch (msg) {
        case WM_ACTIVATEAPP:
            if (fps || scripted_first_person) {
                if (!wparam) release_fps_cursor();
                else if (!fps_menu_visible && !pie_paused && !pie_ejected)
                    restore_gameplay_input_focus();
            }
            break;
        case WM_KILLFOCUS:
            if (fps || scripted_first_person) release_fps_cursor();
            break;
        case WM_SETFOCUS:
            if ((fps || scripted_first_person) && !pie_paused && !pie_ejected)
                capture_fps_cursor();
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
                restore_gameplay_input_focus();
            }
            return 0;
        case WM_APP+28:
            if (!editor && !requested_project_scene_.empty()) {
                auto path=requested_project_scene_;
                requested_project_scene_.clear();
                try { switch_project_scene(path); }
                catch (std::exception const& error) {
                    std::fprintf(stderr,
                        "[ERROR] [engine.session] scene transition: %s\n",error.what());
                    report(std::string("Cannot switch project scene: ")+error.what());
                }
            }
            return 0;
        case WM_TIMER:
            if (editor && content_browser)
                content_browser->poll_thumbnails();
            if (wparam == frame_timer && !runtime_faulted &&
                !error_dialog_active && !config.smoke && !config.check_scene &&
                !config.character_smoke && graphics && !IsIconic(window)) {
                if(startup_report_ && !startup_report_->completed()) {
                    bootstrap_stage("frame.first", [&] {frame();});
                    startup_report_->complete(startup_report_->warning_count()?
                        "READY_WITH_WARNINGS":"READY");
                } else frame();
            }
            return 0;
        case smoke_message:
            if (config.character_smoke) {
                if(!script_character || !script_character_pose)
                    throw std::runtime_error("Project did not configure a skeletal character");
                auto const& scene=preview->scene();
                auto const* camera=fps_camera ? &fps_pose : nullptr;
                for(unsigned i=0;i<8;++i) {
                    apply_script_character(scene,0.05f);
                    graphics->draw(scene,-2,false,preview.get(),nullptr,false,camera,
                        scene_streaming.world(),false);
                }
                std::puts("Project-defined textured skeletal draw: PASS");
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
            } else if (!config.transition_scene.empty()) {
                // Reproduce menu -> game transitions without desktop input.
                // The real project scripts, Jolt, V8 and GPU are exercised.
                if(!script_runtime || !preview || !graphics)
                    throw std::runtime_error("Transition smoke requires a live project runtime");
                std::fprintf(stderr,
                    "[INFO] [engine.session] qa transition begin target=%s\\n",
                    config.transition_scene.u8string().c_str());
                switch_project_scene(config.transition_scene);
                if(!script_runtime || !preview)
                    throw std::runtime_error("Transition smoke lost Jolt/V8 runtime");
                frame();
                std::puts("Project scene transition + first gameplay GPU frame: PASS");
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
        player = child(L"BUTTON", L"Auto Possess Local", WS_TABSTOP | BS_AUTOCHECKBOX, player_object);
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
            if (scene_pawn::is_local(view,view.objects[i]))name+=L"  [Local Pawn]";
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
        if (scene_streaming.world()) {
            auto const& diagnostics=scene_streaming.world()->streaming_diagnostics();
            summary+=L" | Cells: "+std::to_wstring(diagnostics.loaded_cells)+L"/"+
                std::to_wstring(scene_streaming.world()->source().cells.size())+
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
            (is_basic ? scene_pawn::is_local(scene,object) : node.visible) ? BST_CHECKED : BST_UNCHECKED,0);
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
        if (scene_streaming.world())
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
        auto const project=script_project_root(file_path);
        auto root=project/"assets";
        if (!fs::is_directory(root))
            throw std::runtime_error("Project assets folder is missing");
        AssetManager assets;
        assets.mount_project_assets(project,discover_shared_assets(config,project));
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
            // ContentBrowser is part of the unified Qt Editor, not a
            // separately launched asset browser process.
            std::wstring arguments=L"--focus-content-browser";
            if (!file_path.empty())
                arguments+=L" --scene \"" + fs::absolute(file_path).wstring() + L"\"";
            launch_tool(L"northstarcpp_editor_qt.exe",arguments);
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
                    if(preview && preview->controlled_entity())
                        (void)gameplay::configure_player_camera(
                            *fps_camera,preview->scene(),preview->controlled_entity());
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
        editor_tools::promote_to_v3(changed);
        auto& object=changed.objects[index];

        if(inspector_page==0) {
            object.name=to_utf8(get_text(edits[0]));
            object.position={parse_number(edits[1]),parse_number(edits[2]),parse_number(edits[3])};
            object.size=parse_number(edits[4]);
            object.mass=parse_number(edits[5]);
            bool const possess=SendMessageW(player,BM_GETCHECK,0,0)==BST_CHECKED;
            if(possess) {
                if(object.mass==0.f)object.mass=1.f;
                player_character::add_character_components(changed,object.node.id,false);
            }
            for(auto& component:changed.components) {
                if(component.type!=player_character::pawn_type ||
                   component.version!=player_character::version ||
                   component.payload.size()!=12)continue;
                // Change only auto-possess metadata, never destroy Pawn/Camera.
                component.payload[4]=char(possess &&
                    component.entity_id==object.node.id?1:0);
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



    void destroy_script_spawn(std::uint64_t id, std::uint64_t owner)
    {
        auto const owned=script_spawn_owners.find(id);
        if (owned==script_spawn_owners.end() || owned->second!=owner)
            throw std::out_of_range("Stale or foreign runtime scene entity");
        auto const found=script_entity_indices.find(id);
        if (found==script_entity_indices.end() || !preview ||
            !preview->runtime_alive(found->second))
            throw std::out_of_range("Runtime scene entity no longer exists");
        auto const index=found->second;
        for (auto const& [child_id, child_owner]:script_spawn_owners) {
            if (child_id==id || !script_entity_indices.count(child_id)) continue;
            auto const child_index=script_entity_indices.at(child_id);
            if (preview->runtime_alive(child_index) &&
                preview->scene().objects[child_index].node.parent==id)
                throw std::runtime_error("Destroy runtime entity children before their parent");
        }
        preview->destroy_runtime(index);
        auto const handle=script_spawn_handles.find(id);
        if (handle!=script_spawn_handles.end() && script_ecs) {
            script_ecs->erase(handle->second);
            script_handle_ids.erase(handle->second);
            script_spawn_handles.erase(handle);
        }
        script_entity_indices.erase(found);
        script_spawn_owners.erase(owned);
        script_object_colors.erase(index);
        script_model_animations.erase(id);
        script_model_visual_modes.erase(id);
        script_model_bone_attachments.erase(id);
        for(auto it=script_model_bone_attachments.begin();it!=script_model_bone_attachments.end();) {
            std::uint64_t parent=0;
            std::istringstream stream(it->second);
            stream>>parent;
            if(parent==id)it=script_model_bone_attachments.erase(it);
            else ++it;
        }
    }

    void cleanup_script_spawns(std::uint64_t owner)
    {
        std::vector<std::pair<std::uint64_t,std::size_t>> ids;
        for (auto const& [id, context]:script_spawn_owners) {
            if (owner && owner!=context) continue;
            std::size_t depth=0;
            auto parent=preview->scene().objects[script_entity_indices.at(id)].node.parent;
            while (parent && script_entity_indices.count(parent)) {
                if (++depth>max_scene_objects)
                    throw std::runtime_error("Runtime entity hierarchy contains a cycle");
                parent=preview->scene().objects[script_entity_indices.at(parent)].node.parent;
            }
            ids.emplace_back(id,depth);
        }
        std::sort(ids.begin(),ids.end(),[](auto const& a,auto const& b) {
            if (a.second!=b.second) return a.second>b.second;
            return a.first>b.first;
        });
        for (auto const& [id,depth]:ids) {
            (void)depth;
            destroy_script_spawn(id,script_spawn_owners.at(id));
        }
    }

    #include "window_vehicle_runtime.inc"
    void configure_script_bindings(ScriptRuntime& runtime) {
        auto const script_owner=++script_context_epoch;
        configure_vehicle_bindings(runtime);
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
        // Expose the actual build's CMake PROJECT_VERSION to game scripts.
        // Do not hardcode the engine version in project-authored menus.
        runtime.bind("engine.rendering","engineVersion",
            [](std::string_view json) {
                if(!script_args(json).empty())
                    throw std::invalid_argument(
                        "engine.rendering.engineVersion() takes no arguments");
                return script_json_quote(NORTHSTARCPP_RUNTIME_VERSION);
            });
        // Generic, engine-owned quality policy. Projects own the actual
        // settings UI and persist the selected value through engine.save.
        runtime.bind("engine.rendering","setDetailLevel",
            [this](std::string_view json) {
                auto args=script_args(json);
                if(args.size()!=1 || (args[0]!="balanced" && args[0]!="high" &&
                    args[0]!="ultra" && args[0]!="cinematic"))
                    throw std::invalid_argument(
                        "engine.rendering.setDetailLevel: balanced/high/ultra/cinematic");
                graphics_detail_level=args[0];
                if(graphics) graphics->set_detail_level(graphics_detail_level);
                return script_json_quote(graphics_detail_level);
            });
        runtime.bind("engine.rendering","detailLevel",
            [this](std::string_view json) {
                auto args=script_args(json);
                if(!args.empty())
                    throw std::invalid_argument("engine.rendering.detailLevel()");
                return script_json_quote(graphics_detail_level);
            });
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
        runtime.bind("engine.scene","spawn",
            [this,script_owner](std::string_view json) {
                auto args=script_args(json);
                if ((args.size()!=9 && args.size()!=10 && args.size()!=11) || !preview || !script_ecs ||
                    !preview->supports_runtime_entities())
                    throw std::invalid_argument("scene.spawn(name,x,y,z,size,mass,color,asset,layer,parent)");
                SceneObject object;
                object.name=args[0];
                if (object.name.empty() || object.name.size()>=NORTHSTARCPP_ECS_NAME_BYTES)
                    throw std::invalid_argument("Invalid runtime entity name");
                auto finite_coordinate=[](std::string const& input) {
                    auto const v=script_number(input);
                    if (std::abs(v)>1000000.)
                        throw std::invalid_argument("Runtime position out of bounds");
                    return static_cast<float>(v);
                };
                object.position={finite_coordinate(args[1]),finite_coordinate(args[2]),
                                 finite_coordinate(args[3])};
                object.size=static_cast<float>(script_number(args[4]));
                object.mass=static_cast<float>(script_number(args[5]));
                auto const color=script_number(args[6]);
                if (color<0. || color>0xFFFFFFu || std::floor(color)!=color)
                    throw std::invalid_argument("Invalid runtime RGB color");
                object.color=static_cast<std::uint32_t>(color);
                object.node.asset=args[7];
                object.node.layer=args[8];
                if(args.size()==11) {
                    if(args[10]!="true" && args[10]!="false")
                        throw std::invalid_argument("Runtime collision must be boolean");
                    object.node.collision=args[10]=="true";
                }
                if (object.node.layer.empty() || object.node.layer.size()>96)
                    throw std::invalid_argument("Invalid runtime entity layer");
                if (args.size()>=10 && args[9]!="0") {
                    auto const parent=script_index(args[9]);
                    auto const found=script_spawn_owners.find(parent);
                    if (found==script_spawn_owners.end() || found->second!=script_owner ||
                        !preview->runtime_alive(script_entity_indices.at(parent)))
                        throw std::invalid_argument("Runtime parent must be a live entity in this script context");
                    object.node.parent=parent;
                }
                if (!object.node.asset.empty()) {
                    auto const& asset=object.node.asset;
                    if (asset.size()>256 || asset.front()=='/' ||
                        asset.find("..")!=std::string::npos ||
                        asset.find('\\')!=std::string::npos ||
                        asset.find(':')!=std::string::npos ||
                        asset.size()<4 ||
                        (asset.substr(asset.size()-4)!=".obj" &&
                         asset.substr(asset.size()-4)!=".ydd" &&
                         asset.substr(asset.size()-4)!=".ydr") ||
                        (asset.compare(0,7,"models/")!=0 &&
                         asset.compare(0,7,"assets/")!=0 &&
                         asset.compare(0,11,"characters/")!=0))
                        throw std::invalid_argument(
                            "Runtime model must be a VFS OBJ/YDD/YDR");
                    if (!script_assets)
                        throw std::runtime_error("Project VFS unavailable");
                    (void)script_assets->size(asset);
                }
                if (!next_script_spawn_id)
                    for (auto const& previous:preview->scene().objects)
                        next_script_spawn_id=std::max(next_script_spawn_id,previous.node.id);
                if (next_script_spawn_id==UINT64_MAX)
                    throw std::runtime_error("Exhausted runtime entity IDs");
                object.node.id=++next_script_spawn_id;
                auto const index=preview->spawn_runtime(object);
                try {
                    auto const handle=script_ecs->create_runtime(object);
                    script_entity_indices.emplace(object.node.id,index);
                    script_handle_ids.emplace(handle,object.node.id);
                    script_spawn_handles.emplace(object.node.id,handle);
                    script_spawn_owners.emplace(object.node.id,script_owner);
                } catch (...) {
                    preview->destroy_runtime(index);
                    throw;
                }
                return std::string("{\"id\":")+
                    script_json_quote(std::to_string(object.node.id))+
                    ",\"index\":"+std::to_string(index)+"}";
            });
        runtime.bind("engine.scene","destroy",
            [this,script_owner](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=1)
                    throw std::invalid_argument("scene.destroy(id)");
                destroy_script_spawn(script_index(args[0]),script_owner);
                return std::string("true");
            });
        runtime.bind("engine.scene","moveRuntime",
            [this,script_owner](std::string_view json) {
                auto args=script_args(json);
                if (args.size()!=4 || !preview || !script_ecs)
                    throw std::invalid_argument("scene.moveRuntime(id,x,y,z)");
                auto const id=script_index(args[0]);
                if (!script_spawn_owners.count(id) ||
                    script_spawn_owners.at(id)!=script_owner)
                    throw std::out_of_range("Stale runtime entity ID");
                auto const index=script_entity_indices.at(id);
                if (preview->scene().objects[index].mass!=0.f)
                    throw std::invalid_argument("Dynamic bodies must use physics");
                Vector3 p{static_cast<float>(script_number(args[1])),
                          static_cast<float>(script_number(args[2])),
                          static_cast<float>(script_number(args[3]))};
                if (std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>1000000.f)
                    throw std::invalid_argument("Runtime position out of bounds");
                preview->move_static(index,p);
                NsPosition position{p.x,p.y,p.z};
                script_ecs->set_component(script_spawn_handles.at(id),
                    NS_COMPONENT_POSITION,&position,sizeof(position));
                return std::string("null");
            });
        runtime.bind("engine.scene","rotateRuntime",
            [this,script_owner](std::string_view json) {
                auto args=script_args(json);
                if(args.size()!=4 || !preview)
                    throw std::invalid_argument("scene.rotateRuntime(id,x,y,z)");
                auto const id=script_index(args[0]);
                if(!script_spawn_owners.count(id) ||
                   script_spawn_owners.at(id)!=script_owner)
                    throw std::out_of_range("Stale runtime entity rotation request");
                preview->rotate_runtime(script_entity_indices.at(id),
                    Vector3{static_cast<float>(script_number(args[1])),
                            static_cast<float>(script_number(args[2])),
                            static_cast<float>(script_number(args[3]))});
                return std::string("null");
            });
        runtime.bind("engine.prefab","read",[this](std::string_view json) {
            auto args=script_args(json);
            if (args.size()!=1 || !script_assets)
                throw std::invalid_argument("prefab.read(path)");
            auto const& path=args[0];
            if (path.size()>256 || path.size()<17 || path.front()=='/' ||
                path.compare(0,8,"prefabs/")!=0 ||
                path.substr(path.size()-8)!=".nprefab" ||
                path.find("..")!=std::string::npos ||
                path.find('\\')!=std::string::npos ||
                path.find(':')!=std::string::npos ||
                path.find("//")!=std::string::npos)
                throw std::invalid_argument("Prefab path must be under prefabs/*.nprefab");
            if (script_assets->size(path)>1024*1024u)
                throw std::length_error("Runtime prefab exceeds 1 MiB");
            auto const bytes=script_assets->read(path);
            Scene const prefab=parse_scene_data(
                std::string(bytes.begin(),bytes.end()));
            validate_scene(prefab);
            if (prefab.version!=3 || prefab.objects.empty() ||
                prefab.objects.size()>128 || !prefab.volumes.empty() ||
                !prefab.portals.empty() || !prefab.opaque_records.empty())
                throw std::invalid_argument(
                    "Runtime prefab requires 1..128 Scene V3 entities without external records");
            // An animation is authored as a generic prefab component,
            // independent of Pawn/Player and any equipped weapon.
            std::unordered_map<std::uint64_t,std::string> default_clips;
            for(auto const& component:prefab.components) {
                if(component.type!="northstar.animation.default" ||
                   component.version!=1 || component.payload.empty() ||
                   component.payload.size()>512)
                    throw std::invalid_argument(
                        "Runtime prefab supports only animation.default component v1");
                auto const& ref=component.payload;
                auto const sep=ref.rfind('@');
                if(sep==std::string::npos || sep<4 ||
                   sep+1>=ref.size() || ref.substr(sep-4,4)!=".ycd" ||
                   ref.find("..")!=std::string::npos ||
                   ref.find('\\')!=std::string::npos ||
                   ref.find(':')!=std::string::npos ||
                   ref.find('\0')!=std::string::npos ||
                   !default_clips.emplace(component.entity_id,ref).second)
                    throw std::invalid_argument("Invalid prefab animation reference");
                auto const owner=std::find_if(prefab.objects.begin(),
                    prefab.objects.end(),[&](auto const& object){
                        return object.node.id==component.entity_id;
                    });
                if(owner==prefab.objects.end() ||
                   (owner->node.asset.find(".ydd")==std::string::npos &&
                    owner->node.asset.find(".ydr")==std::string::npos))
                    throw std::invalid_argument(
                        "Prefab animation requires an authored YDD/YDR entity");
            }
            std::uint64_t root_id=0;
            std::string result="{\"objects\":[";
            for (std::size_t i=0;i<prefab.objects.size();++i) {
                auto const& obj=prefab.objects[i];
                if (scene_pawn::is_pawn(prefab,obj) || !obj.node.script.empty() ||
                    !obj.node.cell.empty() || !obj.node.room.empty() ||
                    !obj.node.visible ||
                    obj.node.rotation.x!=0.f || obj.node.rotation.y!=0.f ||
                    obj.node.rotation.z!=0.f ||
                    obj.node.scale.x!=1.f || obj.node.scale.y!=1.f ||
                    obj.node.scale.z!=1.f)
                    throw std::invalid_argument(
                        "Runtime prefab requires an unrotated transform and no scene cell");
                if (!obj.node.parent) {
                    if (root_id)
                        throw std::invalid_argument("Runtime prefab has multiple roots");
                    root_id=obj.node.id;
                }
                if (i) result+=',';
                result+="{\"id\":"+script_json_quote(std::to_string(obj.node.id))+
                    ",\"parent\":"+script_json_quote(std::to_string(obj.node.parent))+
                    ",\"name\":"+script_json_quote(obj.name)+
                    ",\"position\":{\"x\":"+std::to_string(obj.position.x)+
                    ",\"y\":"+std::to_string(obj.position.y)+
                    ",\"z\":"+std::to_string(obj.position.z)+"}"+
                    ",\"size\":"+std::to_string(obj.size)+
                    ",\"mass\":"+std::to_string(obj.mass)+
                    ",\"color\":"+std::to_string(obj.color)+
                    ",\"asset\":"+script_json_quote(obj.node.asset)+
                    ",\"layer\":"+script_json_quote(obj.node.layer)+
                    ",\"collision\":"+(obj.node.collision?"true":"false")+"}";
            }
            if (!root_id)
                throw std::invalid_argument("Runtime prefab must have one root");
            result+="],\"animations\":[";
            bool first_animation=true;
            for(auto const& obj:prefab.objects) {
                auto it=default_clips.find(obj.node.id);
                if(it==default_clips.end())continue;
                if(!first_animation)result+=',';
                first_animation=false;
                result+="{\"entity\":"+script_json_quote(std::to_string(obj.node.id))+
                    ",\"clip\":"+script_json_quote(it->second)+"}";
            }
            return result+"]}";
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
                if (args.size()!=2 || !scene_streaming.world() ||
                    (args[1]!="true" && args[1]!="false"))
                    throw std::invalid_argument("setLayerVisible(name,bool)");
                scene_streaming.world()->set_layer_visible(args[0],args[1]=="true");
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
        runtime.bind("engine.input","wheelDelta",[this](std::string_view json) {
            if (!script_args(json).empty()) throw std::invalid_argument("input.wheelDelta()");
            // One wheel notch is 120 input-provider units; keep precision for trackpads.
            return std::to_string(static_cast<double>(script_wheel_vertical)/120.0);
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
        runtime.bind("engine.camera","available",[this](std::string_view json) {
            if (!script_args(json).empty())
                throw std::invalid_argument("camera.available()");
            return fps_camera ? std::string("true") : std::string("false");
        });
        runtime.bind("engine.camera","setMode",[this](std::string_view json) {
            auto const args=script_args(json);
            if (args.size()<1 || args.size()>2 || !fps_camera)
                throw std::invalid_argument("camera.setMode(name,transitionSeconds=0.35)");
            static const std::map<std::string,CameraMode> modes={
                {"first_person",CameraMode::first_person},
                {"third_person",CameraMode::third_person},
                {"vehicle_chase",CameraMode::vehicle_chase},
                {"orbit",CameraMode::orbit},
                {"free",CameraMode::free},
                {"cinematic",CameraMode::cinematic}};
            auto const mode=modes.find(args[0]);
            if (mode==modes.end()) throw std::invalid_argument("Unknown camera mode");
            auto const seconds=args.size()==2 ? script_number(args[1]) : 0.35;
            fps_camera->set_mode(mode->second,static_cast<float>(seconds));
            return std::string("null");
        });
        runtime.bind("engine.camera","mode",[this](std::string_view json) {
            if (!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.mode()");
            switch (fps_camera->mode()) {
            case CameraMode::first_person: return script_json_quote("first_person");
            case CameraMode::third_person: return script_json_quote("third_person");
            case CameraMode::vehicle_chase: return script_json_quote("vehicle_chase");
            case CameraMode::orbit: return script_json_quote("orbit");
            case CameraMode::free: return script_json_quote("free");
            case CameraMode::cinematic: return script_json_quote("cinematic");
            }
            throw std::runtime_error("Invalid camera mode");
        });
        runtime.bind("engine.camera","setFollowDistance",
            [this](std::string_view json) {
                auto const args=script_args(json);
                if (args.size()!=1 || !fps_camera)
                    throw std::invalid_argument("camera.setFollowDistance(metres)");
                auto settings=fps_camera->settings();
                settings.third_person_distance=static_cast<float>(script_number(args[0]));
                fps_camera->configure(settings);
                return std::string("null");
            });
        runtime.bind("engine.camera","followDistance",[this](std::string_view json) {
            if (!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.followDistance()");
            return std::to_string(fps_camera->settings().third_person_distance);
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
        runtime.bind("engine.camera","cut",[this](std::string_view json) {
            if (!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.cut()");
            // Explicit teleport / shot boundary. Keep active cinematic
            // track time, session state, and plugin-owned resources intact.
            fps_camera->cut();
            return std::string("null");
        });
        runtime.bind("engine.camera","pushShot",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=10 || !fps_camera)
                throw std::invalid_argument(
                    "camera.pushShot(source,x,y,z,yaw,pitch,roll,fov,priority,blend)");
            CameraShot shot;
            shot.source=args[0];
            shot.pose.position={
                static_cast<float>(script_number(args[1])),
                static_cast<float>(script_number(args[2])),
                static_cast<float>(script_number(args[3]))};
            shot.pose.yaw=static_cast<float>(script_number(args[4]));
            shot.pose.pitch=static_cast<float>(script_number(args[5]));
            shot.pose.roll=static_cast<float>(script_number(args[6]));
            shot.pose.fov_y_degrees=static_cast<float>(script_number(args[7]));
            shot.pose.near_plane=fps_camera->settings().near_plane;
            shot.pose.far_plane=fps_camera->settings().far_plane;
            auto const priority=script_number(args[8]);
            if(priority<-100. || priority>100. ||
               std::floor(priority)!=priority)
                throw std::invalid_argument("Camera shot priority must be an integer -100..100");
            shot.priority=static_cast<int>(priority);
            shot.blend_seconds=static_cast<float>(script_number(args[9]));
            fps_camera->push_shot(std::move(shot));
            return std::string("null");
        });
        runtime.bind("engine.camera","clearShot",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=1 || !fps_camera)
                throw std::invalid_argument("camera.clearShot(source)");
            fps_camera->clear_shot(args[0]);
            return std::string("null");
        });
        runtime.bind("engine.camera","clearShots",[this](std::string_view json) {
            if(!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.clearShots()");
            fps_camera->clear_shots();
            return std::string("null");
        });
        runtime.bind("engine.camera","activeShot",[this](std::string_view json) {
            if(!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.activeShot()");
            return script_json_quote(fps_camera->active_shot());
        });
        runtime.bind("engine.camera","applyPreset",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=1 || !fps_camera || !script_assets)
                throw std::invalid_argument("camera.applyPreset(presetId) requires project VFS");
            auto const presets=load_camera_presets(*script_assets);
            auto const it=std::find_if(presets.begin(),presets.end(),
                [&](CameraShot const& preset) {
                    return preset.source==args[0];
                });
            if(it==presets.end())
                throw std::invalid_argument("Camera preset source not found");
            fps_camera->push_shot(*it);
            return std::string("null");
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
        runtime.bind("engine.camera","playCutscene",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.empty() || args.size()>4 || !fps_camera || !script_assets)
                throw std::invalid_argument(
                    "camera.playCutscene(vfsPath,loop=false,entry=0.55,exit=0.55)");
            const bool loop=args.size()>1 && args[1]=="true";
            if(args.size()>1 && args[1]!="true" && args[1]!="false")
                throw std::invalid_argument("Cutscene loop must be boolean");
            auto const entry=args.size()>2?script_number(args[2]):0.55;
            auto const exit=args.size()>3?script_number(args[3]):0.55;
            // VFS resolves this through the currently mounted project/provider.
            auto scene=load_camera_cutscene(*script_assets,args[0]);
            fps_camera->play_cutscene(std::move(scene),loop,
                static_cast<float>(entry),static_cast<float>(exit));
            return std::string("null");
        });
        runtime.bind("engine.camera","stopCutscene",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()>1 || !fps_camera)
                throw std::invalid_argument("camera.stopCutscene(exit=0.55)");
            auto const exit=args.empty()?0.55:script_number(args[0]);
            fps_camera->stop_cutscene(static_cast<float>(exit));
            return std::string("null");
        });
        runtime.bind("engine.camera","pauseCutscene",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !fps_camera ||
               (args[0]!="true" && args[0]!="false"))
                throw std::invalid_argument("camera.pauseCutscene(paused)");
            fps_camera->pause_cutscene(args[0]=="true");
            return std::string("null");
        });
        runtime.bind("engine.camera","seekCutscene",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !fps_camera)
                throw std::invalid_argument("camera.seekCutscene(seconds)");
            fps_camera->seek_cutscene(static_cast<float>(script_number(args[0])));
            return std::string("null");
        });
        runtime.bind("engine.camera","cutsceneState",[this](std::string_view json) {
            if(!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.cutsceneState()");
            return std::string("{\"active\":")+
                (fps_camera->cutscene_active()?"true":"false")+
                ",\"time\":"+std::to_string(fps_camera->cutscene_time())+
                ",\"shot\":"+script_json_quote(fps_camera->cutscene_shot())+"}";
        });
        runtime.bind("engine.camera","lensState",[this](std::string_view json) {
            if(!script_args(json).empty() || !fps_camera)
                throw std::invalid_argument("camera.lensState()");
            auto const lens=fps_camera->cinematic_lens();
            return std::string("{\"active\":")+
                (fps_camera->cutscene_lens_active()?"true":"false")+
                ",\"focusMetres\":"+std::to_string(lens.focus_metres)+
                ",\"apertureF\":"+std::to_string(lens.aperture_f)+
                ",\"exposureEV\":"+std::to_string(lens.exposure_ev)+
                ",\"motionBlur\":"+std::to_string(lens.motion_blur)+
                ",\"letterbox\":"+std::to_string(lens.letterbox)+
                ",\"dofStrength\":"+std::to_string(lens.dof_strength)+"}";
        });
        runtime.bind("engine.camera","playTrack",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.empty() || args.size()>3 || !fps_camera || !script_assets)
                throw std::invalid_argument("camera.playTrack(vfsPath,loop=false,blend=0.5)");
            if(args.size()>1 && args[1]!="true" && args[1]!="false")
                throw std::invalid_argument("Track loop must be boolean");
            auto const blend=args.size()>2?script_number(args[2]):0.5;
            fps_camera->set_track(load_camera_track(*script_assets,args[0]),
                args.size()>1 && args[1]=="true");
            fps_camera->set_mode(CameraMode::cinematic,static_cast<float>(blend));
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
        runtime.bind("engine.character","load",[this](std::string_view json) {
            if(!script_assets || !scripted_game)
                throw std::runtime_error("character.load requires an active scripted project");
            auto spec=parse_script_character(json);
            script_character=std::move(spec);
            script_character_pose.reset();
            script_character_renderable=false;
            script_character_dirty=true;
            return std::string("null");
        });
        runtime.bind("engine.character","pose",[this](std::string_view json) {
            auto const args=script_args(json);
            if((args.size()!=3 && args.size()!=4) || !script_character || !preview)
                throw std::invalid_argument("character.pose(sceneIndex,heading,animation,arms?)");
            if(args.size()==4 && args[3]!="true" && args[3]!="false")
                throw std::invalid_argument("character.pose arms must be boolean");
            auto const index=script_index(args[0]);
            if(index>=preview->scene().objects.size())
                throw std::out_of_range("Character scene index is invalid");
            auto const yaw=script_number(args[1]);
            if(std::abs(yaw)>1000000)
                throw std::invalid_argument("Character heading out of range");
            auto const& name=args[2];
            auto const& clips=script_character->animations;
            if(std::none_of(clips.begin(),clips.end(),
               [&](auto const& clip){return clip.name==name;}))
                throw std::invalid_argument("Character animation is not defined by project");
            script_character_pose=CharacterPoseRequest{index,static_cast<float>(yaw),name,
                args.size()==4 && args[3]=="true"};
            return std::string("null");
        });
        runtime.bind("engine.character","hide",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("character.hide()");
            script_character_pose.reset();
            return std::string("null");
        });
        runtime.bind("engine.character","clear",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("character.clear()");
            script_character.reset();
            script_character_pose.reset();
            script_character_renderable=false;
            script_character_dirty=true;
            return std::string("null");
        });
        runtime.bind("engine.player","controlledEntity",[this](std::string_view json) {
            if(!script_args(json).empty() || !preview)
                throw std::invalid_argument("player.controlledEntity()");
            auto const id=preview->controlled_entity();
            return id?script_json_quote(std::to_string(id)):std::string("null");
        });
        runtime.bind("engine.player","controlledIndex",[this](std::string_view json) {
            if(!script_args(json).empty() || !preview)
                throw std::invalid_argument("player.controlledIndex()");
            auto index=preview->controlled_index();
            return index?std::to_string(*index):std::string("-1");
        });
        runtime.bind("engine.player","animationState",[this](std::string_view json) {
            if(!script_args(json).empty()||!preview)
                throw std::invalid_argument("player.animationState()");
            auto frame=preview->player_animation();
            if(!frame)return std::string("null");
            return std::string("{\"pawn\":")+std::to_string(frame->pawn_id)+
                ",\"mesh\":"+std::to_string(frame->mesh_id)+
                ",\"state\":"+script_json_quote(frame->state)+
                ",\"clip\":"+script_json_quote(frame->clip)+
                ",\"previousClip\":"+script_json_quote(frame->previous_clip)+
                ",\"blend\":"+std::to_string(frame->blend_alpha)+
                ",\"time\":"+std::to_string(frame->playback_seconds)+
                ",\"grounded\":"+(frame->grounded?"true":"false")+"}";
        });
        runtime.bind("engine.player","possess",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !preview || !scripted_game ||
               args[0].empty() ||
               !std::all_of(args[0].begin(),args[0].end(),
                   [](unsigned char ch){return ch>='0' && ch<='9';}))
                throw std::invalid_argument("player.possess(entityId)");
            auto const id=std::stoull(args[0]);
            preview->possess(id);
            auto const tune=gameplay::camera_rig(preview->scene(),id);
            script_eye_height=tune.eye_height;
            fps_camera_settings.default_fov_y=tune.fov_degrees;
            if(fps_camera) {
                auto settings=fps_camera->settings();
                settings.default_fov_y=tune.fov_degrees;
                fps_camera->configure(settings);
                (void)gameplay::configure_player_camera(
                    *fps_camera,preview->scene(),id);
                fps_camera->reset();
            }
            return std::string("true");
        });
        runtime.bind("engine.player","unpossess",[this](std::string_view json) {
            if(!script_args(json).empty() || !preview || !scripted_game)
                throw std::invalid_argument("player.unpossess()");
            preview->unpossess();
            script_move_x=script_move_z=0.f;
            if(fps_camera)fps_camera->reset();
            return std::string("null");
        });
        runtime.bind("engine.player","sprint",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !preview || !scripted_game ||
               (args[0]!="true" && args[0]!="false"))
                throw std::invalid_argument("player.sprint(active)");
            preview->set_sprinting(args[0]=="true");
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
        runtime.bind("engine.player","motion",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("player.motion()");
            return std::string("{\"x\":")+std::to_string(script_move_x)+
                ",\"z\":"+std::to_string(script_move_z)+"}";
        });
        runtime.bind("engine.player","ground",[this](std::string_view json) {
            if(!script_args(json).empty() || !preview)
                throw std::invalid_argument("player.ground()");
            auto const& objects=preview->scene().objects;
            auto idx=preview->controlled_index();
            if(!idx || *idx>=objects.size())
                return std::string("{\"grounded\":false,\"surface\":\"stone\"}");
            auto const index=*idx;
            auto const& pos=objects[index].position; // Pawn feet
            auto const from=Vector3{pos.x,pos.y+0.22f,pos.z};
            auto const to=Vector3{pos.x,pos.y-0.32f,pos.z};
            auto const hit=preview->sweep_sphere(from,to,0.10f,index);
            auto const contact=hit && hit->normal.y>=0.55f;
            // Jolt/Bullet both have an implicit y=0 plane; its physics hit
            // has no scene object index. Treat that as stone unless tagged.
            auto const implicit_floor=pos.y>=-0.1f && pos.y<=0.14f;
            if(!contact && !implicit_floor)
                return std::string("{\"grounded\":false,\"surface\":\"stone\"}");
            std::string surface="stone";
            auto const hit_index=contact ? hit->object_index : UINT32_MAX;
            // A contact on a specific triangle takes precedence over
            // object names, collider tags and the implicit floor.
            if(contact && hit->material_id>physics_surface::unknown)
                surface=std::string(physics_surface::name(hit->material_id));
            else if(hit_index<objects.size()) {
                auto const& terrain=objects[hit_index];
                auto classify=[](std::string text) {
                    std::transform(text.begin(),text.end(),text.begin(),
                        [](unsigned char c){return static_cast<char>(std::tolower(c));});
                    constexpr std::array<std::string_view,8> types={{
                        "dirt","grass","metal","mud","snow","stone","water","wood"}};
                    for(auto const category:types) {
                        if(text.find("surface:"+std::string(category))!=std::string::npos ||
                           text.find("surface_"+std::string(category))!=std::string::npos)
                            return std::string(category);
                    }
                    for(auto const category:types)
                        if(text.find(std::string(category))!=std::string::npos)
                            return std::string(category);
                    return std::string("stone");
                };
                surface=classify(terrain.node.layer+" "+terrain.name+" "+terrain.node.asset);
            }
            return std::string("{\"grounded\":true,\"surface\":")+
                script_json_quote(surface)+
                ",\"materialId\":"+
                std::to_string(contact?hit->material_id:0)+
                ",\"triangleIndex\":"+
                std::to_string(contact?hit->triangle_index:UINT32_MAX)+
                ",\"objectIndex\":" + std::to_string(hit_index)+
                "}";
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
            auto idx=preview->controlled_index();
            if(!idx || *idx>=objects.size())
                throw std::runtime_error("No possessed Pawn in scene");
            auto const eye=gameplay::camera_anchor(
                preview->scene(),objects[*idx].node.id,script_eye_height);
            return std::string("{\"x\":")+std::to_string(eye.x)+
                ",\"y\":"+std::to_string(eye.y)+
                ",\"z\":"+std::to_string(eye.z)+"}";
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
            auto const ignore=args[7]=="-1"
                ? static_cast<std::size_t>(-1) : script_index(args[7]);
            auto hit=preview->sweep_sphere(begin,end,static_cast<float>(radius),
                                           ignore);
            if (!hit || hit->object_index==UINT32_MAX) return std::string("null");
            return std::string("{\"index\":")+std::to_string(hit->object_index)+
                ",\"fraction\":"+std::to_string(hit->fraction)+
                ",\"materialId\":"+std::to_string(hit->material_id)+
                ",\"triangleIndex\":"+std::to_string(hit->triangle_index)+"}";
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
                if (i>=preview->authored_object_count() && !preview->runtime_alive(i)) {
                    result+="null";
                    continue;
                }
                result+="{\"index\":"+std::to_string(i)+
                    ",\"name\":"+script_json_quote(obj.name)+
                    ",\"id\":"+script_json_quote(std::to_string(obj.node.id))+
                    ",\"parent\":"+script_json_quote(std::to_string(obj.node.parent))+
                    ",\"script\":"+script_json_quote(obj.node.script)+
                    ",\"mass\":"+std::to_string(obj.mass)+
                    ",\"size\":"+std::to_string(obj.size)+
                    ",\"player\":"+(scene_pawn::is_local(preview->scene(),obj)?"true":"false")+
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
            if (i>=objs.size() || objs[i].mass!=0.f || scene_pawn::is_pawn(preview->scene(),objs[i]))
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
        runtime.bind("engine.scene","rotateStatic",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=4 || !preview)
                throw std::invalid_argument("scene.rotateStatic(index,x,y,z)");
            auto i=script_index(args[0]);
            auto const& objects=preview->scene().objects;
            if(i>=objects.size() || objects[i].mass!=0.f ||
               scene_pawn::is_pawn(preview->scene(),objects[i]))
                throw std::invalid_argument("Scene rotation requires a static non-Pawn");
            auto x=script_number(args[1]),y=script_number(args[2]),
                 z=script_number(args[3]);
            if(std::max({std::abs(x),std::abs(y),std::abs(z)})>360000.)
                throw std::invalid_argument("Rotation is outside limits");
            preview->rotate_runtime(i,Vector3{
                static_cast<float>(x),static_cast<float>(y),
                static_cast<float>(z)});
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
        runtime.bind("engine.scene","setAnimation",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=2 || !preview)
                throw std::invalid_argument("scene.setAnimation(index,clip_reference)");
            auto const index=script_index(args[0]);
            auto const& objects=preview->scene().objects;
            if(index>=objects.size() ||
               (index>=preview->authored_object_count() &&
                !preview->runtime_alive(index)))
                throw std::out_of_range("Invalid or destroyed scene entity");
            auto const& object=objects[index];
            if(!object.node.id)
                throw std::invalid_argument("Animation requires a stable scene entity ID");
            auto const& model=object.node.asset;
            auto const selector=model.find('@');
            auto const native=model.substr(0,selector);
            if(native.size()<5 ||
               (native.compare(native.size()-4,4,".ydd")!=0 &&
                native.compare(native.size()-4,4,".ydr")!=0))
                throw std::invalid_argument("Animation requires a native YDD/YDR asset");
            auto const& ref=args[1];
            if(!ref.empty()) {
                auto const sep=ref.rfind('@');
                if(ref.size()>512 || sep==std::string::npos || sep<4 ||
                   sep+1>=ref.size() || ref.substr(sep-4,4)!=".ycd" ||
                   ref.find("..")!=std::string::npos ||
                   ref.find('\\')!=std::string::npos ||
                   ref.find(':')!=std::string::npos ||
                   ref.find('\0')!=std::string::npos)
                    throw std::invalid_argument("Invalid native YCD clip reference");
                script_model_animations[object.node.id]=ref;
            } else script_model_animations.erase(object.node.id);
            return std::string("null");
        });
        // Generic presentation mode for one existing skinned scene actor.
        // No character respawn, duplicate FPS prefab, or permanent scene edit.
        runtime.bind("engine.scene","setVisualMode",
            [this,script_owner](std::string_view json) {
                auto args=script_args(json);
                if(args.size()!=5 || !preview)
                    throw std::invalid_argument(
                        "scene.setVisualMode(index,mode,forward,drop,pitch)");
                auto const index=script_index(args[0]);
                auto const& objects=preview->scene().objects;
                if(index>=objects.size() ||
                   (index>=preview->authored_object_count() &&
                    !preview->runtime_alive(index)))
                    throw std::out_of_range("Invalid visual actor");
                auto const& actor=objects[index];
                // A saved SkeletalMesh is a legitimate project-owned visual;
                // it must not be re-instantiated merely to enable FPS arms.
                bool const authoredVisual=index<preview->authored_object_count() &&
                    std::any_of(preview->scene().components.begin(),
                                preview->scene().components.end(),
                        [&](auto const& c) {
                            return c.entity_id==actor.node.id && c.version==1 &&
                                (c.type=="northstar.gameplay.skeletal_mesh" ||
                                 c.type=="northstar.editor.visual_prefab");
                        });
                bool const ownedRuntime=script_spawn_owners.count(actor.node.id) &&
                    script_spawn_owners.at(actor.node.id)==script_owner;
                if(!actor.node.id || actor.mass!=0.f ||
                   (!authoredVisual && !ownedRuntime))
                    throw std::invalid_argument(
                        "Visual mode requires an authored SkeletalMesh or an owned static model");
                if(args[1]=="world") {
                    script_model_visual_modes.erase(actor.node.id);
                    return std::string("null");
                }
                if(args[1]!="first_person_arms")
                    throw std::invalid_argument("Unsupported visual mode");
                auto const model=actor.node.asset;
                if(model.size()<4 ||
                   model.substr(model.size()-4)!=".ydd")
                    throw std::invalid_argument("FPS arms require a skinned YDD");
                float const forward=static_cast<float>(script_number(args[2]));
                float const drop=static_cast<float>(script_number(args[3]));
                float const arm_pitch=static_cast<float>(script_number(args[4]));
                if(!std::isfinite(forward) || forward<0.f || forward>3.f ||
                   !std::isfinite(drop) || drop<0.f || drop>3.f ||
                   !std::isfinite(arm_pitch) || arm_pitch< -180.f ||
                   arm_pitch>180.f)
                    throw std::invalid_argument(
                        "FPS visual offsets outside allowed bounds");
                std::ostringstream payload;
                payload.imbue(std::locale::classic());
                payload << "first_person_arms " << forward << ' '
                        << drop << ' ' << arm_pitch;
                script_model_visual_modes[actor.node.id]=payload.str();
                return std::string("null");
            });
        runtime.bind("engine.scene","attachBone",[this,script_owner](std::string_view json) {
            auto args=script_args(json);
            if((args.size()!=9 && args.size()!=10) || !preview)
                throw std::invalid_argument("scene.attachBone(child,parent,bone,offset,rotation[,alignment])");
            auto const childIndex=script_index(args[0]);
            auto const parentIndex=script_index(args[1]);
            auto const& objects=preview->scene().objects;
            if(childIndex>=objects.size() || parentIndex>=objects.size() ||
               childIndex==parentIndex ||
               !preview->runtime_alive(childIndex) ||
               (parentIndex>=preview->authored_object_count() &&
                !preview->runtime_alive(parentIndex)))
                throw std::invalid_argument("Attachment entities must be live");
            auto const& child=objects[childIndex];
            auto const& parent=objects[parentIndex];
            if(child.mass!=0.f || !child.node.id || !parent.node.id ||
               !script_spawn_owners.count(child.node.id) ||
               script_spawn_owners.at(child.node.id)!=script_owner)
                throw std::invalid_argument("Only an owned static visual can attach to a bone");
            auto const& bone=args[2];
            if(bone.empty() || bone.size()>96 ||
               !std::all_of(bone.begin(),bone.end(),[](unsigned char c) {
                   return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
                          (c>='0' && c<='9') || c=='_';
               }))throw std::invalid_argument("Unsafe skeleton joint name");
            std::string payload=std::to_string(parent.node.id)+" "+bone;
            for(std::size_t i=3;i<9;++i) {
                auto const value=script_number(args[i]);
                if(!std::isfinite(value) || std::abs(value)>360.)
                    throw std::invalid_argument("Unsafe socket transform");
                payload+=" "+std::to_string(value);
            }
            if(args.size()==10) {
                if(args[9]!="socket" && args[9]!="camera_forward")
                    throw std::invalid_argument("Unknown bone alignment mode");
                payload+=" "+args[9];
            }
            script_model_bone_attachments[child.node.id]=std::move(payload);
            return std::string("null");
        });
        runtime.bind("engine.scene","detachBone",[this,script_owner](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || !preview)
                throw std::invalid_argument("scene.detachBone(childIndex)");
            auto const index=script_index(args[0]);
            if(index>=preview->scene().objects.size() ||
                !preview->runtime_alive(index))
                throw std::invalid_argument("Invalid detached object");
            auto id=preview->scene().objects[index].node.id;
            if(!script_spawn_owners.count(id) ||
                script_spawn_owners.at(id)!=script_owner)
                throw std::invalid_argument("Attachment owner mismatch");
            script_model_bone_attachments.erase(id);
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
        runtime.bind("engine.ui","apply",[this](std::string_view json) {
            // Script presents a complete, generic UI document.
            // Rendering is deferred until the GPU viewport is ready.
            auto next=parse_script_ui_document(json);
            if(next.target=="world") {
                auto const id=next.entity_id;
                if(next.nodes.empty())script_world_ui_documents_.erase(id);
                else script_world_ui_documents_[id]=next;
            }else script_ui_document_=next;
            if(graphics)graphics->set_ui_document(std::move(next));
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
            else restore_gameplay_input_focus();
            return std::string("null");
        });
        runtime.bind("engine.session","scene",[this](std::string_view json) {
            if(!script_args(json).empty())
                throw std::invalid_argument("session.scene()");
            auto path=config.archive_path.empty()
                ? fs::relative(file_path,script_project_root(file_path))
                : file_path;
            return script_json_quote(path.generic_u8string());
        });
        runtime.bind("engine.session","loadScene",[this](std::string_view json) {
            auto args=script_args(json);
            if(args.size()!=1 || args[0].empty() || args[0].size()>240 ||
               editor || !scripted_game || !window ||
               !requested_project_scene_.empty())
                throw std::invalid_argument("session.loadScene(projectRelativeNscn)");
            auto next=fs::u8path(args[0]);
            if(next.is_absolute() || next.has_root_name() ||
               next.extension()!=L".nscn")
                throw std::invalid_argument("Scene must be a relative .nscn path");
            for(auto const& part:next)
                if(part=="." || part==".." || part.empty())
                    throw std::invalid_argument("Scene traversal is prohibited");
            requested_project_scene_=std::move(next);
            if(!PostMessageW(window,WM_APP+28,0,0)) {
                requested_project_scene_.clear();
                throw std::runtime_error("Cannot queue scene transition");
            }
            return std::string("true");
        });
        runtime.bind("engine.session","restart",[this](std::string_view json) {
            if(!script_args(json).empty()) throw std::invalid_argument("session.restart()");
            if(window) PostMessageW(window,WM_APP+27,0,0);
            return std::string("null");
        });
        runtime.bind("engine.audio","playNac",[this](std::string_view json) {
            auto const args=script_args(json);
            if(args.size()!=4 || !script_assets)
                throw std::invalid_argument(
                    "audio.playNac(virtualNacPath,clipPrefix,variation,gain)");
            auto const& asset_path=args[0];
            auto const& prefix=args[1];
            if(asset_path.size()>256 || asset_path.rfind("audio/",0)!=0 ||
               fs::u8path(asset_path).extension()!=".nac" ||
               asset_path.find('\\')!=std::string::npos ||
               asset_path.find(':')!=std::string::npos ||
               prefix.size()>48 || prefix.empty() ||
               !std::all_of(prefix.begin(),prefix.end(),[](unsigned char c){
                   return std::isalnum(c) || c=='_';
               }))
                throw std::invalid_argument("Invalid NAC asset or clip prefix");
            for(auto const& segment:fs::u8path(asset_path))
                if(segment==".." || segment=="." || segment.empty())
                    throw std::invalid_argument("NAC path traversal is forbidden");
            auto const variation=script_index(args[2]);
            auto const gain=script_number(args[3]);
            if(variation>1000000 || gain<0 || gain>2)
                throw std::invalid_argument("Invalid NAC variation or gain");
            auto it=script_nac_cache.find(asset_path);
            if(it==script_nac_cache.end()) {
                if(script_nac_cache.size()>=32 ||
                   script_assets->size(asset_path)>16ull*1024*1024)
                    throw std::length_error("NAC cache limit exceeded");
                auto data=script_assets->read(asset_path);
                std::vector<nac::Track> tracks;
                std::string error;
                if(!nac::decode(data,tracks,error))
                    throw std::runtime_error("Cannot decode NEF8 NAC: "+error);
                it=script_nac_cache.emplace(asset_path,std::move(tracks)).first;
            }
            auto const& tracks=it->second;
            // Exact name wins for one-shot cues (fire_body/reload/equip).
            // Prefix + underscore preserves the existing footsteps variation
            // contract (walk_01, walk_02, etc.). No substring ambiguity.
            auto const exact=std::find_if(tracks.begin(),tracks.end(),
                [&](auto const& track){return track.name==prefix;});
            bool const exact_match=exact!=tracks.end();
            std::size_t matches=exact_match?1:0;
            if(!exact_match)
                for(auto const& track:tracks)
                    if(track.name.rfind(prefix+"_",0)==0) ++matches;
            if(!matches)
                throw std::runtime_error("No matching NAC audio clips: "+prefix);
            auto const wanted=variation%matches;
            std::size_t count=0;
            for(auto const& track:tracks) {
                if(exact_match ? track.name!=prefix :
                   track.name.rfind(prefix+"_",0)!=0) continue;
                if(count++ != wanted) continue;
                if(!script_voice_audio)
                    script_voice_audio=std::make_unique<AudioEngine>();
                NorthStarAudioVoiceOptions options{
                    sizeof(NorthStarAudioVoiceOptions),
                    static_cast<float>(gain),0,0,{0,0,0}};
                NorthStarPcm16 pcm{track.samples.data(),
                    static_cast<std::uint32_t>(track.samples.size()/track.channels),
                    track.sample_rate,track.channels};
                // ABI-1 providers support single-stream playback only.
                // Preserve functional footsteps with a one-voice fallback;
                // overlap and per-voice gain require optional voices ABI-2.
                if(!script_voice_audio->supports_voices()) {
                    script_voice_audio->play(pcm);
                    return std::string("0");
                }
                return std::to_string(script_voice_audio->play_voice(pcm,options));
            }
            throw std::runtime_error("NAC clip selection failed");
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
                return preview->controlled_entity()==obj.node.id ? std::string("true") : std::string("false");
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
            if (body.mass!=0.f || scene_pawn::is_pawn(preview->scene(),body))
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
    // Engine API modules are compiled into the runtime, never read from
    // a writable project or sharedAssets mount. Project gameplay scripts
    // retain their own separate VFS and lifecycle.
    void load_script_files(ScriptRuntime& runtime) {
        if(!script_assets || !preview)
            throw std::runtime_error("Script asset VFS is not mounted");
        script_bootstrap::Options options;
        options.project_root=script_source_root;
        options.archive_index=config.archive_path;
        options.archive_data=config.archive_data;
        options.load_project_entry=script_entry_exists;
        options.validate_project_path=[this](fs::path const& relative) {
            if(config.archive_path.empty())
                (void)validated_script_file(script_source_root,relative);
            else
                validate_script_virtual_path(relative);
        };
        auto const report=script_bootstrap::load(
            runtime,*script_assets,options,script_logger.get());
        auto const entity_start=Clock::now();
        std::unordered_set<std::string> unique;
        // Scenes may contain thousands of actors but only a few unique
        // script paths; avoid reserving the whole scene size.
        unique.reserve(std::min<std::size_t>(128,preview->scene().objects.size()));
        std::uint64_t entity_bytes=0;
        auto log_entity=[this](LogLevel level,std::string const& message) noexcept {
            if(!script_logger)return;
            try {script_logger->write(level,"engine.scripting.bootstrap",message);}
            catch(...) {}
        };
        for(auto const& object:preview->scene().objects) {
            if(object.node.script.empty() ||
               !unique.insert(object.node.script).second)
                continue;
            auto const started=Clock::now();
            try {
                auto const relative=fs::u8path(object.node.script);
                if(config.archive_path.empty())
                    (void)validated_script_file(script_source_root,relative);
                else
                    validate_script_virtual_path(relative);
                auto const bytes=script_assets->read(object.node.script);
                if(bytes.size()>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
                    throw std::length_error("Entity script source exceeds 1 MiB");
                // One allocation instead of copying bytes to a temporary
                // string and allocating again during JS scope wrapping.
                std::string wrapped;
                wrapped.reserve(bytes.size()+20);
                wrapped.append("(function(){\n");
                if(!bytes.empty())
                    wrapped.append(reinterpret_cast<char const*>(bytes.data()),bytes.size());
                wrapped.append("\n})();");
                runtime.evaluate(wrapped,object.node.script);
                entity_bytes+=bytes.size();
                log_entity(LogLevel::debug,
                    "phase=entity status=pass path="+object.node.script+
                    " bytes="+std::to_string(bytes.size())+
                    " duration_us="+std::to_string(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            Clock::now()-started).count()));
            }catch(std::exception const& error) {
                log_entity(LogLevel::error,
                    "phase=entity status=fail path="+object.node.script+
                    " reason="+error.what());
                throw;
            }
        }
        log_entity(LogLevel::info,
            "phase=ready status=pass embedded="+std::to_string(report.embedded)+
            " overrides="+std::to_string(report.project_overrides)+
            " entity_scripts="+std::to_string(unique.size())+
            " entity_bytes="+std::to_string(entity_bytes)+
            " total_source_bytes="+std::to_string(report.source_bytes+entity_bytes)+
            " entity_duration_us="+std::to_string(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now()-entity_start).count()));
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
            // Keep payload bounded and execute callbacks inside the JS realm,
            // but avoid one new Realm.eval compilation for every entity batch.
            runtime.emit_event("northstar._entityBatch",
                "{\"event\":"+script_json_quote(event)+",\"items\":"+batch+"}");
            batch="[";
            count=0;
        };
        for (std::size_t i=0;i<scene.objects.size();++i) {
            auto const& object=scene.objects[i];
            if (object.node.script.empty() ||
                (dt>=0.f && scene_streaming.world() && !scene_streaming.world()->object_resident(i))) continue;
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
        auto const reload_started=Clock::now();
        // Loading a candidate JS context may invoke engine.animation.play().
        // Preserve the previous animation state so failed hot reload cannot
        // destroy the active gameplay simulation.
        auto old_animations=script_animations
            ? std::make_unique<SceneAnimationPlayer>(*script_animations)
            : nullptr;
        auto old_visuals=script_animation_visuals;
        auto const old_character=script_character;
        auto const old_character_pose=script_character_pose;
        auto const old_character_dirty=script_character_dirty;
        auto const old_character_renderable=script_character_renderable;
        try {
            script_entry_exists=fs::is_regular_file(script_source_root/"scripts"/"main.js");
            ScriptingOptions scriptOptions;
            if(auto const* timeout=std::getenv("NORTHSTARCPP_QA_SCRIPT_TIMEOUT_MS")) {
                auto const value=std::strtoul(timeout,nullptr,10);
                if(value>=1000ul && value<=60000ul)
                    scriptOptions.timeout_ms=static_cast<std::uint32_t>(value);
            }
            auto fresh=std::make_unique<ScriptRuntime>(scriptOptions);
            configure_script_bindings(*fresh);
            load_script_files(*fresh);
            fresh->emit_event("start");
            notify_script_entities(*fresh,"entity.start");
            auto new_animations=script_animations
                ? std::make_unique<SceneAnimationPlayer>(*script_animations)
                : nullptr;
            // The old context's stop callback can clear native character state.
            // Capture the candidate's resources and pose before stopping the old
            // context, then restore them for the next render frame.
            auto new_character=script_character;
            auto new_character_pose=script_character_pose;
            try {
                notify_script_entities(*script_runtime,"entity.stop");
                script_runtime->emit_event("stop");
            } catch (...) { /* old scripts cannot veto a successful reload */ }
            cleanup_script_spawns(active_script_context_epoch);
            active_script_context_epoch=script_context_epoch;
            script_character=std::move(new_character);
            script_character_pose=std::move(new_character_pose);
            script_character_renderable=false;
            script_character_dirty=true;
            if (new_animations) *script_animations=std::move(*new_animations);
            script_animation_samples.clear();
            script_animation_visuals.clear();
            script_runtime.swap(fresh);
            script_suspended=false;
            if (script_logger)
                script_logger->info("project.script",
                    "phase=hot_reload status=pass duration_us="+std::to_string(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            Clock::now()-reload_started).count()));
        } catch (std::exception const& e) {
            cleanup_script_spawns(script_context_epoch);
            if (old_animations && script_animations)
                *script_animations=std::move(*old_animations);
            script_animation_visuals=std::move(old_visuals);
            script_character=old_character;
            script_character_pose=old_character_pose;
            script_character_dirty=old_character_dirty;
            script_character_renderable=old_character_renderable;
            // A failed edit must not destroy the currently running context.
            if(script_logger) {
                try {script_logger->error("project.script",
                    "phase=hot_reload status=reject duration_us="+std::to_string(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            Clock::now()-reload_started).count())+
                    " reason="+e.what());}catch(...){}
            }
            OutputDebugStringA(e.what());
            std::fprintf(stderr,"Project script reload rejected: %s\n",e.what());
        }
    }

    void stop_project_scripts() {
        script_audio_cache.clear();
        script_nac_cache.clear();
        script_voice_audio.reset();
        script_object_colors.clear();
        script_character.reset();
        script_character_pose.reset();
        script_character_renderable=false;
        script_character_dirty=false;
        if(graphics) {
            try { graphics->clear_character(); }
            catch(std::exception const& error) {
                std::fprintf(stderr,"Character cleanup failed: %s\n",error.what());
            }
        }
        script_paused=false;
        script_move_x=script_move_z=0.f;
        if (script_runtime) {
            try {
                notify_script_entities(*script_runtime,"entity.stop");
                script_runtime->emit_event("stop");
            } catch (std::exception const&) {}
        }
        cleanup_script_spawns(0);
        active_script_context_epoch=0;
        script_runtime.reset();
        script_animations.reset();
        script_vehicle_rigs.clear();
        script_vehicle_visual_indices.clear();
        script_vehicle_door_visual_indices.clear();
        script_vehicle_visual_followers.clear();
        script_vehicle_targets.clear();
        script_vehicle_native.clear();
        script_vehicle.reset();
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
        auto const common=discover_shared_assets(config,base);
        // Embedded engine API is available even when no external sharedAssets
        // directory is mounted (standalone exported games).
        if (archived) {
            script_assets->mount_runtime_archives();
            if(!common.empty())script_assets->mount_shared_assets(common);
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
            script_assets->mount_project_assets(base,common);
            if (!fps_assets && fs::is_directory(base/"assets")) {
                fps_assets=std::make_unique<AssetManager>();
                fps_assets->mount_project_assets(base,common);
            }
        }
        auto const& scene=preview->scene();
        bool attached=false;
        for (auto const& object:scene.objects)
            if (!object.node.script.empty()) attached=true;
        if (!script_entry_exists && !attached && !fps_camera) {
            // A scene without any game camera or attached scripts does not
            // need a persistent V8 instance. All camera-enabled games still
            // receive the engine default root script.
            script_assets.reset();
            return;
        }
        script_animations=std::make_unique<SceneAnimationPlayer>();
        script_vehicle=std::make_unique<VehicleService>();
        std::fprintf(stderr,"[INFO] [engine.vehicle] selected=%s source=%s\n",
            script_vehicle->provider_name().c_str(),script_vehicle->provider_source().c_str());
        script_ecs=std::make_unique<EcsWorld>();
        script_ecs->load(scene);
        next_script_spawn_id=0;
        script_spawn_handles.clear();
        script_spawn_owners.clear();
        script_model_visual_modes.clear();
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
            // Track ALL project root script overrides, including file creation
            // and deletion. The hot-reload swap keeps the current context when
            // a malformed author script fails to initialize.
            static constexpr std::array<std::string_view,28> roots={{
                "input/Keys.js", "input/GamepadButtons.js",
                "scene/Entity.js", "scene/Prefab.js",
                "ui/UI.js", "UI.js",
                "character/Character.js", "Character.js",
                "player/playerConfig.js", "playerConfig.js",
                "player/playerActor.js", "playerActor.js",
                "player/playerSession.js", "playerSession.js",
                "camera/playerCamera.js", "playerCamera.js",
                "camera/playerLook.js", "playerLook.js",
                "player/playerMovement.js", "playerMovement.js",
                "audio/footsteps.js", "footsteps.js",
                "player/playerFootsteps.js", "playerFootsteps.js",
                "character/playerCharacter.js", "playerCharacter.js",
                "player/playerInteraction.js", "playerInteraction.js"
            }};
            for(auto name:roots) {
                auto const path=fs::path("scripts")/fs::u8path(name);
                auto const absolute=base/path;
                if(fs::is_symlink(absolute))
                    throw std::runtime_error("Project root script override cannot be a symbolic link");
                script_file_stamps.emplace(path,fs::is_regular_file(absolute) ?
                    fs::last_write_time(absolute):fs::file_time_type::min());
            }
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
        ScriptingOptions scriptOptions;
        if(auto const* timeout=std::getenv("NORTHSTARCPP_QA_SCRIPT_TIMEOUT_MS")) {
            auto const value=std::strtoul(timeout,nullptr,10);
            if(value>=1000ul && value<=60000ul)
                scriptOptions.timeout_ms=static_cast<std::uint32_t>(value);
        }
        auto fresh=std::make_unique<ScriptRuntime>(scriptOptions);
        configure_script_bindings(*fresh);
        load_script_files(*fresh);
        fresh->emit_event("start");
        notify_script_entities(*fresh,"entity.start");
        script_runtime=std::move(fresh);
        active_script_context_epoch=script_context_epoch;
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
                if (scene_streaming.world() && !scene_streaming.world()->object_resident(sample.scene_index))
                    continue;
                auto const& obj=preview->scene().objects[sample.scene_index];
                if (obj.node.id!=sample.entity_id || obj.mass!=0.f || scene_pawn::is_pawn(preview->scene(),obj))
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
        // The sampled frame interval is always raw. Simulation is still
        // clamped for physics stability, independently from profiling.
        auto const raw_seconds = frame_clock.restart_seconds();
        diagnostics::FrameProfileScope frame_profile(frame_profiler.get(), raw_seconds);
        float seconds = static_cast<float>(raw_seconds);
        if (fps || scripted_first_person) seconds = std::clamp(seconds, 0.f, 0.05f);
        // Keep rendering, message dispatch and async asset streaming alive while
        // freezing simulation time. Restarting FrameClock each render prevents a
        // catch-up physics step on Resume.
        auto const slice=pie_stepper.advance(seconds,pie_paused,script_paused);
        bool const simulation_paused=slice.paused;
        seconds=slice.seconds;
        auto const state = input->poll();
        script_mouse_dx=state.mouse_dx;
        script_mouse_dy=state.mouse_dy;
        script_wheel_vertical=state.focused ? state.wheel_vertical : 0;
        std::copy_n(std::begin(state.mouse_buttons),script_mouse_buttons.size(),
                    script_mouse_buttons.begin());
        std::copy_n(std::begin(state.gamepads),script_gamepads.size(),
                    script_gamepads.begin());
        // In Play In Editor the Qt Editor, not its embedded Player HWND,
        // is the foreground top-level window. Never discard valid relative
        // mouse input just because the Player is embedded.
        if (scripted_first_person && (!game_has_foreground() ||
            !state.focused || (!fps_captured && !editor))) {
            script_mouse_dx=script_mouse_dy=0.f;
        }
        if (scripted_game) {
            script_move_x=script_move_z=0.f; // no stale movement from prior frames
        }
        frame_profile.measure(diagnostics::FrameStage::scripting, [&] {
        if (script_runtime) {
            // Never call into V8 from an external UI provider callback.
            // Drain the provider's bounded event batch on the game thread
            // before the regular update event.
            if(graphics && !script_suspended) try {
                for(auto const& event:graphics->take_ui_events()) {
                    if(event.id.size()>96 || event.name.size()>64)continue;
                    std::string value="null";
                    switch(event.value_kind) {
                    case NORTHSTAR_UI_VALUE_BOOL:
                        value=event.boolean?"true":"false";break;
                    case NORTHSTAR_UI_VALUE_NUMBER:
                        if(std::isfinite(event.number))
                            value=std::to_string(event.number);
                        break;
                    case NORTHSTAR_UI_VALUE_STRING:
                        value=script_json_quote(event.text);
                        break;
                    default:break;
                    }
                    std::string payload="{\"id\":"+script_json_quote(event.id)+
                        ",\"name\":"+script_json_quote(event.name)+
                        ",\"kind\":"+std::to_string(event.kind)+
                        ",\"value\":"+value+"}";
                    script_runtime->emit_event("ui.event",payload);
                }
            } catch(std::exception const& error) {
                std::fprintf(stderr,
                    "[WARN] [engine.ui] UI dispatch error: %s\n",error.what());
                script_suspended=true;
            }
            std::copy_n(std::begin(state.keys),script_keys.size(),script_keys.begin());
            auto const scan_now=Clock::now();
            if (!config.packaged_game &&
                scan_now-script_probe_last>=std::chrono::milliseconds(500)) {
                script_probe_last=scan_now;
                if (scripts_changed()) reload_project_scripts();
            }
            if (!script_suspended && !simulation_paused) try {
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
        });
        frame_profile.measure(diagnostics::FrameStage::animation, [&] {
        if (script_animations && !script_suspended && !simulation_paused) {
            try { advance_scene_animations(seconds); }
            catch(std::exception const& error) {
                OutputDebugStringA(error.what());
                std::fprintf(stderr,"Project animation update failed: %s\n",error.what());
                script_suspended=true;
            }
        }
        });
        auto const down = [&state](unsigned code) {
            return code < NORTHSTARCPP_INPUT_MAX_KEYS && state.keys[code] != 0;
        };
        bool const foreground = game_has_foreground() && state.focused;
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
            !simulation_paused && !pie_ejected &&
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
                            if(body.mass>0.f && !scene_pawn::is_pawn(preview->scene(),body))
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
            std::optional<Scene> authored;
            if(scene_streaming.world() || !gizmo_scene)authored.emplace(document.snapshot());
            frame_profile.measure(diagnostics::FrameStage::scene_streaming,[&] {
                if(authored)update_scene_world(*authored,seconds);
            });
            auto const& active_scene=gizmo_scene?*gizmo_scene:*authored;
            auto const overlay=editor ? std::optional<ViewportGizmo>(active_gizmo(active_scene)) :
                std::optional<ViewportGizmo>{};
            frame_profile.measure(diagnostics::FrameStage::rendering, [&] { graphics->draw(active_scene, selected_index(), false,
                           nullptr,nullptr,false,editor ? &editor_camera.pose : nullptr,
                           scene_streaming.world(),true,show_streaming_overlay,
                           overlay ? &*overlay : nullptr); });
            return;
        }
        if (scripted_first_person) {
            if (!simulation_paused && !script_suspended)
                frame_profile.measure(diagnostics::FrameStage::physics, [&] {
                    preview->step(seconds,script_move_x,script_move_z);
                    advance_vehicles(seconds);
                });
            update_script_camera(script_paused ? 0.f : seconds);
            update_cinematic_postfx();
            Scene visual=preview->scene();
            apply_vehicle_visuals(visual);
            for (auto const& [index,color]:script_object_colors)
                if (index<visual.objects.size()) visual.objects[index].color=color;
            apply_scene_animation_visuals(visual);
            for(auto const& [id,payload]:script_model_visual_modes)
                visual.components.push_back(
                    {id,"engine.model.visual_mode",1,payload});
            for(auto const& [id,ref]:script_model_animations) {
                auto found=std::find_if(visual.components.begin(),
                    visual.components.end(),[&](auto const& component) {
                        return component.entity_id==id &&
                            component.type=="engine.model.animation" &&
                            component.version==1;
                    });
                if(found!=visual.components.end())found->payload=ref;
                else visual.components.push_back(
                    {id,"engine.model.animation",1,ref});
            }
            for(auto const& [id,payload]:script_model_bone_attachments)
                visual.components.push_back({id,"engine.model.bone_attachment",1,payload});
            frame_profile.measure(diagnostics::FrameStage::scene_streaming, [&] { update_scene_world(seconds); });
            apply_script_character(visual,script_paused ? 0.f : seconds);
            frame_profile.measure(diagnostics::FrameStage::rendering, [&] { graphics->draw(visual,-2,false,preview.get(),nullptr,script_paused,
                           fps_camera ? &fps_pose : nullptr,scene_streaming.world(),false); });
        } else if (fps) {
            if (!fps_menu_visible && !simulation_paused && !pie_ejected &&
                foreground && fps->alive()) {
                fps->advance(seconds,preview->scene());
                for (auto const index : fps->moving_indices())
                    preview->move_static(index, fps->moving_position(index));
                auto const m = fps->movement(x,z);
                frame_profile.measure(diagnostics::FrameStage::physics, [&] { preview->step(seconds,m.x,m.z); });
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
            update_cinematic_postfx();
            Scene visual = preview->scene();
            fps->tint(visual);
            frame_profile.measure(diagnostics::FrameStage::scene_streaming, [&] { update_scene_world(seconds); });
            apply_script_character(visual,fps_menu_visible ? 0.f : seconds);
            frame_profile.measure(diagnostics::FrameStage::rendering, [&] { graphics->draw(visual,-2,false,preview.get(),fps.get(),fps_menu_visible,
                           fps_camera ? &fps_pose : nullptr,scene_streaming.world(),
                           !config.explore); });
            update_fps_title();
        } else {
            if(!simulation_paused)
                frame_profile.measure(diagnostics::FrameStage::physics, [&] { preview->step(seconds,scripted_game ? script_move_x : x,
                                   scripted_game ? script_move_z : z); });
            if(!simulation_paused && !script_suspended)advance_vehicles(seconds);
            frame_profile.measure(diagnostics::FrameStage::scene_streaming, [&] { update_scene_world(seconds); });
            Scene visual=preview->scene();
            apply_vehicle_visuals(visual);
            for (auto const& [index,color]:script_object_colors)
                if (index<visual.objects.size()) visual.objects[index].color=color;
            apply_scene_animation_visuals(visual);
            apply_script_character(visual,seconds);
            frame_profile.measure(diagnostics::FrameStage::rendering, [&] { graphics->draw(visual,-2,false,preview.get(),
                           nullptr,false,nullptr,scene_streaming.world(),
                           true,show_streaming_overlay && editor); });
        }
        frame_profile.measure(diagnostics::FrameStage::asset_streaming, [&] { update_asset_zones(); });
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
                !scene_pawn::is_local(document.snapshot(),document.object(document.at(0))))
                throw std::runtime_error("Editor inspector did not commit the scene");
            graphics->draw(document.snapshot(), selected_index(), true);
            file_path = config.smoke_output;
            save(false);
            click(play_scene);
        }
        auto const movable = [this](SceneObject const& object) {return scene_pawn::is_local(preview->scene(),object);};
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
        if (!config.smoke && !config.check_scene && !error_dialog_active) {
            // Prevent recursively queued GUI/timer messages from generating
            // nested copies of the same failure dialog.
            error_dialog_active = true;
            MessageBoxW(window, message.c_str(), L"NorthStarCpp", MB_OK | MB_ICONERROR);
            error_dialog_active = false;
        }
    }

    Options config;
    diagnostics::StartupReport* startup_report_=nullptr;
    bool runtime_faulted = false;
    mutable bool error_dialog_active = false;
    std::unique_ptr<diagnostics::FrameProfiler> frame_profiler;
    std::unique_ptr<ScriptRuntime> script_runtime;
    std::unique_ptr<SceneAnimationPlayer> script_animations;
    std::unique_ptr<VehicleService> script_vehicle;
    std::unordered_map<std::uint32_t,VehicleRigAsset> script_vehicle_rigs;
    std::unordered_map<std::uint32_t,std::vector<std::uint32_t>> script_vehicle_visual_indices;
    std::unordered_map<std::uint32_t,std::vector<std::uint32_t>> script_vehicle_door_visual_indices;
    struct VehicleVisualFollower {
        std::uint32_t child_index=0, parent_index=0;
        Vector3 local_offset{}, local_rotation{};
    };
    // Resolved once per loaded rig, then updated in the existing vehicle
    // render pass. A composite mesh never creates extra Jolt rigid bodies.
    std::unordered_map<std::uint32_t,std::vector<VehicleVisualFollower>>
        script_vehicle_visual_followers;
    struct VehicleTarget {
        float x,z,speed,radius;
    };
    std::unordered_map<std::uint32_t,VehicleTarget> script_vehicle_targets;
    std::unordered_set<std::uint32_t> script_vehicle_native;
    std::vector<SceneAnimationSample> script_animation_samples;
    std::unordered_map<std::size_t,SceneAnimationSample> script_animation_visuals;
    std::unique_ptr<AssetStore> script_assets;
    std::unique_ptr<EcsWorld> script_ecs;
    std::unordered_map<std::uint64_t,std::size_t> script_entity_indices;
    std::unordered_map<NsEntity,std::uint64_t> script_handle_ids;
    std::unordered_map<std::uint64_t,NsEntity> script_spawn_handles;
    std::unordered_map<std::uint64_t,std::uint64_t> script_spawn_owners;
    std::uint64_t next_script_spawn_id=0;
    std::uint64_t script_context_epoch=0;
    std::uint64_t active_script_context_epoch=0;
    fs::path script_source_root;
    fs::path requested_project_scene_; // deferred generic scene transition
    std::map<fs::path,fs::file_time_type> script_file_stamps;
    Clock::time_point script_probe_last{};
    bool script_entry_exists=false;
    bool script_suspended=false;
    std::wstring script_window_title;
    UiDocument script_ui_document_{};
    std::unordered_map<std::string,UiDocument> script_world_ui_documents_;
    bool scripted_game=false;
    bool scripted_first_person=false;
    bool script_paused=false;
    // Editor-controlled simulation pause and possession are independent of
    // game-authored script pause. Neither survives Player termination.
    bool pie_paused=false;
    bool pie_ejected=false;
    northstarcpp::editor::PieFrameStepper pie_stepper;
    float script_move_x=0.f, script_move_z=0.f;
    float script_yaw=0.f, script_pitch=0.f, script_eye_height=1.45f;
    std::optional<NorthStarRenderPostFxSettings> cinematic_postfx_original_;
    float cinematic_postfx_applied_ev_=1000.f;
    bool cinematic_postfx_unavailable_=false;
    float script_mouse_dx=0.f, script_mouse_dy=0.f;
    std::int32_t script_wheel_vertical=0;
    std::array<std::uint8_t,5> script_mouse_buttons{};
    std::array<NorthStarInputGamepad,NORTHSTARCPP_INPUT_MAX_GAMEPADS> script_gamepads{};
    std::unordered_map<std::size_t,std::uint32_t> script_object_colors;
    // Ephemeral per-entity YCD selection. The authored scene is unchanged.
    std::unordered_map<std::uint64_t,std::string> script_model_animations;
    std::unordered_map<std::uint64_t,std::string> script_model_visual_modes;
    std::unordered_map<std::uint64_t,std::string> script_model_bone_attachments;
    std::unordered_map<std::string,FpsAudioClip> script_audio_cache;
    std::unordered_map<std::string,std::vector<nac::Track>> script_nac_cache;
    std::unique_ptr<AudioEngine> script_voice_audio;
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
    std::string graphics_detail_level="cinematic";
    EcsWorld document;
    window_detail::WindowSceneStreaming scene_streaming;
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
    struct CharacterPoseRequest {
        std::size_t scene_index=0;
        float heading=0.f;
        std::string animation;
        bool first_person_arms=false;
    };
    std::optional<CharacterAssetSpec> script_character;
    std::optional<CharacterPoseRequest> script_character_pose;
    bool script_character_dirty=false;
    bool script_character_renderable=false;
    std::unique_ptr<CameraDirector> fps_camera;
    CameraPose fps_pose{};
    HWND fps_caption = nullptr;
    std::array<HWND, 3> fps_buttons{};
    bool ui_mouse_down = false;
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
    bool startup_sink_bound=false;
    northstarcpp::diagnostics::StartupReport startup;
    startup.info("process.begin",
        std::string("host=")+(NORTHSTARCPP_EDITOR?"editor":"player")+
        " pid="+std::to_string(GetCurrentProcessId())+" platform=Windows");
    try {
        auto settings = startup.measure("arguments.parse", [] {
            return northstarcpp::options();
        });
        arguments_parsed = true;
        smoke = settings.smoke || settings.check_scene || settings.runtime_init_smoke ||
                settings.fps_ui_smoke || settings.character_smoke || settings.check_shared_assets ||
                settings.inspect_providers;
        startup.info("arguments.summary",
            std::string("scene=")+settings.scene_path.u8string()+
            " archive="+settings.archive_path.u8string()+
            " fps="+(settings.fps?"true":"false")+
            " warp="+(settings.warp?"true":"false")+
            " smoke="+(smoke?"true":"false")+
            " packaged="+(settings.packaged_game?"true":"false"));
        // Test/validation runs do not create log files unless explicitly
        // redirected. The bootstrap report still goes to stderr in that case.
        if (!smoke || northstarcpp::application_log_directory_overridden()) {
            try {
                startup.measure("logger.initialize", [&] {
                    auto const logging=northstarcpp::application_logger_configuration(
                        NORTHSTARCPP_EDITOR?"NorthStarCpp-Editor":"NorthStarCpp-Player");
                    if(!logging.enabled) {
                        startup.warning("logger.initialize",
                            "engine.logger is disabled by bootstrap.json");
                        return;
                    }
                    northstarcpp::prepare_application_log_console(logging);
                    process_logger=std::make_unique<northstarcpp::Logger>(logging.options);
                    auto folder=logging.options.directory;
                    if(folder.empty()) {
                        std::array<wchar_t,32768> env{};
                        auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",
                            env.data(),static_cast<DWORD>(env.size()));
                        if(length==0 || length>=env.size()) {
                            length=GetTempPathW(static_cast<DWORD>(env.size()),env.data());
                            if(length==0 || length>=env.size())
                                throw std::runtime_error("Cannot locate startup report directory");
                        }
                        folder=std::filesystem::path(env.data())/
                            L"KaylasSystems"/L"NorthStarCpp"/L"Logs";
                    }
                    auto const stem=logging.options.session+"-"+
                        std::to_string(GetCurrentProcessId());
                    startup.set_report_path(folder/(stem+"-startup.json"));
                });
                if(process_logger) {
                    startup.set_sink([&](auto severity,std::string const& text) {
                        using Severity=northstarcpp::diagnostics::StartupReport::Severity;
                        process_logger->write(severity==Severity::error?
                            northstarcpp::LogLevel::error:
                            severity==Severity::warning?northstarcpp::LogLevel::warn:
                            northstarcpp::LogLevel::info,"engine.startup",text);
                    });
                    startup_sink_bound=true;
                    startup.info("logger.provider",
                        "selected="+process_logger->provider_name()+
                        " version="+process_logger->provider_version()+
                        " report="+startup.report_path().u8string());
                }
            } catch (std::exception const& error) {
                startup.warning("logger.initialize",
                    std::string("Fallback to stderr: ")+error.what());
                OutputDebugStringA(error.what());
            }
        } else {
            startup.info("logger.initialize","smoke mode; stderr only");
        }
        if (settings.inspect_providers) {
            startup.info("providers.inspect","read-only ABI negotiation");
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
            startup.complete("READY");
            if(process_logger)process_logger->flush();
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
            startup.info("assets.inventory",
                "materials="+std::to_string(assets.materials)+
                " drawables="+std::to_string(assets.drawables)+
                " textures="+std::to_string(assets.textures)+
                " missing="+std::to_string(assets.missing_references.size()));
            startup.complete(assets.missing_references.empty()?"READY":"READY_WITH_WARNINGS");
            if(process_logger)process_logger->flush();
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
            startup.info("editor.manager","launched="+manager.u8string());
            startup.complete("READY");
            if(process_logger)process_logger->flush();
            return 0;
        }
#endif
        if(settings.embed_handshake_smoke) {
            // Test the actual cross-process HWND parenting and Escape
            // notification without requiring a GPU or loading a city.
            WNDCLASSW type{};
            type.hInstance=instance;
            type.lpszClassName=L"NorthStarCppWindow";
            type.lpfnWndProc=DefWindowProcW;
            if(!RegisterClassW(&type))
                throw std::runtime_error("Cannot register embedded smoke window");
            RECT area{};
            GetClientRect(settings.embed_parent,&area);
            HWND const embedded=CreateWindowExW(0,type.lpszClassName,
                L"NorthStar embedded handshake",WS_CHILD|WS_VISIBLE,
                0,0,std::max(1L,area.right),std::max(1L,area.bottom),
                settings.embed_parent,nullptr,instance,nullptr);
            if(!embedded)
                throw std::runtime_error("Cannot create embedded Player HWND");
            DWORD parent_pid=0,child_pid=0;
            GetWindowThreadProcessId(settings.embed_parent,&parent_pid);
            GetWindowThreadProcessId(embedded,&child_pid);
            if(parent_pid==child_pid || child_pid!=GetCurrentProcessId())
                throw std::runtime_error("Embedded HWND is not cross-process");
            Sleep(120); // parent Qt timer must observe the embedded child
            UINT const notify=RegisterWindowMessageW(
                ::northstarcpp::editor::pie_protocol::exit_request);
            if(!notify || !PostMessageW(settings.embed_parent,notify,0,0)) {
                DestroyWindow(embedded);
                throw std::runtime_error("Embedded Player Escape notification failed");
            }
            Sleep(300); // allow Qt to discover the real child HWND
            DestroyWindow(embedded);
            startup.complete("READY");
            return 0;
        }
        if(settings.runtime_init_smoke) {
            // Exercise the REAL Player startup pipeline: physics, scene-v3
            // streaming mounts, zones and scripts. Unlike --check-scene,
            // this runs the App constructor but opens no window.
            startup.measure("application.construct", [&] {
                northstarcpp::App initialized(std::move(settings),&startup);
            });
            startup.complete(startup.warning_count()?"READY_WITH_WARNINGS":"READY");
            if(process_logger)process_logger->flush();
            std::puts("Player runtime asset initialization: PASS");
            return 0;
        }
        auto app=startup.measure("application.construct", [&] {
            return std::make_unique<northstarcpp::App>(std::move(settings),&startup);
        });
        int const result=app->run(instance);
        if(!startup.completed())
            startup.complete(result?"FAILED":
                (startup.warning_count()?"READY_WITH_WARNINGS":"READY"));
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
                    auto folder=logging.options.directory;
                    if(folder.empty()) {
                        std::array<wchar_t,32768> env{};
                        auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",
                            env.data(),static_cast<DWORD>(env.size()));
                        if(!length || length>=env.size()) {
                            length=GetTempPathW(static_cast<DWORD>(env.size()),env.data());
                            if(!length || length>=env.size())
                                throw std::runtime_error("No bootstrap report directory");
                        }
                        folder=std::filesystem::path(env.data())/
                            L"KaylasSystems"/L"NorthStarCpp"/L"Logs";
                    }
                    startup.set_report_path(folder/
                        (logging.options.session+"-"+
                            std::to_string(GetCurrentProcessId())+"-startup.json"));
                }
            } catch(std::exception const& logging_error) {
                std::fprintf(stderr,"Logger emergency initialization: %s\n",
                             logging_error.what());
            }
        }
        if(process_logger && !startup_sink_bound) {
            startup.set_sink([&](auto severity,std::string const& text) {
                using Severity=northstarcpp::diagnostics::StartupReport::Severity;
                process_logger->write(severity==Severity::error?
                    northstarcpp::LogLevel::error:
                    severity==Severity::warning?northstarcpp::LogLevel::warn:
                    northstarcpp::LogLevel::info,"engine.startup",text);
            });
            startup_sink_bound=true;
        }
        startup.failure("bootstrap",error.what());
        startup.complete("FAILED");
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
