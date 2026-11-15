// Enumerate available engine.* implementations for the Qt Project Browser.
// External modules are ABI/table-validated without calling create().
#include "provider_inventory.hpp"
#include <northstarcpp/provider_identity.hpp>
#include "../host/module_contracts.hpp"
#include <northstarcpp/module.h>
#include <northstarcpp/render_features_api.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_skinning_api.h>
#include <northstarcpp/render_surfaces_api.h>
#include <northstarcpp/render_character_api.h>
#include <northstarcpp/render_lights_api.h>
#include <northstarcpp/render_timing_api.h>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QString>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace northstarcpp::projects {
namespace {
namespace fs=std::filesystem;
struct Expected {
    char const* capability;
    std::uint32_t abi,table_bytes;
    bool (*valid)(void const*);
};
template<class Api>
bool valid_table(void const* raw) {
    return raw && northstarcpp::valid_api(*static_cast<Api const*>(raw));
}
static const std::array<Expected,12> expected{{
    {NORTHSTARCPP_CAPABILITY_ECS,NORTHSTARCPP_ECS_API_VERSION,
        sizeof(NsEcsApi),valid_table<NsEcsApi>},
    {NORTHSTARCPP_CAPABILITY_PHYSICS,NORTHSTARCPP_PHYSICS_ABI,
        sizeof(NorthStarPhysicsApi),valid_table<NorthStarPhysicsApi>},
    {NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_ABI,
        sizeof(NorthStarRenderApi),valid_table<NorthStarRenderApi>},
    {NORTHSTARCPP_CAPABILITY_AUDIO,NORTHSTARCPP_AUDIO_ABI,
        sizeof(NorthStarAudioApi),valid_table<NorthStarAudioApi>},
    {NORTHSTARCPP_CAPABILITY_INPUT,NORTHSTARCPP_INPUT_ABI,
        sizeof(NorthStarInputApi),valid_table<NorthStarInputApi>},
    {NORTHSTARCPP_CAPABILITY_ASSETS,NORTHSTARCPP_ASSETS_ABI,
        sizeof(NorthStarAssetsApi),valid_table<NorthStarAssetsApi>},
    {NORTHSTARCPP_CAPABILITY_LOGGER,NORTHSTARCPP_LOGGER_ABI,
        sizeof(NorthStarLoggerApi),valid_table<NorthStarLoggerApi>},
    {NORTHSTARCPP_CAPABILITY_PERLIN,NORTHSTARCPP_PERLIN_ABI,
        sizeof(NorthStarPerlinApi),valid_table<NorthStarPerlinApi>},
    {NORTHSTARCPP_CAPABILITY_GEOMETRY,NORTHSTARCPP_GEOMETRY_ABI,
        sizeof(NorthStarGeometryApi),valid_table<NorthStarGeometryApi>},
    {NORTHSTARCPP_CAPABILITY_COLLISION,NORTHSTARCPP_COLLISION_ABI,
        sizeof(NorthStarCollisionApi),valid_table<NorthStarCollisionApi>},
    {NORTHSTARCPP_CAPABILITY_TIME,NORTHSTARCPP_TIME_ABI,
        sizeof(NorthStarTimeApi),valid_table<NorthStarTimeApi>},
    {NORTHSTARCPP_CAPABILITY_SCRIPTING,NORTHSTARCPP_SCRIPTING_ABI,
        sizeof(NorthStarScriptingApi),valid_table<NorthStarScriptingApi>},
}};
template<class Api>
bool valid_render_extension(NorthStarModuleQuery query,std::uint32_t abi) {
    auto const* desc=query(NORTHSTARCPP_MODULE_ABI,
        NORTHSTARCPP_CAPABILITY_RENDER,abi);
    return desc && desc->size>=sizeof(NorthStarModule) &&
        desc->host_abi==NORTHSTARCPP_MODULE_ABI &&
        desc->api_version==abi &&
        desc->api_size>=sizeof(Api) && desc->api && desc->last_error &&
        desc->capability &&
        std::strcmp(desc->capability,NORTHSTARCPP_CAPABILITY_RENDER)==0 &&
        northstarcpp::valid_api(*static_cast<Api const*>(desc->api));
}
void inspect_render_extensions(NorthStarModuleQuery query,ProviderChoice& candidate) {
    if(valid_render_extension<NorthStarRenderTrianglesApi>(
        query,NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI))
        candidate.verified_render_apis|=NS_RENDER_FEATURE_COLORED_GEOMETRY;
    if(valid_render_extension<NorthStarRenderMaterialsApi>(
        query,NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI))
        candidate.verified_render_apis|=NS_RENDER_FEATURE_TEXTURED_MATERIALS;
    if(valid_render_extension<NorthStarRenderMaterialsApiV2>(
        query,NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI))
        candidate.verified_render_apis|=NS_RENDER_FEATURE_MATERIAL_GRAPH;
    if(valid_render_extension<NorthStarRenderSkinnedMaterialsApi>(
        query,NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI))
        candidate.verified_render_apis|=NS_RENDER_FEATURE_GPU_SKINNING;
    if(valid_render_extension<NorthStarRenderSurfacesApi>(
        query,NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI))
        candidate.verified_render_apis|=NS_RENDER_FEATURE_WORLD_SURFACES;
    if(valid_render_extension<NorthStarRenderCharacterApi>(
        query,NORTHSTARCPP_RENDER_CHARACTER_MODULE_ABI))
        candidate.verified_render_apis|=NS_RENDER_FEATURE_CHARACTER;
    if(valid_render_extension<NorthStarRenderLightsApi>(
        query,NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI))
        candidate.verified_render_apis|=NS_RENDER_FEATURE_DYNAMIC_LIGHTS;
    // Timing API has no valid_api() overload: validate the POD table here,
    // without invoking read() or initializing a GPU device.
    auto const* timing=query(NORTHSTARCPP_MODULE_ABI,
        NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_TIMING_MODULE_ABI);
    if(timing&&timing->size>=sizeof(NorthStarModule)&&
       timing->host_abi==NORTHSTARCPP_MODULE_ABI&&
       timing->api_version==NORTHSTARCPP_RENDER_TIMING_MODULE_ABI&&
       timing->api_size>=sizeof(NorthStarRenderTimingApi)&&
       timing->api&&timing->last_error&&timing->capability&&
       std::strcmp(timing->capability,NORTHSTARCPP_CAPABILITY_RENDER)==0) {
        auto const* table=static_cast<NorthStarRenderTimingApi const*>(timing->api);
        if(table->abi_version==NORTHSTARCPP_RENDER_TIMING_ABI&&table->read)
            candidate.verified_render_apis|=NS_RENDER_FEATURE_GPU_TIMING;
    }
    auto const* advertised=query(NORTHSTARCPP_MODULE_ABI,
        NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI);
    if(advertised&&advertised->size>=sizeof(NorthStarModule) &&
       advertised->host_abi==NORTHSTARCPP_MODULE_ABI &&
       advertised->api_version==NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI &&
       advertised->api_size>=sizeof(NorthStarRenderFeaturesApi)&&
       advertised->api&&advertised->last_error&&advertised->capability&&
       std::strcmp(advertised->capability,NORTHSTARCPP_CAPABILITY_RENDER)==0) {
        auto const* metadata=static_cast<NorthStarRenderFeaturesApi const*>(advertised->api);
        if(metadata->abi_version==NORTHSTARCPP_RENDER_FEATURES_ABI &&
           metadata->struct_size>=sizeof(NorthStarRenderFeaturesApi)) {
            candidate.advertised_render_features=metadata->implemented_features;
            candidate.has_feature_manifest=true;
        }
    }
}
bool safe_id(std::string const& id) {
    return !id.empty()&&id.size()<=96&&
        std::all_of(id.begin(),id.end(),[](unsigned char c) {
            return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';
        });
}
QString qt_path(fs::path const& p) {return QString::fromStdWString(p.wstring());}
std::string native(QString const& value) {return value.toUtf8().toStdString();}
bool is_module_candidate(QString const& filename) {
    auto lower=filename.toLower();
    if(!lower.endsWith(".dll"))return false;
    // The first-party assets provider ships as AssetManager.dll.
    if(lower=="assetmanager.dll")return true;
    // Other native project DLLs follow capability_provider.dll convention.
    // Do not load the application's unrelated Qt/CRT runtime DLLs.
    for(auto prefix:{"render_","ecs_","physics_","audio_","input_",
                     "assets_","logger_","perlin_","time_","geometry_",
                     "collision_","scripting_","script_"})
        if(lower.startsWith(prefix))return true;
    return false;
}
std::vector<fs::path> search_directories(fs::path const& exe) {
    auto add=[](std::vector<fs::path>& result,fs::path path) {
        std::error_code ec;
        if(fs::is_directory(path,ec)&&!ec) {
            auto normal=fs::absolute(path).lexically_normal();
            if(std::find(result.begin(),result.end(),normal)==result.end())
                result.push_back(normal);
        }
    };
    std::vector<fs::path> paths;
    add(paths,exe);
    add(paths,exe/"bin");
    add(paths,exe.parent_path());
    add(paths,exe.parent_path()/"bin");
    // Match the installed bootstrap's plugin path when explicitly configured.
    for(auto const& base:{exe,exe.parent_path()}) {
        QFile file(qt_path(base/"bootstrap.json"));
        if(!file.open(QIODevice::ReadOnly)||file.size()>65536)continue;
        auto doc=QJsonDocument::fromJson(file.readAll());
        if(!doc.isObject())continue;
        auto configured=doc.object().value("plugins").toObject().value("path").toString();
        if(configured.isEmpty())continue;
        auto path=fs::path(configured.toStdWString());
        if(path.is_relative())path=base/path;
        add(paths,path);
    }
    return paths;
}
}
std::string provider_environment_key(std::string const& capability) {
    if(capability.rfind("engine.",0)!=0||capability.size()<=7||capability.size()>96)
        throw std::invalid_argument("Invalid provider capability");
    std::string key="NORTHSTARCPP_";
    for(char c:capability.substr(7)) {
        if(!(std::islower(static_cast<unsigned char>(c))||
             std::isdigit(static_cast<unsigned char>(c))||c=='_'||c=='.'))
            throw std::invalid_argument("Invalid provider capability");
        key+=c=='.'?'_':static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return key+"_PROVIDER";
}
std::vector<ProviderChoice> discover_provider_choices(fs::path const& executable) {
    auto const dirs=search_directories(executable);
    std::map<std::pair<std::string,std::string>,ProviderChoice> found;
    std::set<std::pair<std::string,std::string>> defaults;
    // Builtin implementations cannot be introspected by dlopen() and are
    // listed from the engine-generated plugin inventory.
    for(auto const& directory:dirs) {
        QFile file(qt_path(directory/"northstarcpp.plugins.json"));
        if(!file.open(QIODevice::ReadOnly)||file.size()>128*1024)continue;
        auto const doc=QJsonDocument::fromJson(file.readAll());
        if(!doc.isObject()||doc.object().value("engine")!="NorthStarCpp")continue;
        for(auto const& v:doc.object().value("plugins").toArray()) {
            auto const object=v.toObject();
            auto const capability=native(object.value("capability").toString());
            auto const id=native(object.value("provider").toString());
            if(capability.rfind("engine.",0)!=0||!safe_id(id))continue;
            auto const key=std::make_pair(capability,id);
            if(object.value("default").toBool())defaults.insert(key);
            if(object.value("kind")=="builtin") {
                found.emplace(key,ProviderChoice{capability,id,"embedded","builtin",true,
                    defaults.count(key)!=0});
            }
        }
    }
    // Scan only flat module roots, bounded to 256 candidates; query every
    // engine API contract on each binary and validate the table before listing.
    std::set<fs::path> visited;
    for(auto const& directory:dirs) {
        auto entries=QDir(qt_path(directory)).entryInfoList(
            QStringList{"*.dll","*.DLL"},QDir::Files|QDir::NoSymLinks,QDir::Name);
        for(auto const& info:entries) {
            if(visited.size()>=256)break;
            if(!is_module_candidate(info.fileName()))continue;
            auto absolute=fs::path(info.absoluteFilePath().toStdWString());
            if(!visited.insert(absolute).second)continue;
            QLibrary library(info.absoluteFilePath());
            auto raw=library.resolve("northstar_module_query");
            if(!raw)continue;
            auto query=reinterpret_cast<NorthStarModuleQuery>(raw);
            for(auto const& contract:expected) {
                auto const* module=query(NORTHSTARCPP_MODULE_ABI,
                    contract.capability,contract.abi);
                if(!module||module->size<sizeof(NorthStarModule)||
                   module->host_abi!=NORTHSTARCPP_MODULE_ABI||
                   module->api_version!=contract.abi||
                   module->api_size<contract.table_bytes||
                   !module->api||!module->last_error||
                   !module->provider||!module->version||!module->capability||
                   std::strcmp(module->capability,contract.capability)!=0||
                   !contract.valid(module->api))
                    continue;
                auto id=northstarcpp::canonical_provider_id(
                    module->provider,contract.capability);
                if(!safe_id(id))continue;
                auto const key=std::make_pair(std::string(contract.capability),id);
                ProviderChoice choice{contract.capability,id,
                    module->version,absolute.u8string(),false,
                    defaults.count(key)!=0};
                if(choice.capability==NORTHSTARCPP_CAPABILITY_RENDER)
                    inspect_render_extensions(query,choice);
                found[key]=std::move(choice);
            }
        }
    }
    std::vector<ProviderChoice> result;
    for(auto& [key,value]:found) {
        value.recommended=defaults.count(key)!=0;
        result.push_back(std::move(value));
    }
    return result;
}
} // namespace northstarcpp::projects
