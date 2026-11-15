// Project-scoped mandatory rendering capability contract. Public C++ utilities
// consumed by Project Catalog, Qt Browser and runtime Viewport.
// Unlike renderer feature *declarations*, requirements are strict: a missing
// extension or undeclared pass must never silently select another GPU backend.
#pragma once
#include <northstarcpp/render_features_api.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace northstarcpp::render {
struct FeatureRequirement {
    const char* key;
    const char* label;
    std::uint64_t bit;
    bool requires_extension; // validated exported function table, not a claim
};
inline constexpr std::array<FeatureRequirement,20> feature_requirements{{
    {"geometry","Colored geometry",NS_RENDER_FEATURE_COLORED_GEOMETRY,true},
    {"depth","Depth buffer and occlusion",NS_RENDER_FEATURE_DEPTH_TEST,false},
    {"textured_materials","PBR textured materials",NS_RENDER_FEATURE_TEXTURED_MATERIALS,true},
    {"material_graph","Programmable Material Graph v2",NS_RENDER_FEATURE_MATERIAL_GRAPH,true},
    {"gpu_skinning","GPU skinned materials",NS_RENDER_FEATURE_GPU_SKINNING,true},
    {"world_surfaces","World surface textures",NS_RENDER_FEATURE_WORLD_SURFACES,true},
    {"character","Character meshes and poses",NS_RENDER_FEATURE_CHARACTER,true},
    {"dynamic_lights","Dynamic light API",NS_RENDER_FEATURE_DYNAMIC_LIGHTS,true},
    {"gbuffer","G-buffer",NS_RENDER_FEATURE_GBUFFER,false},
    {"deferred","Deferred lighting",NS_RENDER_FEATURE_DEFERRED,false},
    {"csm","Cascaded Shadow Maps",NS_RENDER_FEATURE_CSM,false},
    {"pcss","PCSS soft shadows",NS_RENDER_FEATURE_PCSS,false},
    {"ssao","Screen Space Ambient Occlusion",NS_RENDER_FEATURE_SSAO,false},
    {"hdr","HDR pipeline",NS_RENDER_FEATURE_HDR,false},
    {"bloom","Bloom",NS_RENDER_FEATURE_BLOOM,false},
    {"exposure","Auto exposure",NS_RENDER_FEATURE_EXPOSURE,false},
    {"tonemap","Tonemapping",NS_RENDER_FEATURE_TONEMAP,false},
    {"fxaa","FXAA",NS_RENDER_FEATURE_FXAA,false},
    {"gpu_timing","GPU timestamp API",NS_RENDER_FEATURE_GPU_TIMING,true},
    {"transient_aliasing","Transient GPU resource aliasing",
        NS_RENDER_FEATURE_GPU_TRANSIENT_ALIASING,false}
}};
inline const FeatureRequirement* find_feature_requirement(const std::string& id) {
    auto it=std::find_if(feature_requirements.begin(),
        feature_requirements.end(),[&](const FeatureRequirement& feature) {
            return id==feature.key;
        });
    return it==feature_requirements.end()?nullptr:&*it;
}
inline void validate_feature_requirements(const std::vector<std::string>& features) {
    if(features.size()>feature_requirements.size())
        throw std::invalid_argument("Too many renderer feature requirements");
    std::vector<std::string> unique;
    for(const auto& id:features) {
        if(!find_feature_requirement(id))
            throw std::invalid_argument("Unknown renderer feature requirement: "+id);
        if(std::find(unique.begin(),unique.end(),id)!=unique.end())
            throw std::invalid_argument("Duplicate renderer feature requirement: "+id);
        unique.push_back(id);
    }
}
inline std::vector<std::string> missing_feature_requirements(
    const std::vector<std::string>& required,std::uint64_t verified_apis,
    std::uint64_t declared_features,bool declaration_available)
{
    validate_feature_requirements(required);
    std::vector<std::string> missing;
    for(const auto& id:required) {
        auto feature=find_feature_requirement(id);
        bool supported=feature->requires_extension?
            (verified_apis&feature->bit)!=0:
            declaration_available&&(declared_features&feature->bit)!=0;
        if(!supported)missing.push_back(id);
    }
    return missing;
}
} // namespace northstarcpp::render
