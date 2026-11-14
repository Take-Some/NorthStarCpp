#include <northstarcpp/scene_component_registry.hpp>
#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
namespace northstarcpp {
namespace {
bool valid_name(std::string const& name);
std::mutex registry_mutex;
std::unordered_map<std::string,SceneComponentType> registry;
struct CDescriptor {
    NorthStarSceneComponentValidateC callback=nullptr;
    void* userdata=nullptr;
};
std::mutex c_mutex;
std::unordered_map<std::string,std::unique_ptr<CDescriptor>> c_registrations;
bool c_validator(void* ctx,std::uint32_t version,
                 const void* payload,std::size_t bytes) {
    auto* data=static_cast<CDescriptor*>(ctx);
    return data && data->callback &&
           data->callback(data->userdata,version,payload,bytes)!=0;
}
std::uint32_t c_register(const char* name,std::uint32_t min_version,
                         std::uint32_t max_version,
                         NorthStarSceneComponentValidateC callback,
                         void* context) {
    if(!name || !callback)return 0;
    try {
    std::string type(name);
    if(!valid_name(type))return 0;
    auto state=std::make_unique<CDescriptor>();
    state->callback=callback;state->userdata=context;
    auto* raw=state.get();
    std::lock_guard lock(c_mutex);
    if(c_registrations.count(type))return 0;
    if(!register_scene_component_type({type,min_version,max_version,
                                       c_validator,raw}))return 0;
    c_registrations.emplace(std::move(type),std::move(state));
    return 1;
    } catch(...) {return 0;} // Exceptions must never cross the plugin C ABI.
}
void c_unregister(const char* name) {
    if(!name)return;
    try {
        std::lock_guard lock(c_mutex);
        unregister_scene_component_type(name);
        c_registrations.erase(name);
    } catch(...) {} // C ABI is noexcept by contract.
}

bool valid_name(std::string const& name) {
    if(name.size()<3 || name.size()>128 || name.front()=='.' || name.back()=='.')
        return false;
    return name.find('.')!=std::string::npos &&
        std::all_of(name.begin(),name.end(),[](unsigned char c){
            return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
                   (c>='0' && c<='9') || c=='.' || c=='_' || c=='-';
        });
}
}
bool register_scene_component_type(SceneComponentType value) {
    if(!valid_name(value.type) || value.min_version==0 ||
       value.max_version<value.min_version || !value.validate)return false;
    std::lock_guard lock(registry_mutex);
    return registry.emplace(value.type,std::move(value)).second;
}
void unregister_scene_component_type(std::string const& type) {
    std::lock_guard lock(registry_mutex);
    registry.erase(type);
}
bool validate_scene_component_blob(SceneComponentBlob const& component) {
    if(!valid_name(component.type) || component.version==0 ||
       component.payload.size()>1048576)return false;
    // An absent provider does not imply invalid data: preserve opaque bytes.
    std::lock_guard lock(registry_mutex);
    auto it=registry.find(component.type);
    if(it==registry.end())return true;
    auto const& desc=it->second;
    if(component.version<desc.min_version || component.version>desc.max_version)
        return true; // forward/backward version must survive round-trip
    try {
        return desc.validate(desc.context,component.version,
            component.payload.data(),component.payload.size());
    } catch(...) {return false;}
}
NorthStarSceneComponentHostApi const* scene_component_host_api_v1() noexcept {
    static NorthStarSceneComponentHostApi const table{
        sizeof(NorthStarSceneComponentHostApi),
        NORTHSTARCPP_SCENE_COMPONENT_HOST_ABI,
        c_register,c_unregister
    };
    return &table;
}
} // namespace northstarcpp
