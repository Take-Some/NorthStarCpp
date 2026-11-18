#include "window_script_domain_contracts.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include "viewport.hpp"
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/scene_animation.hpp>
#include <northstarcpp/math.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <stdexcept>
#include <string>
namespace northstarcpp::window_app_internal {
void collect_scene_count_script_binding(SceneScriptBindings& bindings, SceneCountStateView view) {
    if(!view.object_count) throw std::invalid_argument("Invalid scene ScriptRuntime view");
        bindings.add("objectCount",
            [view](std::string_view json) {
                if (!runtime_script_contracts::script_args(json).empty())
                    throw std::invalid_argument("objectCount expects no arguments");
                return std::to_string(view.object_count());
            });
}

void collect_scene_runtime_script_bindings(
    SceneScriptBindings& bindings, SceneDomainCapabilities service) {
    if(!service.physics_slot || !service.ecs_slot || !service.assets_slot ||
       !service.graphics_slot || !service.camera_slot || !service.camera_pose ||
       !service.indices || !service.handles || !service.spawn_handles_slot ||
       !service.owners || !service.next_id || !service.colors ||
       !service.animations || !service.visual_modes || !service.attachments ||
       !service.scene_generation || !service.destroy_entity ||
       !service.layer_available || !service.set_layer_visible)
        throw std::invalid_argument("Invalid Scene domain capabilities");
        bindings.add("spawn",
            [service](std::string_view json) {
                auto args=runtime_script_contracts::script_args(json);
                if ((args.size()!=9 && args.size()!=10 && args.size()!=11 && args.size()!=14) ||
                    !service.physics() || !service.ecs() ||
                    !service.physics()->supports_runtime_entities())
                    throw std::invalid_argument("scene.spawn(name,x,y,z,size,mass,color,asset,layer,parent)");
                SceneObject object;
                object.name=args[0];
                if (object.name.empty() || object.name.size()>=NORTHSTARCPP_ECS_NAME_BYTES)
                    throw std::invalid_argument("Invalid runtime entity name");
                auto finite_coordinate=[](std::string const& input) {
                    auto const v=runtime_script_contracts::script_number(input);
                    if (std::abs(v)>1000000.)
                        throw std::invalid_argument("Runtime position out of bounds");
                    return static_cast<float>(v);
                };
                object.position={finite_coordinate(args[1]),finite_coordinate(args[2]),
                                 finite_coordinate(args[3])};
                object.size=static_cast<float>(runtime_script_contracts::script_number(args[4]));
                object.mass=static_cast<float>(runtime_script_contracts::script_number(args[5]));
                auto const color=runtime_script_contracts::script_number(args[6]);
                if (color<0. || color>0xFFFFFFu || std::floor(color)!=color)
                    throw std::invalid_argument("Invalid runtime RGB color");
                object.color=static_cast<std::uint32_t>(color);
                object.node.asset=args[7];
                object.node.layer=args[8];
                if(args.size()==11 || args.size()==14) {
                    if(args[10]!="true" && args[10]!="false")
                        throw std::invalid_argument("Runtime collision must be boolean");
                    object.node.collision=args[10]=="true";
                }
                if(args.size()==14) {
                    const Vector3 visual_scale{
                        static_cast<float>(runtime_script_contracts::script_number(args[11])),
                        static_cast<float>(runtime_script_contracts::script_number(args[12])),
                        static_cast<float>(runtime_script_contracts::script_number(args[13]))};
                    if(!visual_scale.valid() || visual_scale.x<=0.f ||
                       visual_scale.y<=0.f || visual_scale.z<=0.f ||
                       visual_scale.x>100.f || visual_scale.y>100.f ||
                       visual_scale.z>100.f)
                        throw std::invalid_argument("Runtime visual scale must be positive <=100");
                    object.node.scale=visual_scale;
                }
                if (object.node.layer.empty() || object.node.layer.size()>96)
                    throw std::invalid_argument("Invalid runtime entity layer");
                if (args.size()>=10 && args[9]!="0") {
                    auto const parent=runtime_script_contracts::script_index(args[9]);
                    auto const found=service.spawn_owners().find(parent);
                    if (found==service.spawn_owners().end() || found->second!=service.owner ||
                        !service.physics()->runtime_alive(service.live_index(parent)))
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
                    if (!service.assets())
                        throw std::runtime_error("Project VFS unavailable");
                    (void)service.assets()->size(asset);
                }
                if (!*service.next_id)
                    for (auto const& previous:service.physics()->scene().objects)
                        *service.next_id=std::max(*service.next_id,previous.node.id);
                if (*service.next_id==UINT64_MAX)
                    throw std::runtime_error("Exhausted runtime entity IDs");
                object.node.id=++*service.next_id;
                auto const index=service.physics()->spawn_runtime(object);
                if(std::getenv("NORTHSTARCPP_QA_WEAPON_TRACE") &&
                   object.node.layer=="Weapon debris")
                    std::fprintf(stderr,
                        "[WEAPON FX TRACE] casing spawned index=%zu mass=%.4f"
                        " origin=(%.4f %.4f %.4f) asset=%s visual_scale=%.1f\n",
                        index,object.mass,object.position.x,
                        object.position.y,object.position.z,
                        object.node.asset.c_str(),object.node.scale.x);
                try {
                    auto const handle=service.ecs()->create_runtime(object);
                    service.entity_indices().emplace(object.node.id,index);
                    service.handle_ids().emplace(handle,object.node.id);
                    service.spawn_handles().emplace(object.node.id,handle);
                    service.spawn_owners().emplace(object.node.id,service.owner);
                } catch (...) {
                    service.physics()->destroy_runtime(index);
                    throw;
                }
                return std::string("{\"id\":")+
                    runtime_project_files::script_json_quote(std::to_string(object.node.id))+
                    ",\"index\":"+std::to_string(index)+"}";
            });
        bindings.add("destroy",
            [service](std::string_view json) {
                auto args=runtime_script_contracts::script_args(json);
                if (args.size()!=1)
                    throw std::invalid_argument("scene.destroy(id)");
                service.destroy_entity(runtime_script_contracts::script_index(args[0]),service.owner);
                return std::string("true");
            });
        bindings.add("moveRuntime",
            [service](std::string_view json) {
                auto args=runtime_script_contracts::script_args(json);
                if (args.size()!=4 || !service.physics() || !service.ecs())
                    throw std::invalid_argument("scene.moveRuntime(id,x,y,z)");
                auto const id=runtime_script_contracts::script_index(args[0]);
                if (!service.spawn_owners().count(id) ||
                    service.spawn_owners().at(id)!=service.owner)
                    throw std::out_of_range("Stale runtime entity ID");
                auto const index=service.live_index(id);
                if (service.physics()->scene().objects[index].mass!=0.f)
                    throw std::invalid_argument("Dynamic bodies must use physics");
                Vector3 p{static_cast<float>(runtime_script_contracts::script_number(args[1])),
                          static_cast<float>(runtime_script_contracts::script_number(args[2])),
                          static_cast<float>(runtime_script_contracts::script_number(args[3]))};
                if (std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>1000000.f)
                    throw std::invalid_argument("Runtime position out of bounds");
                service.physics()->move_static(index,p);
                NsPosition position{p.x,p.y,p.z};
                service.ecs()->set_component(service.spawn_handles().at(id),
                    NS_COMPONENT_POSITION,&position,sizeof(position));
                return std::string("null");
            });
        bindings.add("rotateRuntime",
            [service](std::string_view json) {
                auto args=runtime_script_contracts::script_args(json);
                if(args.size()!=4 || !service.physics())
                    throw std::invalid_argument("scene.rotateRuntime(id,x,y,z)");
                auto const id=runtime_script_contracts::script_index(args[0]);
                if(!service.spawn_owners().count(id) ||
                   service.spawn_owners().at(id)!=service.owner)
                    throw std::out_of_range("Stale runtime entity rotation request");
                service.physics()->rotate_runtime(service.live_index(id),
                    Vector3{static_cast<float>(runtime_script_contracts::script_number(args[1])),
                            static_cast<float>(runtime_script_contracts::script_number(args[2])),
                            static_cast<float>(runtime_script_contracts::script_number(args[3]))});
                return std::string("null");
            });
        bindings.add("setLayerVisible",
            [service](std::string_view json) {
                auto args=runtime_script_contracts::script_args(json);
                if (args.size()!=2 || !service.layer_available() ||
                    (args[1]!="true" && args[1]!="false"))
                    throw std::invalid_argument("setLayerVisible(name,bool)");
                service.set_layer_visible(args[0],args[1]=="true");
                return std::string("null");
            });
        bindings.add("objects",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()>2 || !service.physics()) throw std::invalid_argument("scene.objects(start,count)");
            auto start=args.empty()?std::size_t(0):runtime_script_contracts::script_index(args[0]);
            auto count=args.size()<2?std::size_t(256):runtime_script_contracts::script_index(args[1]);
            if (count>512) throw std::invalid_argument("Scene query page >512");
            std::string result="[";
            auto const& objects=service.physics()->scene().objects;
            for(auto i=start;i<objects.size() && i-start<count;++i) {
                if(i!=start) result+=',';
                auto const& obj=objects[i];
                if (i>=service.physics()->authored_object_count() && !service.physics()->runtime_alive(i)) {
                    result+="null";
                    continue;
                }
                result+="{\"index\":"+std::to_string(i)+
                    ",\"name\":"+runtime_project_files::script_json_quote(obj.name)+
                    ",\"id\":"+runtime_project_files::script_json_quote(std::to_string(obj.node.id))+
                    ",\"parent\":"+runtime_project_files::script_json_quote(std::to_string(obj.node.parent))+
                    ",\"script\":"+runtime_project_files::script_json_quote(obj.node.script)+
                    ",\"mass\":"+std::to_string(obj.mass)+
                    ",\"size\":"+std::to_string(obj.size)+
                    ",\"player\":"+(scene_pawn::is_local(service.physics()->scene(),obj)?"true":"false")+
                    ",\"position\":{\"x\":"+std::to_string(obj.position.x)+
                    ",\"y\":"+std::to_string(obj.position.y)+
                    ",\"z\":"+std::to_string(obj.position.z)+"}}";
            }
            return result+"]";
        });
        bindings.add("moveStatic",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=4 || !service.physics()) throw std::invalid_argument("scene.moveStatic(index,x,y,z)");
            auto i=runtime_script_contracts::script_index(args[0]);
            auto const& objs=service.physics()->scene().objects;
            if (i>=objs.size() || objs[i].mass!=0.f || scene_pawn::is_pawn(service.physics()->scene(),objs[i]))
                throw std::invalid_argument("Scene entity must be static");
            auto x=runtime_script_contracts::script_number(args[1]),y=runtime_script_contracts::script_number(args[2]),z=runtime_script_contracts::script_number(args[3]);
            if (std::max({std::abs(x),std::abs(y),std::abs(z)})>1000000)
                throw std::invalid_argument("Position out of bounds");
            Vector3 pos{static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)};
            service.physics()->move_static(i,pos);
            if(service.ecs() && i<service.ecs()->size()) {
                NsPosition updated{pos.x,pos.y,pos.z};
                service.ecs()->set_component(service.ecs()->at(i),NS_COMPONENT_POSITION,
                                          &updated,sizeof(updated));
            }
            return std::string("null");
        });
        bindings.add("rotateStatic",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.size()!=4 || !service.physics())
                throw std::invalid_argument("scene.rotateStatic(index,x,y,z)");
            auto i=runtime_script_contracts::script_index(args[0]);
            auto const& objects=service.physics()->scene().objects;
            if(i>=objects.size() || objects[i].mass!=0.f ||
               scene_pawn::is_pawn(service.physics()->scene(),objects[i]))
                throw std::invalid_argument("Scene rotation requires a static non-Pawn");
            auto x=runtime_script_contracts::script_number(args[1]),y=runtime_script_contracts::script_number(args[2]),
                 z=runtime_script_contracts::script_number(args[3]);
            if(std::max({std::abs(x),std::abs(y),std::abs(z)})>360000.)
                throw std::invalid_argument("Rotation is outside limits");
            service.physics()->rotate_runtime(i,Vector3{
                static_cast<float>(x),static_cast<float>(y),
                static_cast<float>(z)});
            return std::string("null");
        });
        bindings.add("setColor",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.size()!=2 || !service.physics()) throw std::invalid_argument("scene.setColor(index,rgb)");
            auto idx=runtime_script_contracts::script_index(args[0]),rgb=runtime_script_contracts::script_index(args[1]);
            if(idx>=service.physics()->scene().objects.size() || rgb>0xffffff)
                throw std::invalid_argument("Scene object/color invalid");
            service.object_colors()[idx]=static_cast<std::uint32_t>(rgb);
            return std::string("null");
        });
        bindings.add("setAnimation",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if((args.size()!=2 && args.size()!=3) || !service.physics())
                throw std::invalid_argument("scene.setAnimation(index,clip_reference[,playback_rate])");
            auto const index=runtime_script_contracts::script_index(args[0]);
            auto const& objects=service.physics()->scene().objects;
            if(index>=objects.size() ||
               (index>=service.physics()->authored_object_count() &&
                !service.physics()->runtime_alive(index)))
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
            auto const rate=args.size()==3 ?
                static_cast<float>(runtime_script_contracts::script_number(args[2])) : 1.f;
            if(!std::isfinite(rate) || rate<0.1f || rate>4.f)
                throw std::invalid_argument("Animation playback rate must be 0.1..4");
            if(!ref.empty()) {
                auto const sep=ref.rfind('@');
                if(ref.size()>512 || sep==std::string::npos || sep<4 ||
                   sep+1>=ref.size() || ref.substr(sep-4,4)!=".ycd" ||
                   ref.find("..")!=std::string::npos ||
                   ref.find('\\')!=std::string::npos ||
                   ref.find(':')!=std::string::npos ||
                   ref.find('\0')!=std::string::npos)
                    throw std::invalid_argument("Invalid native YCD clip reference");
                if(auto* graphics=service.graphics())
                    graphics->set_scene_animation_override(object.node.id,ref,rate);
                service.model_animations()[object.node.id]=ref;
                service.model_animation_rates()[object.node.id]=rate;
            } else {
                if(auto* graphics=service.graphics())
                    graphics->set_scene_animation_override(object.node.id,{},1.f);
                service.model_animations().erase(object.node.id);
                service.model_animation_rates().erase(object.node.id);
            }
            return std::string("null");
        });
        // Generic presentation mode for one existing skinned scene actor.
        // No character respawn, duplicate FPS prefab, or permanent scene edit.
        bindings.add("setVisualMode",
            [service](std::string_view json) {
                auto args=runtime_script_contracts::script_args(json);
                if((args.size()!=5 && args.size()!=6 && args.size()!=7 && args.size()!=9) || !service.physics())
                    throw std::invalid_argument(
                        "scene.setVisualMode(index,mode,forward,drop,pitch[,right[,ads]])");
                auto const index=runtime_script_contracts::script_index(args[0]);
                auto const& objects=service.physics()->scene().objects;
                if(index>=objects.size() ||
                   (index>=service.physics()->authored_object_count() &&
                    !service.physics()->runtime_alive(index)))
                    throw std::out_of_range("Invalid visual actor");
                auto const& actor=objects[index];
                // A saved SkeletalMesh is a legitimate project-owned visual;
                // it must not be re-instantiated merely to enable FPS arms.
                bool const authoredVisual=index<service.physics()->authored_object_count() &&
                    std::any_of(service.physics()->scene().components.begin(),
                                service.physics()->scene().components.end(),
                        [&](auto const& c) {
                            return c.entity_id==actor.node.id && c.version==1 &&
                                (c.type=="northstar.gameplay.skeletal_mesh" ||
                                 c.type=="northstar.editor.visual_prefab");
                        });
                bool const ownedRuntime=service.spawn_owners().count(actor.node.id) &&
                    service.spawn_owners().at(actor.node.id)==service.owner;
                if(!actor.node.id || actor.mass!=0.f ||
                   (!authoredVisual && !ownedRuntime))
                    throw std::invalid_argument(
                        "Visual mode requires an authored SkeletalMesh or an owned static model");
                if(args[1]=="world") {
                    service.model_visual_modes().erase(actor.node.id);
                    return std::string("null");
                }
                if(args[1]!="first_person_arms" && args[1]!="first_person_body")
                    throw std::invalid_argument("Unsupported visual mode");
                auto const model=actor.node.asset;
                if(model.size()<4 ||
                   model.substr(model.size()-4)!=".ydd")
                    throw std::invalid_argument("FPS arms require a skinned YDD");
                float const forward=static_cast<float>(runtime_script_contracts::script_number(args[2]));
                float const drop=static_cast<float>(runtime_script_contracts::script_number(args[3]));
                float const arm_pitch=static_cast<float>(runtime_script_contracts::script_number(args[4]));
                float const right=args.size()>=6?static_cast<float>(runtime_script_contracts::script_number(args[5])):0.f;
                if(!std::isfinite(forward) || forward<0.f || forward>3.f ||
                   !std::isfinite(drop) || drop<0.f || drop>3.f ||
                   !std::isfinite(arm_pitch) || arm_pitch< -180.f ||
                   arm_pitch>180.f || !std::isfinite(right) || std::abs(right)>1.f)
                    throw std::invalid_argument(
                        "FPS visual offsets outside allowed bounds");
                std::ostringstream payload;
                payload.imbue(std::locale::classic());
                payload << args[1] << ' ' << forward << ' '
                        << drop << ' ' << arm_pitch << ' ' << right;
                if(args.size()>=7) {
                    if(args[6]!="true" && args[6]!="false")
                        throw std::invalid_argument("Visual ADS flag must be boolean");
                    payload << ' ' << (args[6]=="true"?"ads":"ready");
                }
                if(args.size()==9) {
                    const float back=static_cast<float>(
                        runtime_script_contracts::script_number(args[7]));
                    const float arm=static_cast<float>(
                        runtime_script_contracts::script_number(args[8]));
                    if(!std::isfinite(back) || back<0.f || back>.06f ||
                       !std::isfinite(arm) || arm<0.f || arm>1.f)
                        throw std::invalid_argument("Invalid viewmodel recoil");
                    payload << ' ' << back << ' ' << arm;
                }
                service.model_visual_modes()[actor.node.id]=payload.str();
                return std::string("null");
            });
        bindings.add("attachBone",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if((args.size()!=9 && args.size()!=10) || !service.physics())
                throw std::invalid_argument("scene.attachBone(child,parent,bone,offset,rotation[,alignment])");
            auto const childIndex=runtime_script_contracts::script_index(args[0]);
            auto const parentIndex=runtime_script_contracts::script_index(args[1]);
            auto const& objects=service.physics()->scene().objects;
            if(childIndex>=objects.size() || parentIndex>=objects.size() ||
               childIndex==parentIndex ||
               !service.physics()->runtime_alive(childIndex) ||
               (parentIndex>=service.physics()->authored_object_count() &&
                !service.physics()->runtime_alive(parentIndex)))
                throw std::invalid_argument("Attachment entities must be live");
            auto const& child=objects[childIndex];
            auto const& parent=objects[parentIndex];
            if(child.mass!=0.f || !child.node.id || !parent.node.id ||
               !service.spawn_owners().count(child.node.id) ||
               service.spawn_owners().at(child.node.id)!=service.owner)
                throw std::invalid_argument("Only an owned static visual can attach to a bone");
            auto const& bone=args[2];
            if(bone.empty() || bone.size()>96 ||
               !std::all_of(bone.begin(),bone.end(),[](unsigned char c) {
                   return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
                          (c>='0' && c<='9') || c=='_';
               }))throw std::invalid_argument("Unsafe skeleton joint name");
            std::string payload=std::to_string(parent.node.id)+" "+bone;
            for(std::size_t i=3;i<9;++i) {
                auto const value=runtime_script_contracts::script_number(args[i]);
                if(!std::isfinite(value) || std::abs(value)>360.)
                    throw std::invalid_argument("Unsafe socket transform");
                payload+=" "+std::to_string(value);
            }
            if(args.size()==10) {
                if(args[9]!="socket" && args[9]!="camera_forward")
                    throw std::invalid_argument("Unknown bone alignment mode");
                payload+=" "+args[9];
            }
            service.model_bone_attachments()[child.node.id]=std::move(payload);
            return std::string("null");
        });
        bindings.add("attachmentPoint",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=4 || !service.physics() || !service.graphics())
                throw std::invalid_argument(
                    "scene.attachmentPoint(childIndex,x,y,z)");
            const auto index=runtime_script_contracts::script_index(args[0]);
            if(index>=service.physics()->scene().objects.size())return std::string("null");
            auto const& child=service.physics()->scene().objects[index];
            if(!child.node.id)return std::string("null");
            const bool attached=service.model_bone_attachments().count(
                child.node.id)!=0;
            const Vector3 local{
                static_cast<float>(runtime_script_contracts::script_number(args[1])),
                static_cast<float>(runtime_script_contracts::script_number(args[2])),
                static_cast<float>(runtime_script_contracts::script_number(args[3]))};
            if(!local.valid() || local.magnitude()>5.f)
                throw std::invalid_argument("Unsafe weapon local muzzle point");
            // Attached weapons use the EXACT last-published skinned pose.
            // Free weapons use their scene/physics world transform. Never
            // substitute camera/hand offsets when the attachment is missing.
            std::optional<Vector3> point;
            if(attached) {
                point=service.graphics()->attached_local_point(
                    child.node.id,local,service.camera() ? &*service.camera_pose : nullptr);
            } else {
                const auto scaled=Vector3{
                    local.x*child.node.scale.x*child.size,
                    local.y*child.node.scale.y*child.size,
                    local.z*child.node.scale.z*child.size};
                if(child.mass>0.f) {
                    const auto orientation=service.physics()->orientation(index);
                    point=child.position+
                        orientation.transform_direction(scaled);
                } else {
                    constexpr float radians=.01745329251994329577f;
                    auto const& angles=child.node.rotation;
                    const auto rotation=
                        math::create_rotation_x(angles.x*radians)*
                        math::create_rotation_y(angles.y*radians)*
                        math::create_rotation_z(angles.z*radians);
                    point=child.position+rotation.transform_direction(scaled);
                }
            }
            if(!point || !point->valid())return std::string("null");
            return std::string("{\"x\":")+std::to_string(point->x)+
                ",\"y\":"+std::to_string(point->y)+
                ",\"z\":"+std::to_string(point->z)+"}";
        });
        bindings.add("detachBone",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !service.physics())
                throw std::invalid_argument("scene.detachBone(childIndex)");
            auto const index=runtime_script_contracts::script_index(args[0]);
            if(index>=service.physics()->scene().objects.size() ||
                !service.physics()->runtime_alive(index))
                throw std::invalid_argument("Invalid detached object");
            auto id=service.physics()->scene().objects[index].node.id;
            if(!service.spawn_owners().count(id) ||
                service.spawn_owners().at(id)!=service.owner)
                throw std::invalid_argument("Attachment owner mismatch");
            service.model_bone_attachments().erase(id);
            return std::string("null");
        });
}

void register_scene_script_bindings(ScriptRuntime& runtime,
                                   SceneScriptBindings&& handlers) {
    std::move(handlers).bind_all(runtime);
}
} // namespace northstarcpp::window_app_internal
