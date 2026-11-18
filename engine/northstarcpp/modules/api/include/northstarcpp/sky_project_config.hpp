// Unified project-owned sky contract for Player and Qt Editor.
// Render providers receive NorthStarSky / SkyUniforms; they never resolve VFS.
#pragma once
#include <northstarcpp/assets.hpp>
#include <northstarcpp/sky_environment.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace northstarcpp::project_sky {
struct Configuration {
    SkySettings settings;
    std::string mode="disabled";
    std::string drawable;
    // A missing profile permits scripts to opt in. An explicit Disabled
    // profile is authoritative for the scene, including during startup scripts.
    bool explicitly_disabled=false;
};

inline void validate_drawable_reference(std::string_view reference) {
    if(reference.empty() || reference.size()>512 || reference.front()=='/' ||
       reference.find('\\')!=reference.npos ||
       reference.find(':')!=reference.npos ||
       reference.find("//")!=reference.npos ||
       reference.find("..")!=reference.npos)
        throw std::invalid_argument("Sky drawable must be a safe VFS asset reference");
    for(unsigned char c:reference)
        if(!std::isalnum(c) && c!='/' && c!='.' && c!='_' && c!='-' && c!='@')
            throw std::invalid_argument("Invalid character in sky drawable VFS reference");
    auto const at=reference.find('@');
    auto const filename=reference.substr(0,at);
    auto const ext=filename.rfind('.');
    if(ext==filename.npos || (filename.substr(ext)!=".ydd" &&
       filename.substr(ext)!=".ydr") || (at!=reference.npos && at+1==reference.size()))
        throw std::invalid_argument("Sky drawable must reference a YDD/YDR model");
    // A YDD is a drawable dictionary: require a concrete element selection.
    if(filename.substr(ext)==".ydd" && at==reference.npos)
        throw std::invalid_argument("YDD sky dictionary requires an @drawable selector");
}

inline Configuration parse(std::string_view json,std::string_view scene={}) {
    Configuration result;
    std::istringstream stream{std::string(json)};
    boost::property_tree::ptree document;
    boost::property_tree::read_json(stream,document);
    // A legacy project profile belongs to its authored startup scene.
    if(!scene.empty()) {
        auto startup=document.get_optional<std::string>("startup_scene");
        if(startup) {
            if(std::filesystem::u8path(*startup).lexically_normal().generic_u8string()!=scene)
                return result;
        } else if(scene!="main.nscn" && scene!="main.nscene") {
            return result;
        }
    }
    auto settings=document.get_child_optional("sky");
    if(!settings)return result;
    if(!settings->data().empty() ||
       std::any_of(settings->begin(),settings->end(),[](auto const& entry) {
           return entry.first.empty();
       }))
        throw std::invalid_argument("Sky settings must be an object");
    result.mode=settings->get<std::string>("mode","procedural");
    if(result.mode!="procedural" && result.mode!="drawable" &&
       result.mode!="hybrid" && result.mode!="disabled")
        throw std::invalid_argument("Invalid project sky mode");
    result.drawable=settings->get<std::string>("drawable","");
    if(result.mode=="procedural" || result.mode=="disabled")
        result.drawable.clear();
    if(!result.drawable.empty())validate_drawable_reference(result.drawable);
    if(result.mode=="drawable" && result.drawable.empty())
        throw std::invalid_argument("Drawable sky mode needs an authored VFS reference");
    auto& s=result.settings;
    result.explicitly_disabled=result.mode=="disabled";
    s.enabled=(result.mode=="procedural" || result.mode=="hybrid");
    if(s.enabled)s.fog_density=.035;
    auto flag=[&](char const* name,bool& dst){
        if(auto v=settings->get_optional<bool>(name))dst=*v;
    };
    auto number=[&](char const* name,double& dst){
        if(auto v=settings->get_optional<double>(name))dst=*v;
    };
    flag("sun",s.sun);flag("moon",s.moon);
    flag("stars",s.stars);flag("clouds",s.clouds);flag("paused",s.paused);
    number("timeOfDay",s.time_of_day);
    number("dayLengthSeconds",s.day_length_seconds);
    number("latitudeDegrees",s.latitude_degrees);
    if(auto v=settings->get_optional<int>("dayOfYear"))s.day_of_year=*v;
    number("moonPhase",s.moon_phase);
    number("cloudCoverage",s.cloud_coverage);
    number("cloudSpeed",s.cloud_speed);
    number("haze",s.haze);
    number("fogDensity",s.fog_density);
    number("sunIntensity",s.sun_intensity);
    number("moonIntensity",s.moon_intensity);
    number("starDensity",s.star_density);
    if(auto preset=settings->get_optional<std::string>("preset"))s.preset=*preset;
    SkyEnvironment::validate(s);
    return result;
}
inline std::filesystem::path find_root(std::filesystem::path const& scene) {
    namespace fs=std::filesystem;
    auto const fallback=fs::absolute(scene).lexically_normal().parent_path();
    for(auto root=fallback;!root.empty();root=root.parent_path()) {
        std::error_code error;
        if(fs::is_regular_file(root/"northstarcpp.project.json",error))return root;
        if(root==root.parent_path())break;
    }
    return fallback;
}

inline std::string scene_reference(std::filesystem::path const& root,
                                   std::filesystem::path const& scene) {
    namespace fs=std::filesystem;
    if(scene.empty())return {};
    auto relative=root.empty()?scene:
        fs::absolute(scene).lexically_normal().lexically_relative(fs::absolute(root).lexically_normal());
    relative=relative.lexically_normal();
    if(relative.empty() || relative.has_root_path() || *relative.begin()=="..")
        return {};
    return relative.generic_u8string();
}

inline std::string scene_profile(std::string_view scene) {
    return std::string(scene)+".sky.json";
}

inline std::optional<std::string> read_document(AssetStore const& assets,
                                              std::string const& path) {
    std::uint64_t size=0;
    try {size=assets.size(path);}
    catch(std::runtime_error const&) {return std::nullopt;}
    if(!size || size>128u*1024u)
        throw std::invalid_argument("Sky document size is invalid");
    auto bytes=assets.read(path);
    return std::string(bytes.begin(),bytes.end());
}

inline Configuration load(AssetStore const& assets,std::string_view scene={}) {
    if(!scene.empty()) {
        if(auto profile=read_document(assets,scene_profile(scene)))
            return parse(*profile);
    }
    if(auto manifest=read_document(assets,"northstarcpp.project.json"))
        return parse(*manifest,scene);
    return {};
}
} // namespace northstarcpp::project_sky
