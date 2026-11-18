#include "window_script_domain_contracts.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <northstarcpp/scene_pawn.hpp>
#include <cmath>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>

#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <stdexcept>
#include <string>
namespace northstarcpp::window_app_internal {
void collect_ecs_entities_script_binding(EcsScriptBindings& bindings, EcsEntitiesStateView view) {
    if(!view.current_scene || !view.index_is_live)
        throw std::invalid_argument("Invalid ecs ScriptRuntime view");
        bindings.add("entities",[view](std::string_view json) {
            if (!runtime_script_contracts::script_args(json).empty()) throw std::invalid_argument("entities() expects no arguments");
            std::string result="[";
            bool first=true;
            if (auto const* scene=view.current_scene())
                for (std::size_t index=0; index<scene->objects.size(); ++index) {
                if (!view.index_is_live(index)) continue;
                auto const& object=scene->objects[index];
                if (!object.node.id) continue;
                if (!first) result+=',';
                first=false;
                result+=runtime_project_files::script_json_quote(std::to_string(object.node.id));
            }
            return result+"]";
        });
        // Native FLECS set-intersection in one script/native boundary crossing.
        // Accept only known engine-owned component names and bounded arity.
}

void collect_ecs_runtime_script_bindings(
    EcsScriptBindings& bindings, EcsDomainCapabilities service) {
    if(!service.physics_slot || !service.ecs_slot || !service.indices ||
       !service.handles || !service.scene_generation)
        throw std::invalid_argument("Invalid ECS capabilities");
bindings.add("queryMany",[service](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.empty() || args.size()>32 || !service.ecs())
                throw std::invalid_argument("engine.ecs.queryMany(componentNames...)");
            static const std::set<std::string> allowed={
                NS_COMPONENT_NAME,NS_COMPONENT_POSITION,NS_COMPONENT_VISUAL,
                NS_COMPONENT_BODY,NS_COMPONENT_CONTROLLER,NS_COMPONENT_MOTION,
                NS_COMPONENT_SCRIPT};
            for(auto const& name:args)
                if(!allowed.count(name))
                    throw std::invalid_argument("Unknown ECS component: "+name);
            std::string result="[";
            bool first=true;
            for(auto handle:service.ecs()->query(args)) {
                auto it=service.handle_ids().find(handle);
                if(it==service.handle_ids().end() || !it->second ||
                   !service.live_handle(handle,it->second)) continue;
                if(!first) result+=',';
                first=false;
                result+=runtime_project_files::script_json_quote(std::to_string(it->second));
            }
            return result+"]";
        });
        bindings.add("query",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=1 || !service.ecs())
                throw std::invalid_argument("engine.ecs.query(componentName)");
            static const std::set<std::string> allowed={
                NS_COMPONENT_NAME,NS_COMPONENT_POSITION,NS_COMPONENT_VISUAL,
                NS_COMPONENT_BODY,NS_COMPONENT_CONTROLLER,NS_COMPONENT_MOTION,
                NS_COMPONENT_SCRIPT};
            if (!allowed.count(args[0])) throw std::invalid_argument("Unknown ECS component");
            std::string result="[";
            bool first=true;
            for (auto handle:service.ecs()->query({args[0]})) {
                auto it=service.handle_ids().find(handle);
                if (it==service.handle_ids().end() || !it->second ||
                    !service.live_handle(handle,it->second)) continue;
                if (!first) result+=',';
                first=false;
                result+=runtime_project_files::script_json_quote(std::to_string(it->second));
            }
            return result+"]";
        });
        bindings.add("has",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=2 || !service.ecs())
                throw std::invalid_argument("engine.ecs.has(id,componentName)");
            static const std::set<std::string> allowed={
                NS_COMPONENT_NAME,NS_COMPONENT_POSITION,NS_COMPONENT_VISUAL,
                NS_COMPONENT_BODY,NS_COMPONENT_CONTROLLER,NS_COMPONENT_MOTION,
                NS_COMPONENT_SCRIPT};
            if (!allowed.count(args[1])) throw std::invalid_argument("Unknown ECS component");
            auto const index=service.find_live_index(runtime_script_contracts::script_index(args[0]));
            if (!index) return std::string("false");
            return service.ecs()->has_component(service.ecs()->at(*index),args[1])
                ? std::string("true") : std::string("false");
        });
        bindings.add("get",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=2 || !service.physics() || !service.ecs())
                throw std::invalid_argument("engine.ecs.get(id,componentName)");
            auto const index=service.require_live_index(runtime_script_contracts::script_index(args[0]));
            auto const& obj=service.physics()->scene().objects.at(index);
            if (args[1]==NS_COMPONENT_NAME)
                return runtime_project_files::script_json_quote(obj.name);
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
                return service.physics()->controlled_entity()==obj.node.id ? std::string("true") : std::string("false");
            if (args[1]==NS_COMPONENT_SCRIPT)
                return obj.node.script.empty() ? std::string("null") :
                    runtime_project_files::script_json_quote(obj.node.script);
            if (args[1]==NS_COMPONENT_MOTION) {
                NsMotion motion{};
                service.ecs()->get_component(service.ecs()->at(index),
                    NS_COMPONENT_MOTION,&motion,sizeof(motion));
                return std::string("{\"x\":")+std::to_string(motion.x)+
                    ",\"z\":"+std::to_string(motion.z)+"}";
            }
            throw std::invalid_argument("Unsupported ECS component value type");
        });
        bindings.add("getScript",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=1 || !service.physics())
                throw std::invalid_argument("engine.ecs.getScript(id)");
            auto const index=service.require_live_index(runtime_script_contracts::script_index(args[0]));
            return runtime_project_files::script_json_quote(service.physics()->scene().objects.at(index).node.script);
        });
        bindings.add("getPosition",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=1 || !service.physics())
                throw std::invalid_argument("engine.ecs.getPosition(id)");
            auto const index=service.require_live_index(runtime_script_contracts::script_index(args[0]));
            auto const position=service.physics()->scene().objects.at(index).position;
            return std::string("{\"x\":")+std::to_string(position.x)+
                ",\"y\":"+std::to_string(position.y)+
                ",\"z\":"+std::to_string(position.z)+"}";
        });
        bindings.add("setPosition",[service](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=4 || !service.physics() || !service.ecs())
                throw std::invalid_argument("engine.ecs.setPosition(id,x,y,z)");
            auto const index=service.require_live_index(runtime_script_contracts::script_index(args[0]));
            auto const idx=index;
            auto const& body=service.physics()->scene().objects.at(idx);
            if (body.mass!=0.f || scene_pawn::is_pawn(service.physics()->scene(),body))
                throw std::invalid_argument("Only static ECS entities may be repositioned");
            auto finite_float=[](std::string const& str) {
                auto number=runtime_script_contracts::script_number(str);
                if (std::abs(number)>1000000.0)
                    throw std::invalid_argument("Position exceeds world bounds");
                return static_cast<float>(number);
            };
            Vector3 position{finite_float(args[1]),finite_float(args[2]),finite_float(args[3])};
            service.physics()->move_static(idx,position);
            NsPosition next{position.x,position.y,position.z};
            service.ecs()->set_component(service.ecs()->at(idx),NS_COMPONENT_POSITION,
                                      &next,sizeof(next));
            return std::string("null");
        });
}

void register_ecs_script_bindings(ScriptRuntime& runtime,
                                   EcsScriptBindings&& handlers) {
    std::move(handlers).bind_all(runtime);
}
} // namespace northstarcpp::window_app_internal
