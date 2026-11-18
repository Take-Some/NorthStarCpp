#include "window_script_domain_contracts.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <northstarcpp/player_camera_runtime.hpp>
#include <northstarcpp/physics_surface.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <stdexcept>
#include <string>
namespace northstarcpp::window_app_internal {
void collect_player_motion_script_binding(PlayerScriptBindings& bindings, PlayerMotionStateView view) {
    if(!view.move_x || !view.move_z) throw std::invalid_argument("Invalid player ScriptRuntime view");
        bindings.add("motion",[view](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty())
                throw std::invalid_argument("player.motion()");
            return std::string("{\"x\":")+std::to_string(*view.move_x)+
                ",\"z\":"+std::to_string(*view.move_z)+"}";
        });
}

void collect_player_runtime_script_bindings(
    PlayerScriptBindings& bindings, PlayerDomainCapabilities service) {
    if(!service.physics_slot || !service.camera_slot || !service.scripted_game ||
       !service.camera_settings || !service.eye_height || !service.move_x ||
       !service.move_z || !service.scene_generation)
        throw std::invalid_argument("Invalid Player domain capabilities");
        bindings.add("controlledEntity",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty() || !service.physics())
                throw std::invalid_argument("player.controlledEntity()");
            auto const id=service.physics()->controlled_entity();
            return id?runtime_project_files::script_json_quote(std::to_string(id)):std::string("null");
        });
        bindings.add("viewTarget",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty() || !service.physics())
                throw std::invalid_argument("player.viewTarget()");
            const auto id=service.physics()->view_target();
            return id?runtime_project_files::script_json_quote(std::to_string(id))
                     :std::string("null");
        });
        bindings.add("setViewTarget",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !service.physics() || !*service.scripted_game ||
               args[0].empty() ||
               !std::all_of(args[0].begin(),args[0].end(),
                   [](unsigned char c){return c>='0' && c<='9';}))
                throw std::invalid_argument("player.setViewTarget(entityId)");
            auto const id=std::stoull(args[0]);
            service.physics()->set_view_target(id);
            // Switch the native CameraDirector rig without changing Pawn ownership.
            if(service.camera()) {
                (void)gameplay::configure_view_target_camera(*service.camera(),
                    service.physics()->scene(),id);
                *service.camera_settings=service.camera()->settings();
            }
            return std::string("true");
        });
        bindings.add("resetViewTarget",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty() ||
               !service.physics() || !*service.scripted_game)
                throw std::invalid_argument("player.resetViewTarget()");
            service.physics()->reset_view_target();
            if(service.camera()) {
                (void)gameplay::configure_view_target_camera(*service.camera(),
                    service.physics()->scene(),service.physics()->view_target());
                *service.camera_settings=service.camera()->settings();
            }
            return std::string("null");
        });
        bindings.add("controlledIndex",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty() || !service.physics())
                throw std::invalid_argument("player.controlledIndex()");
            auto index=service.physics()->controlled_index();
            return index?std::to_string(*index):std::string("-1");
        });
        bindings.add("animationState",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty()||!service.physics())
                throw std::invalid_argument("player.animationState()");
            auto frame=service.physics()->player_animation();
            if(!frame)return std::string("null");
            return std::string("{\"pawn\":")+std::to_string(frame->pawn_id)+
                ",\"mesh\":"+std::to_string(frame->mesh_id)+
                ",\"state\":"+runtime_project_files::script_json_quote(frame->state)+
                ",\"clip\":"+runtime_project_files::script_json_quote(frame->clip)+
                ",\"previousClip\":"+runtime_project_files::script_json_quote(frame->previous_clip)+
                ",\"blend\":"+std::to_string(frame->blend_alpha)+
                ",\"time\":"+std::to_string(frame->playback_seconds)+
                ",\"grounded\":"+(frame->grounded?"true":"false")+"}";
        });
        bindings.add("possess",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !service.physics() || !*service.scripted_game ||
               args[0].empty() ||
               !std::all_of(args[0].begin(),args[0].end(),
                   [](unsigned char ch){return ch>='0' && ch<='9';}))
                throw std::invalid_argument("player.possess(entityId)");
            auto const id=std::stoull(args[0]);
            service.physics()->possess(id);
            auto const tune=gameplay::camera_rig(service.physics()->scene(),id);
            *service.eye_height=tune.eye_height;
            auto const fov=std::clamp(tune.fov_degrees,25.f,120.f);
            service.camera_settings->default_fov_y=fov;
            if(service.camera()) {
                auto settings=service.camera()->settings();
                settings.default_fov_y=fov;
                service.camera()->configure(settings);
                (void)gameplay::configure_player_camera(
                    *service.camera(),service.physics()->scene(),id);
                service.camera()->reset();
            }
            return std::string("true");
        });
        bindings.add("unpossess",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty() || !service.physics() || !*service.scripted_game)
                throw std::invalid_argument("player.unpossess()");
            service.physics()->unpossess();
            *service.move_x=*service.move_z=0.f;
            if(service.camera())service.camera()->reset();
            return std::string("null");
        });
        bindings.add("sprint",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !service.physics() || !*service.scripted_game ||
               (args[0]!="true" && args[0]!="false"))
                throw std::invalid_argument("player.sprint(active)");
            service.physics()->set_sprinting(args[0]=="true");
            return std::string("null");
        });
        bindings.add("move",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=2 || !*service.scripted_game)
                throw std::invalid_argument("player.move(worldX,worldZ)");
            auto x=runtime_script_contracts::script_number(args[0]),z=runtime_script_contracts::script_number(args[1]);
            if (std::abs(x)>1 || std::abs(z)>1)
                throw std::invalid_argument("Player movement components must be -1..1");
            *service.move_x=static_cast<float>(x);
            *service.move_z=static_cast<float>(z);
            return std::string("null");
        });
        collect_player_motion_script_binding(bindings, PlayerMotionStateView{
            service.move_x, service.move_z
        });
        bindings.add("motionState",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty())
                throw std::invalid_argument("player.motionState()");
            service.require_scene();
            if(!service.motion_tracker || !service.motion_tracker->active())
                return std::string("null");
            auto const& m=service.motion_tracker->motion();
            return std::string("{\"pawn\":")+
                runtime_project_files::script_json_quote(std::to_string(m.pawn))+
                ",\"state\":"+runtime_project_files::script_json_quote(m.state)+
                ",\"horizontalSpeed\":"+std::to_string(m.horizontal_speed)+
                ",\"verticalSpeed\":"+std::to_string(m.vertical_speed)+
                ",\"fallDistance\":"+std::to_string(m.fall_distance)+
                ",\"impactSpeed\":"+std::to_string(m.impact_speed)+
                ",\"grounded\":"+(m.grounded?"true":"false")+
                ",\"moving\":"+(m.moving?"true":"false")+
                ",\"landed\":"+(m.landed?"true":"false")+"}";
        });
        bindings.add("ground",[service](std::string_view json) {
            if(!runtime_script_contracts::script_args(json).empty() || !service.physics())
                throw std::invalid_argument("player.ground()");
            auto const& objects=service.physics()->scene().objects;
            auto idx=service.physics()->controlled_index();
            if(!idx || *idx>=objects.size())
                return std::string("{\"grounded\":false,\"surface\":\"stone\"}");
            auto const index=*idx;
            auto const& pos=objects[index].position; // Pawn feet
            auto const from=Vector3{pos.x,pos.y+0.22f,pos.z};
            auto const to=Vector3{pos.x,pos.y-0.32f,pos.z};
            auto const hit=service.physics()->sweep_sphere(from,to,0.10f,index);
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
                runtime_project_files::script_json_quote(surface)+
                ",\"materialId\":"+
                std::to_string(contact?hit->material_id:0)+
                ",\"triangleIndex\":"+
                std::to_string(contact?hit->triangle_index:UINT32_MAX)+
                ",\"objectIndex\":" + std::to_string(hit_index)+
                "}";
        });
        bindings.add("jump",[service](std::string_view json) {
            if (!runtime_script_contracts::script_args(json).empty() || !service.physics() || !*service.scripted_game)
                throw std::invalid_argument("player.jump()");
            service.physics()->jump();
            return std::string("null");
        });
        bindings.add("eye",[service](std::string_view json) {
            if (!runtime_script_contracts::script_args(json).empty() || !service.physics())
                throw std::invalid_argument("player.eye()");
            auto const& objects=service.physics()->scene().objects;
            auto idx=service.physics()->controlled_index();
            if(!idx || *idx>=objects.size())
                throw std::runtime_error("No possessed Pawn in scene");
            auto const eye=gameplay::camera_anchor(
                service.physics()->scene(),objects[*idx].node.id,*service.eye_height);
            return std::string("{\"x\":")+std::to_string(eye.x)+
                ",\"y\":"+std::to_string(eye.y)+
                ",\"z\":"+std::to_string(eye.z)+"}";
        });
        bindings.add("setEyeHeight",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=1) throw std::invalid_argument("player.setEyeHeight(height)");
            auto height=runtime_script_contracts::script_number(args[0]);
            if (height<0 || height>5) throw std::invalid_argument("Eye height must be 0..5");
            *service.eye_height=static_cast<float>(height);
            return std::string("null");
        });
}

void register_player_script_bindings(ScriptRuntime& runtime,
                                   PlayerScriptBindings&& handlers) {
    std::move(handlers).bind_all(runtime);
}
} // namespace northstarcpp::window_app_internal
