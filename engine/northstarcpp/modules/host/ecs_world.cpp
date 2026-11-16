// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/ecs.hpp>
#include <northstarcpp/scene_world.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include "module_loader.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>

namespace northstarcpp {
namespace {
void require(bool ok, char const* action, NsEcsApi const* api)
{
    if (ok) return;
    std::string message = action;
    auto error = api->last_error();
    if (error && *error) { message += ": "; message += error; }
    throw std::runtime_error(message);
}

void register_builtins(NsEcsApi const* api, void* world)
{
    require(api->register_component(world,NS_COMPONENT_NAME,sizeof(NsName),alignof(NsName)),
        "Register Name",api);
    require(api->register_component(world,NS_COMPONENT_POSITION,sizeof(NsPosition),alignof(NsPosition)),
        "Register Position",api);
    require(api->register_component(world,NS_COMPONENT_VISUAL,sizeof(NsVisual),alignof(NsVisual)),
        "Register Visual",api);
    require(api->register_component(world,NS_COMPONENT_BODY,sizeof(NsBody),alignof(NsBody)),
        "Register PhysicsBody",api);
    require(api->register_component(world,NS_COMPONENT_CONTROLLER,sizeof(NsController),alignof(NsController)),
        "Register Controller",api);
    require(api->register_component(world,NS_COMPONENT_MOTION,sizeof(NsMotion),alignof(NsMotion)),
        "Register Motion",api);
    require(api->register_component(world,NS_COMPONENT_SCRIPT,
            sizeof(NsScriptComponent),alignof(NsScriptComponent)),
        "Register Script",api);
}

void apply_record(NsEcsApi const* api, void* world, NsEntity id, SceneObject const& object,bool controlled)
{
    NsName name{};
    std::memcpy(name.text,object.name.c_str(),object.name.size());
    NsPosition position{object.position.x,object.position.y,object.position.z};
    NsVisual visual{object.size,object.color};
    NsBody body{object.mass};
    NsController controller{static_cast<uint8_t>(controlled)};
    NsMotion motion{0.f,0.f};
    auto write = [&](char const* key, void const* data, size_t bytes) {
        require(api->set(world,id,key,data,bytes),"Set ECS component",api);
    };
    write(NS_COMPONENT_NAME,&name,sizeof(name));
    write(NS_COMPONENT_POSITION,&position,sizeof(position));
    write(NS_COMPONENT_VISUAL,&visual,sizeof(visual));
    write(NS_COMPONENT_BODY,&body,sizeof(body));
    write(NS_COMPONENT_CONTROLLER,&controller,sizeof(controller));
    if (!api->has(world,id,NS_COMPONENT_MOTION))
        write(NS_COMPONENT_MOTION,&motion,sizeof(motion));
    if (object.node.script.empty()) {
        if (api->has(world,id,NS_COMPONENT_SCRIPT))
            require(api->remove(world,id,NS_COMPONENT_SCRIPT),"Remove Script",api);
    } else {
        NsScriptComponent component{};
        if (object.node.script.size()>=sizeof(component.path))
            throw std::invalid_argument("Script virtual path exceeds ECS component capacity");
        std::memcpy(component.path,object.node.script.data(),object.node.script.size());
        write(NS_COMPONENT_SCRIPT,&component,sizeof(component));
    }
}

SceneObject read_record(NsEcsApi const* api, void* world, NsEntity id)
{
    NsName name{};
    NsPosition position{};
    NsVisual visual{};
    NsBody body{};
    NsController controller{};
    auto read = [&](char const* key, void* data, size_t bytes) {
        require(api->get(world,id,key,data,bytes),"Get ECS component",api);
    };
    read(NS_COMPONENT_NAME,&name,sizeof(name));
    read(NS_COMPONENT_POSITION,&position,sizeof(position));
    read(NS_COMPONENT_VISUAL,&visual,sizeof(visual));
    read(NS_COMPONENT_BODY,&body,sizeof(body));
    read(NS_COMPONENT_CONTROLLER,&controller,sizeof(controller));
    (void)controller; // runtime input is controlled by PlayerController
    if (!std::memchr(name.text,'\0',sizeof(name.text)))
        throw std::runtime_error("ECS Name is not terminated");
    SceneObject object;
    object.name = name.text;
    object.position = {position.x,position.y,position.z};
    object.size = visual.size;
    object.color = visual.color;
    object.mass = body.mass;
    return object;
}
} // namespace

struct EcsWorld::Impl {
    Module module{NORTHSTARCPP_CAPABILITY_ECS};
    NsEcsApi const* api = nullptr;
    void* world = nullptr;
    // Authoring metadata is independent of the concrete ECS provider.
    // Runtime built-ins remain authoritative for mutable physics/visual values.
    Scene scene_metadata;
    std::unordered_map<NsEntity, SceneNode> scene_nodes;
    // Only the physics-owned runtime uses sparse index-preserving streaming.
    // The authoring EcsWorld remains fully populated and index-stable.
    std::vector<NsEntity> authored_handles;
    std::vector<std::uint64_t> authored_ids;
    bool streaming=false;

