#include <northstarcpp/scene_component_registry.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
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
// DLL registration is synchronous on the loading thread; the mutex also
// serializes acquire/release for providers shared across multiple services.
thread_local std::vector<std::string>* pending_dll_types=nullptr;
struct DllSchemas {std::size_t references=0;std::vector<std::string> types;};
std::mutex dll_schemas_mutex;
std::unordered_map<void*,DllSchemas> dll_schemas;
std::unordered_map<std::string,std::unique_ptr<CDescriptor>> c_registrations;
bool c_validator(void* ctx,std::uint32_t version,
                 const void* payload,std::size_t bytes) {
    auto* data=static_cast<CDescriptor*>(ctx);
    return data && data->callback &&
           data->callback(data->userdata,version,payload,bytes)!=0;
}
std::uint32_t c_register_reflected(const char* name,
                         std::uint32_t min_version,std::uint32_t max_version,
                         NorthStarSceneComponentValidateC callback,
                         void* context,
                         NorthStarSceneComponentFieldC const* fields,
                         std::uint32_t field_count) {
    if(!name || !callback || field_count>64u ||
       (field_count && !fields))return 0;
    try {
        std::string type(name);
        if(!valid_name(type))return 0;
        std::vector<SceneComponentField> copied;
        copied.reserve(field_count);
        for(std::uint32_t i=0;i<field_count;++i) {
            auto const& field=fields[i];
            if(!field.label)return 0;
            std::size_t len=0;
            while(len<=80 && field.label[len])++len;
            if(!len || len>80)return 0;
            if(field.kind!=NORTHSTARCPP_SCENE_COMPONENT_FIELD_U32 &&
               field.kind!=NORTHSTARCPP_SCENE_COMPONENT_FIELD_F32)return 0;
            copied.push_back({std::string(field.label,len),
                static_cast<SceneComponentFieldKind>(field.kind),field.offset});
        }
        auto state=std::make_unique<CDescriptor>();
        state->callback=callback;
        state->userdata=context;
        auto* raw=state.get();
        std::lock_guard lock(c_mutex);
        if(c_registrations.count(type))return 0;
        if(!register_scene_component_type({type,min_version,max_version,
                                            c_validator,raw,std::move(copied)}))
            return 0;
        try {
            c_registrations.emplace(type,std::move(state));
            if(pending_dll_types)pending_dll_types->push_back(type);
        }catch(...) {
            unregister_scene_component_type(type);
            c_registrations.erase(type);
            return 0;
        }
        return 1;
    } catch(...) {return 0;}
}
std::uint32_t c_register(const char* name,std::uint32_t min_version,
                         std::uint32_t max_version,
                         NorthStarSceneComponentValidateC callback,
                         void* context) {
    return c_register_reflected(name,min_version,max_version,callback,context,
                                nullptr,0);
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
std::optional<SceneComponentFieldValue> read_scene_component_field(
    SceneComponentBlob const& component,SceneComponentField const& field) {
    if(field.offset>component.payload.size() ||
       component.payload.size()-field.offset<4)return std::nullopt;
    std::uint32_t bits=0;
    for(unsigned byte=0;byte<4;++byte)
        bits|=std::uint32_t(static_cast<unsigned char>(
            component.payload[field.offset+byte]))<<(8*byte);
    if(field.kind==SceneComponentFieldKind::UInt32)
        return SceneComponentFieldValue{bits};
    if(field.kind==SceneComponentFieldKind::Float32) {
        float value=0;
        std::memcpy(&value,&bits,sizeof(value));
        if(std::isfinite(value))return SceneComponentFieldValue{value};
    }
    return std::nullopt;
}
bool write_scene_component_field(
    SceneComponentBlob& component,SceneComponentField const& field,
    SceneComponentFieldValue value) {
    if(field.offset>component.payload.size() ||
       component.payload.size()-field.offset<4)return false;
    std::uint32_t bits=0;
    if(field.kind==SceneComponentFieldKind::UInt32 &&
       std::holds_alternative<std::uint32_t>(value))
        bits=std::get<std::uint32_t>(value);
    else if(field.kind==SceneComponentFieldKind::Float32 &&
            std::holds_alternative<float>(value)) {
        auto const scalar=std::get<float>(value);
        if(!std::isfinite(scalar))return false;
        std::memcpy(&bits,&scalar,sizeof(bits));
    } else return false;
    for(unsigned byte=0;byte<4;++byte)
        component.payload[field.offset+byte]=static_cast<char>(
            (bits>>(8*byte))&0xffu);
    return true;
}
bool register_scene_component_type(SceneComponentType value) {
    if(!valid_name(value.type) || value.min_version==0 ||
       value.max_version<value.min_version || !value.validate)return false;
    if(value.editor_fields.size()>64)return false;
    std::unordered_set<std::uint32_t> offsets;
    std::unordered_set<std::string> names;
    for(auto const& field:value.editor_fields) {
        if(field.label.empty() || field.label.size()>80 ||
           field.offset>1048572u || field.offset%4u!=0 ||
           (field.kind!=SceneComponentFieldKind::UInt32 &&
            field.kind!=SceneComponentFieldKind::Float32) ||
           !offsets.insert(field.offset).second ||
           !names.insert(field.label).second)return false;
    }
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
std::optional<SceneComponentSchemaInfo> scene_component_schema(
    std::string const& type) {
    std::lock_guard lock(registry_mutex);
    auto it=registry.find(type);
    if(it==registry.end())return std::nullopt;
    return SceneComponentSchemaInfo{
        it->second.type,it->second.min_version,it->second.max_version,
        it->second.editor_fields};
}
std::vector<SceneComponentSchemaInfo> list_scene_component_schemas() {
    std::lock_guard lock(registry_mutex);
    std::vector<SceneComponentSchemaInfo> entries;
    entries.reserve(registry.size());
    for(auto const& item:registry)
        entries.push_back({item.second.type,item.second.min_version,
                          item.second.max_version,item.second.editor_fields});
    std::sort(entries.begin(),entries.end(),
              [](auto const& a,auto const& b){return a.type<b.type;});
    return entries;
}
bool acquire_scene_components_for_module(
    void* library,NorthStarSceneComponentsRegisterC callback) noexcept {
    if(!library || !callback)return false;
    try {
        std::lock_guard lock(dll_schemas_mutex);
        if(auto it=dll_schemas.find(library);it!=dll_schemas.end()) {
            ++it->second.references;
            return true;
        }
        std::vector<std::string> registered;
        auto* const previous=pending_dll_types;
        pending_dll_types=&registered;
        bool success=false;
        try {
            success=callback(scene_component_host_api_v1())==1u;
        }catch(...) {
            success=false;
        }
        pending_dll_types=previous;
        if(success) {
            try {
                dll_schemas.emplace(library,DllSchemas{1,registered});
                return true;
            }catch(...) {success=false;}
        }
        // On callback failure, undo every type registered in this attempt
        // before the owning DLL can be unloaded. Never run its validator
        // after releasing the OS library reference.
        for(auto it=registered.rbegin();it!=registered.rend();++it)
            c_unregister(it->c_str());
    }catch(...) {return false;}
    return false;
}
void release_scene_components_for_module(void* library) noexcept {
    if(!library)return;
    try {
        std::lock_guard lock(dll_schemas_mutex);
        auto it=dll_schemas.find(library);
        if(it==dll_schemas.end())return;
        if(--it->second.references)return;
        auto const owned=std::move(it->second.types);
        dll_schemas.erase(it);
        for(auto entry=owned.rbegin();entry!=owned.rend();++entry)
            c_unregister(entry->c_str());
    }catch(...) {}
}
NorthStarSceneComponentHostApi const* scene_component_host_api_v1() noexcept {
    static NorthStarSceneComponentHostApi const table{
        sizeof(NorthStarSceneComponentHostApi),
        NORTHSTARCPP_SCENE_COMPONENT_HOST_ABI,
        c_register,c_unregister,c_register_reflected
    };
    return &table;
}
} // namespace northstarcpp
