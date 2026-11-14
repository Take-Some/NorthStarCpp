// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/ecs_api.h>
#include "module_provider.hpp"
#include <flecs.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
thread_local std::string error_message;

struct Name { char text[NORTHSTARCPP_ECS_NAME_BYTES]; };
struct Position { float x,y,z; };
struct Visual { float size; uint32_t color; };
struct Body { float mass; };
struct Controller { uint8_t enabled; };
struct Motion { float x,z; };

struct Type { ecs_id_t id; size_t size; size_t alignment; };

struct World {
    flecs::world flecs_world;
    std::vector<NsEntity> ordered;
    std::unordered_map<std::string, Type> types;

    template<class T>
    void builtin(char const* key) {
        flecs_world.component<T>();
        types.emplace(key, Type{flecs_world.id<T>(), sizeof(T), alignof(T)});
    }

    World() {
        builtin<Name>(NS_COMPONENT_NAME);
        builtin<Position>(NS_COMPONENT_POSITION);
        builtin<Visual>(NS_COMPONENT_VISUAL);
        builtin<Body>(NS_COMPONENT_BODY);
        builtin<Controller>(NS_COMPONENT_CONTROLLER);
        builtin<Motion>(NS_COMPONENT_MOTION);
        flecs_world.system<Position, const Controller, const Motion>("NorthStarMovement")
            .kind(flecs::OnUpdate)
            .each([](Position& pos, Controller const& ctrl, Motion const& motion) {
                if (!ctrl.enabled)
                    return;
                pos.x = std::clamp(pos.x + motion.x, -1000.f, 1000.f);
                pos.z = std::clamp(pos.z + motion.z, -1000.f, 1000.f);
            });
    }

    ecs_world_t* raw() { return flecs_world.c_ptr(); }

    Type const& require_type(char const* key) const {
        if (!key || !*key)
            throw std::runtime_error("Component key is empty");
        auto it = types.find(key);
        if (it == types.end())
            throw std::runtime_error(std::string("Component not registered: ") + key);
        return it->second;
    }

    void require_alive(NsEntity e) {
        if (!e || !ecs_is_alive(raw(), e) ||
            std::find(ordered.begin(), ordered.end(), e) == ordered.end())
            throw std::runtime_error("Unknown or stale entity handle");
    }
};

World& require_world(void* p) {
    if (!p)
        throw std::runtime_error("Null ECS world");
    return *static_cast<World*>(p);
}

template<class Fn>
int guarded(Fn&& fn) {
    try {
        error_message.clear();
        fn();
        return 1;
    } catch (std::exception const& error) {
        error_message = error.what();
    } catch (...) {
        error_message = "Unexpected ECS provider failure";
    }
    return 0;
}
const char* provider() { return "Flecs 4.2.0"; }
const char* last_error() { return error_message.c_str(); }

void* create_world() {
    try {
        error_message.clear();
        return new World();
    } catch (std::exception const& e) {
        error_message = e.what();
    } catch (...) {
        error_message = "Could not create ECS world";
    }
    return nullptr;
}
void destroy_world(void* p) { delete static_cast<World*>(p); }

