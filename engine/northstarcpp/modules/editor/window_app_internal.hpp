#pragma once
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
#include "window_runtime_services.hpp"
#include "window_runtime_message_pump.hpp"
#include "window_runtime_app_state.hpp"
#include "window_win32_controls.hpp"
#include "window_runtime_frame_scheduler.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_script_lifetime.hpp"
#include "window_fps_nui.hpp"
#include "../shared/ui_xml_codec.hpp"
#include "window_runtime_project_files.hpp"
#include "crash_handler.hpp"
#include "../shared/frame_profiler.hpp"
#include "../shared/startup_report.hpp"
#include "resource_ids.h"

#include <northstarcpp/physics.hpp>
#include <northstarcpp/world_foundation.hpp>
#include "../host/terrain_physics_bridge.hpp"
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/player_controller.hpp>
#include <northstarcpp/player_action_events.hpp>
#include <northstarcpp/player_camera_runtime.hpp>
#include <northstarcpp/scene_camera_actor.hpp>
#include <northstarcpp/physics_surface.hpp>
#include <northstarcpp/audio.hpp>
#include <northstarcpp/nac.hpp>
#include <northstarcpp/logger.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/sky_project_config.hpp>
#include <northstarcpp/input.hpp>
#include <northstarcpp/input_actions.hpp>
#include <northstarcpp/input_actions_xml.hpp>
#include <northstarcpp/ai_controller.hpp>
#include <northstarcpp/navigation_navmesh.hpp>
#include <northstarcpp/navigation_crowd.hpp>
#include <northstarcpp/player_animation_controller.hpp>
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/fps_weapon_effects.hpp>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/scene_camera_frustum.hpp>
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
#include <northstarcpp/sky_environment.hpp>
#include <northstarcpp/vehicle_service.hpp>
#include "../host/vehicle_rig_xml.hpp"
#include <boost/property_tree/json_parser.hpp>

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
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
namespace window_app_internal {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
inline constexpr bool editor = NORTHSTARCPP_EDITOR != 0;
inline constexpr wchar_t window_class[] = L"NorthStarCppWindow";
inline constexpr wchar_t viewport_class[] = L"NorthStarCppViewport";
inline constexpr UINT frame_timer = 1;
inline constexpr UINT smoke_message = WM_APP + 1;

enum Control {
    new_scene = 100, open_scene, save_scene_id, save_as, add_object, delete_object,
    play_scene, camera_timeline, object_list, apply_properties, player_object,
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

using win32_controls::to_wide;
using win32_controls::to_utf8;
using win32_controls::get_text;
using win32_controls::parse_number;
using win32_controls::set_number;

using runtime_options::Options;
using runtime_options::discover_shared_assets;
inline Options options() { return runtime_options::parse_options(editor); }

inline Scene read_document(fs::path const& path)
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

// All mutable App members live in one private state base.
#include "window_app_state.hpp"
class App : private AppRuntimeState {
private:
    // Defined before the constructor because placeholder return-type
    // deduction is not available until this template definition is seen.
    template<class F>
    decltype(auto) bootstrap_stage(char const* phase,F&& fn) {
        if(startup_report_)
            return startup_report_->measure(phase,std::forward<F>(fn));
        return std::forward<F>(fn)();
    }
    void apply_scene_sky() {
        graphics->set_sky_asset_reference({});
        graphics->set_sky_environment(SkyEnvironment{});
        if(!sky_drawable_reference_.empty()) {
            try {
                graphics->set_sky_asset_reference(sky_drawable_reference_);
            } catch(std::exception const& error) {
                bootstrap_warning("renderer.sky",
                    std::string("Authored sky resource unavailable: ")+error.what());
                graphics->set_sky_asset_reference({});
            }
        }
        if(sky_environment_.settings().enabled)
            graphics->set_sky_environment(sky_environment_);
        sky_visual_dirty_=false;
    }

    void load_scene_sky() {
        project_sky::Configuration profile;
        try {
            AssetStore assets;
            fs::path root;
            if(config.archive_path.empty()) {
                root=project_sky::find_root(file_path);
                assets.mount_project_assets(root,discover_shared_assets(config,root));
            } else {
                assets.mount_archive("",config.archive_path,
                    config.archive_data.empty()?config.archive_path:config.archive_data);
            }
            profile=project_sky::load(assets,project_sky::scene_reference(root,file_path));
        } catch(std::exception const& error) {
            bootstrap_warning("sky.project",
                std::string("Sky configuration ignored: ")+error.what());
        }
        sky_scene_disabled_=profile.explicitly_disabled;
        sky_mode_=std::move(profile.mode);
        sky_drawable_reference_=std::move(profile.drawable);
        sky_environment_=SkyEnvironment(profile.settings);
        if(startup_report_)
            startup_report_->info("sky.config",
                "scene="+project_sky::scene_reference(
                    project_sky::find_root(file_path),file_path)+
                " mode="+sky_mode_+
                " enabled="+(sky_environment_.settings().enabled?"true":"false")+
                " clouds="+(sky_environment_.settings().clouds?"true":"false"));
        sky_visual_elapsed_=0;
        sky_visual_dirty_=true;
        if(graphics)apply_scene_sky();
    }
public:
    // Stateful App methods remain in the same class until Pimpl extraction.
#include "window_runtime_bootstrap.inc"

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
                bootstrap_stage("fps.ui_asset", [&] {create_fps_menu();});
            bootstrap_stage("window.layout", [&] {layout();});
            bootstrap_stage("renderer.provider", [&] {
                graphics=std::make_unique<Viewport>(surface,config.warp);
            graphics->set_detail_level(graphics_detail_level);
                // The project script is started before the renderer provider.
                // Replay validated queued YCD commands once on the actual
                // viewport, retaining the original per-clip playback rate.
                for(auto const& [id,clip]:script_model_animations) {
                    if(clip.empty())continue;
                    auto const rate=script_model_animation_rates.find(id);
                    graphics->set_scene_animation_override(
                        id,clip,rate==script_model_animation_rates.end()?
                            1.f:rate->second);
                }
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
                    config.archive_path.empty()?project_sky::find_root(file_path):fs::path{},
                    config.archive_path,config.archive_data,
                    discover_shared_assets(config,file_path.empty()
                        ? fs::path{} : project_sky::find_root(file_path)));
            });
            bootstrap_stage("renderer.sky", [&] {apply_scene_sky();});
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

        auto const exit_code=runtime_win32::run_message_loop(
            window,accelerators,editor);
        if (accelerators)
            DestroyAcceleratorTable(accelerators);
        return exit_code;
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
    // Stateful App methods remain in the same class until Pimpl extraction.
#include "window_runtime_win32_viewport.inc"

    // Stateful App methods remain in the same class until Pimpl extraction.
#include "window_runtime_scene_streaming.inc"
#include "window_runtime_procedural_terrain.inc"

#include "window_runtime_fps_bridge.inc"

    // Stateful App methods remain in the same class until Pimpl extraction.
#include "window_runtime_win32_messages.inc"

#include "window_editor_workspace.inc"

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
        script_model_animation_rates.erase(id);
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
    // Defined out of line in window_script_bindings.cpp.
    void configure_script_bindings(ScriptRuntime& runtime);

#include "window_runtime_script_lifecycle.inc"

    // Stateful App methods remain in the same class until Pimpl extraction.
#include "window_runtime_frame_loop.inc"

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


};

} // namespace window_app_internal
using window_app_internal::App;
using window_app_internal::options;
} // namespace northstarcpp