    Impl() {
        api = module.query<NsEcsApi>(NORTHSTARCPP_ECS_API_VERSION, true);
        if (!api) {
            auto const get_api = module.symbol<decltype(&northstar_ecs_get_api)>("northstar_ecs_get_api");
            api = get_api ? get_api(NORTHSTARCPP_ECS_API_VERSION) : nullptr;
        }
        if (!api || !valid_api(*api))
            throw module.error("Incompatible ECS function table");
        world = api->create_world();
        require(world != nullptr,"Create ECS world",api);
        try {
            register_builtins(api,world);
        } catch (...) {
            api->destroy_world(world);
            world = nullptr;
            throw;
        }
    }
    ~Impl() {
        if (world) api->destroy_world(world);
    }
};

EcsWorld::EcsWorld():impl_(std::make_unique<Impl>()){}
EcsWorld::~EcsWorld()=default;
EcsWorld::EcsWorld(EcsWorld&&) noexcept=default;
EcsWorld& EcsWorld::operator=(EcsWorld&&) noexcept=default;

void EcsWorld::load(Scene const& scene)
{
    validate_scene(scene);
    auto const* api = impl_->api;
    auto replacement = api->create_world();
    require(replacement != nullptr,"Create replacement world",api);
    std::unordered_map<NsEntity, SceneNode> nodes;
    nodes.reserve(scene.objects.size());
    std::vector<NsEntity> handles;
    std::vector<std::uint64_t> stable_ids;
    handles.reserve(scene.objects.size());
    stable_ids.reserve(scene.objects.size());
    try {
        register_builtins(api,replacement);
        for (auto const& object : scene.objects) {
            auto id = api->create_entity(replacement);
            require(id != 0,"Create entity",api);
            apply_record(api,replacement,id,object,scene_pawn::is_local(scene,object));
            nodes.emplace(id, object.node);
            handles.push_back(id);
            stable_ids.push_back(object.node.id);
        }
    } catch (...) {
        api->destroy_world(replacement);
        throw;
    }
    Scene metadata = scene;
    metadata.objects.clear();
    api->destroy_world(impl_->world);
    impl_->world = replacement;
    impl_->scene_metadata = std::move(metadata);
    impl_->scene_nodes = std::move(nodes);
    impl_->authored_handles = std::move(handles);
    impl_->authored_ids = std::move(stable_ids);
    impl_->streaming = false;
}

NsEntity EcsWorld::create(SceneObject const& object)
{
    Scene updated = snapshot();
    SceneObject created = object;
    if (updated.version >= 3 && created.node.id == 0) {
        std::uint64_t maximum = 0;
        for (auto const& previous : updated.objects)
            maximum = std::max(maximum, previous.node.id);
        if (maximum == UINT64_MAX)
            throw std::runtime_error("Scene stable-ID space exhausted");
        created.node.id = maximum + 1;
    }
    updated.objects.push_back(created);
    validate_scene(updated);
    auto const* api=impl_->api;
    auto id=api->create_entity(impl_->world);
    require(id != 0,"Create entity",api);
    try {
        apply_record(api,impl_->world,id,created,scene_pawn::is_local(updated,created));
        impl_->scene_nodes.emplace(id, created.node);
    } catch (...) {
        api->destroy_entity(impl_->world,id);
        throw;
    }
    return id;
}

NsEntity EcsWorld::create_runtime(SceneObject const& object)
{
    if (!object.node.id ||
        object.name.empty() || object.name.size()>=NORTHSTARCPP_ECS_NAME_BYTES ||
        !std::isfinite(object.position.x) || !std::isfinite(object.position.y) ||
        !std::isfinite(object.position.z) || !std::isfinite(object.size) ||
        !std::isfinite(object.mass) || object.size<=0.f || object.mass<0.f)
        throw std::invalid_argument("Invalid transient ECS entity");
    auto const* api=impl_->api;
    auto id=api->create_entity(impl_->world);
    require(id!=0,"Create runtime ECS entity",api);
    try {
        apply_record(api,impl_->world,id,object,false);
        impl_->scene_nodes.emplace(id,object.node);
    } catch (...) {
        api->destroy_entity(impl_->world,id);
        throw;
    }
    return id;
}

void EcsWorld::update(NsEntity id, SceneObject const& object)
{
    auto current = snapshot();
    auto entities_list = entities();
    auto it = std::find(entities_list.begin(),entities_list.end(),id);
    if (it==entities_list.end())
        throw std::runtime_error("Cannot update stale ECS entity");
    auto changed = object;
    // Editing scalar values must not accidentally drop scene metadata.
    if (current.version >= 3 && changed.node.id == 0)
        changed.node = current.objects[static_cast<size_t>(it-entities_list.begin())].node;
    current.objects[static_cast<size_t>(it-entities_list.begin())] = changed;
    validate_scene(current);
    apply_record(impl_->api,impl_->world,id,changed,scene_pawn::is_local(current,changed));
    impl_->scene_nodes[id] = changed.node;
}

void EcsWorld::erase(NsEntity id)
{
    // Reject deleting a parent with descendants instead of creating dangling
    // stable IDs and silently corrupting an authored hierarchy.
    auto const found = impl_->scene_nodes.find(id);
    if (impl_->scene_metadata.version >= 3 && found != impl_->scene_nodes.end()) {
        for (auto const& [other, node] : impl_->scene_nodes)
            if (other != id && node.parent == found->second.id)
                throw std::runtime_error("Cannot delete an entity with scene children");
    }
    require(impl_->api->destroy_entity(impl_->world,id),"Delete ECS entity",impl_->api);
    impl_->scene_nodes.erase(id);
}

bool EcsWorld::alive(NsEntity id) const { return impl_->api->is_alive(impl_->world,id) != 0; }
std::size_t EcsWorld::size() const
{
    auto n=impl_->api->count(impl_->world);
    if (n==SIZE_MAX) throw std::runtime_error("Cannot count ECS entities");
    return n;
}
NsEntity EcsWorld::at(std::size_t index) const
{
    auto id=impl_->api->at(impl_->world,index);
    if (!id) throw std::out_of_range("ECS entity index is out of range");
    return id;
}
std::vector<NsEntity> EcsWorld::entities() const
{
    std::vector<NsEntity> ids;
    auto n=size();
    ids.reserve(n);
    for (size_t i=0;i<n;++i) ids.push_back(at(i));
    return ids;
}
SceneObject EcsWorld::object(NsEntity id) const
{
    if (!alive(id)) throw std::runtime_error("Stale ECS entity handle");
    auto object = read_record(impl_->api,impl_->world,id);
    if (auto node=impl_->scene_nodes.find(id); node!=impl_->scene_nodes.end())
        object.node=node->second;
    return object;
}
Scene EcsWorld::snapshot() const
{
    Scene result=impl_->scene_metadata;
    result.objects.reserve(size());
    for (auto id:entities()) result.objects.push_back(object(id));
    return result;
}
std::size_t EcsWorld::reconcile_scene_residency(
    Scene const& source, SceneWorld const& scene_world)
{
    auto& handles=impl_->authored_handles;
    auto const& ids=impl_->authored_ids;
    if(source.version!=3 || handles.size()!=source.objects.size() ||
       ids.size()!=source.objects.size() ||
       scene_world.source().objects.size()!=source.objects.size())
        throw std::runtime_error("ECS streaming authored index mismatch");
    // Keep all dynamic bodies active even if their authored cell unloads.
    // The runtime's ECS handles are not stable: authored IDs are stable.
    std::size_t changed=0;
    for(std::size_t i=0;i<source.objects.size();++i) {
        auto const& obj=source.objects[i];
        if(obj.node.id!=ids[i] ||
           scene_world.source().objects[i].node.id!=obj.node.id)
            throw std::runtime_error("ECS streaming stable scene ID mismatch");
        bool const wanted=scene_pawn::is_pawn(source,obj) || obj.mass>0.f || obj.node.cell.empty() ||
                          scene_world.object_resident(i);
        auto& handle=handles[i];
        if(wanted && !handle) {
            auto id=impl_->api->create_entity(impl_->world);
            require(id!=0,"Stream ECS entity",impl_->api);
            try {
                apply_record(impl_->api,impl_->world,id,obj,scene_pawn::is_local(source,obj));
                impl_->scene_nodes.emplace(id,obj.node);
            } catch(...) {
                impl_->api->destroy_entity(impl_->world,id);
                throw;
            }
            handle=id;
            ++changed;
        } else if(!wanted && handle) {
            require(impl_->api->destroy_entity(impl_->world,handle),
                    "Evict ECS scene entity",impl_->api);
            impl_->scene_nodes.erase(handle);
            handle=0;
            ++changed;
        }
    }
    impl_->streaming=true;
    return changed;
}

void EcsWorld::sync_positions(Scene const& scene)
{
    if(impl_->streaming) {
        if(scene.objects.size()!=impl_->authored_handles.size() ||
           scene.objects.size()!=impl_->authored_ids.size())
            throw std::runtime_error("Streamed physics/ECS index mismatch");
        for(std::size_t i=0;i<scene.objects.size();++i) {
            if(scene.objects[i].node.id!=impl_->authored_ids[i])
                throw std::runtime_error("Streamed ECS authored ID changed");
            auto const id=impl_->authored_handles[i];
            if(!id)continue;
            auto const& p=scene.objects[i].position;
            NsPosition position{p.x,p.y,p.z};
            set_component(id,NS_COMPONENT_POSITION,&position,sizeof(position));
        }
        return;
    }
    if (scene.objects.size()!=size())
        throw std::runtime_error("Physics/ECS entity count mismatch");
    for (size_t i=0;i<scene.objects.size();++i) {
        auto const& p=scene.objects[i].position;
        NsPosition position{p.x,p.y,p.z};
        set_component(at(i),NS_COMPONENT_POSITION,&position,sizeof(position));
    }
}
void EcsWorld::step(float seconds, float input_x, float input_z)
{
    if (!std::isfinite(seconds)||seconds<=0.f||
        !std::isfinite(input_x)||!std::isfinite(input_z))return;
    require(impl_->api->progress(impl_->world,seconds,input_x,input_z),
        "Run ECS systems",impl_->api);
}
std::string EcsWorld::provider_name() const { return impl_->api->provider(); }

void EcsWorld::register_component(std::string const& key, size_t size, size_t align)
{
    require(impl_->api->register_component(impl_->world,key.c_str(),size,align),
        "Register ECS component",impl_->api);
}
void EcsWorld::set_component(NsEntity id,std::string const& key,void const* data,size_t bytes)
{
    require(impl_->api->set(impl_->world,id,key.c_str(),data,bytes),
        "Set ECS component",impl_->api);
}
void EcsWorld::get_component(NsEntity id,std::string const& key,void* data,size_t bytes) const
{
    require(impl_->api->get(impl_->world,id,key.c_str(),data,bytes),
        "Get ECS component",impl_->api);
}
void EcsWorld::remove_component(NsEntity id,std::string const& key)
{
    require(impl_->api->remove(impl_->world,id,key.c_str()),
        "Remove ECS component",impl_->api);
}
bool EcsWorld::has_component(NsEntity id,std::string const& key) const
{
    return impl_->api->has(impl_->world,id,key.c_str()) != 0;
}
std::vector<NsEntity> EcsWorld::query(std::vector<std::string> const& required) const
{
    std::vector<char const*> keys;
    keys.reserve(required.size());
    for (auto const& key:required) keys.push_back(key.c_str());
    auto n=impl_->api->query(impl_->world,keys.data(),keys.size(),nullptr,0);
    if (n==SIZE_MAX) require(false,"Query ECS entities",impl_->api);
    std::vector<NsEntity> result(n);
    auto written=impl_->api->query(impl_->world,keys.data(),keys.size(),result.data(),result.size());
    if (written!=n) throw std::runtime_error("ECS query changed during enumeration");
    return result;
}
} // namespace northstarcpp