int register_component(void* ctx, const char* key, size_t size, size_t alignment) {
    return guarded([&] {
        auto& w = require_world(ctx);
        if (!key || !*key || std::strlen(key) > 127 || size == 0 || size > 65536 ||
            alignment == 0 || alignment > 64 || (alignment & (alignment-1)) != 0)
            throw std::runtime_error("Invalid POD component schema");
        auto found = w.types.find(key);
        if (found != w.types.end()) {
            if (found->second.size != size || found->second.alignment != alignment)
                throw std::runtime_error("Incompatible component schema");
            return;
        }
        ecs_component_desc_t desc{};
        desc.entity = ecs_new(w.raw());
        desc.type.size = static_cast<ecs_size_t>(size);
        desc.type.alignment = static_cast<ecs_size_t>(alignment);
        auto id = ecs_component_init(w.raw(), &desc);
        if (!id)
            throw std::runtime_error("Flecs component registration failed");
        w.types.emplace(key, Type{id,size,alignment});
    });
}
NsEntity create_entity(void* ctx) {
    NsEntity result = 0;
    if (!guarded([&] {
        auto& w = require_world(ctx);
        if (w.ordered.size() >= 16384)
            throw std::runtime_error("World has reached the scene V3 entity limit");
        result = ecs_new(w.raw());
        if (!result)
            throw std::runtime_error("Flecs could not create an entity");
        w.ordered.push_back(result);
    })) return 0;
    return result;
}
int destroy_entity(void* ctx, NsEntity entity) {
    return guarded([&] {
        auto& w = require_world(ctx);
        w.require_alive(entity);
        ecs_delete(w.raw(), entity);
        auto it = std::find(w.ordered.begin(), w.ordered.end(), entity);
        w.ordered.erase(it);
    });
}
int is_alive(void* ctx, NsEntity entity) {
    if (!ctx || !entity) return 0;
    auto& w = *static_cast<World*>(ctx);
    return ecs_is_alive(w.raw(),entity) &&
        std::find(w.ordered.begin(),w.ordered.end(),entity) != w.ordered.end();
}
size_t count(void* ctx) {
    return ctx ? static_cast<World*>(ctx)->ordered.size() : SIZE_MAX;
}
NsEntity at(void* ctx, size_t index) {
    if (!ctx) return 0;
    auto const& items = static_cast<World*>(ctx)->ordered;
    return index < items.size() ? items[index] : 0;
}
int set_component(void* ctx, NsEntity entity, const char* key, const void* bytes, size_t size) {
    return guarded([&] {
        auto& w = require_world(ctx);
        w.require_alive(entity);
        auto const& type = w.require_type(key);
        if (!bytes || size != type.size)
            throw std::runtime_error("Incorrect component payload size");
        ecs_set_id(w.raw(), entity, type.id, size, bytes);
    });
}
int get_component(void* ctx, NsEntity entity, const char* key, void* out, size_t capacity) {
    return guarded([&] {
        auto& w = require_world(ctx);
        w.require_alive(entity);
        auto const& type = w.require_type(key);
        if (!out || capacity != type.size)
            throw std::runtime_error("Incorrect output buffer size");
        auto const* data = ecs_get_id(w.raw(),entity,type.id);
        if (!data)
            throw std::runtime_error(std::string("Entity missing component: ") + key);
        std::memcpy(out,data,type.size);
    });
}
int has_component(void* ctx, NsEntity entity, const char* key) {
    if (!ctx || !entity || !key) return 0;
    auto& w = *static_cast<World*>(ctx);
    auto it = w.types.find(key);
    if (it == w.types.end() || !is_alive(ctx,entity)) return 0;
    return ecs_has_id(w.raw(),entity,it->second.id) ? 1 : 0;
}
int remove_component(void* ctx, NsEntity entity, const char* key) {
    return guarded([&] {
        auto& w = require_world(ctx);
        w.require_alive(entity);
        auto const& type = w.require_type(key);
        ecs_remove_id(w.raw(),entity,type.id);
    });
}
size_t query(void* ctx, const char* const* required, size_t n, NsEntity* output, size_t capacity) {
    size_t result = SIZE_MAX;
    if (!guarded([&] {
        auto& w = require_world(ctx);
        if (n > 64 || (n && !required))
            throw std::runtime_error("Invalid ECS query");
        std::vector<ecs_id_t> ids;
        ids.reserve(n);
        for (size_t i=0; i<n; ++i)
            ids.push_back(w.require_type(required[i]).id);
        result = 0;
        for (auto entity : w.ordered) {
            bool matches = true;
            for (auto id : ids) {
                if (!ecs_has_id(w.raw(),entity,id)) { matches = false; break; }
            }
            if (!matches) continue;
            if (output && result < capacity)
                output[result] = entity;
            ++result;
        }
    })) return SIZE_MAX;
    return result;
}
int progress(void* ctx, float seconds, float input_x, float input_z) {
    return guarded([&] {
        auto& w = require_world(ctx);
        if (!std::isfinite(seconds) || !std::isfinite(input_x) || !std::isfinite(input_z))
            throw std::runtime_error("Invalid ECS timestep");
        if (seconds <= 0.f) return;
        auto dx = std::clamp(input_x,-1.f,1.f);
        auto dz = std::clamp(input_z,-1.f,1.f);
        auto distance = 4.f * std::min(seconds,.1f) / std::max(1.f,std::hypot(dx,dz));
        auto const controller_id = w.require_type(NS_COMPONENT_CONTROLLER).id;
        auto const motion_id = w.require_type(NS_COMPONENT_MOTION).id;
        for (auto entity : w.ordered) {
            auto const* ctrl = static_cast<Controller const*>(ecs_get_id(w.raw(),entity,controller_id));
            if (ctrl && ctrl->enabled) {
                Motion motion{dx*distance,dz*distance};
                ecs_set_id(w.raw(),entity,motion_id,sizeof(motion),&motion);
            }
        }
        w.flecs_world.progress(seconds);
    });
}

NsEcsApi const api = {
    NORTHSTARCPP_ECS_API_VERSION, sizeof(NsEcsApi),
    provider, last_error, create_world, destroy_world,
    register_component, create_entity, destroy_entity, is_alive,
    count, at, set_component, get_component, has_component, remove_component,
    query, progress
};
} // namespace

#ifdef NORTHSTARCPP_ECS_BUILD
extern "C" NORTHSTARCPP_ECS_API_EXPORT
const NsEcsApi* northstar_ecs_get_api(uint32_t version) {
    return version == NORTHSTARCPP_ECS_API_VERSION ? &api : nullptr;
}
#endif

extern "C" NORTHSTARCPP_MODULE_EXPORT
const NorthStarModule* northstar_module_query(uint32_t host, const char* capability, uint32_t version)
{
    static NorthStarModule const module{
        sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI, NORTHSTARCPP_CAPABILITY_ECS,
        "Flecs", "4.2.0", NORTHSTARCPP_ECS_API_VERSION, sizeof(NsEcsApi), &api, last_error
    };
    return northstarcpp::module_provider::match(module, host, capability, version);
}
