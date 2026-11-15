// Dependency-ordered Material Graph / Material Function shader precache planner.
// Resolves from VFS and compiles typed HLSL without decoding textures or
// changing the versioned engine.render / engine.assets plugin ABI.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include <cstddef>

namespace northstarcpp {
class AssetStore;
using MaterialPrecacheReader=std::function<std::string(std::string const&)>;
struct MaterialPrecacheVariant {
    std::string material_reference;
    std::string graph_shader_defines;
    unsigned blend_mode=0;
    float alpha_cutoff=0.5f;
    std::vector<std::string> dependencies;
    double graph_compile_ms=0.0;
};
struct MaterialPrecachePlan {
    std::vector<std::string> function_order; // dependencies before their callers
    std::vector<MaterialPrecacheVariant> variants;
    std::size_t bytes_read=0;
    double planning_ms=0.0;
};
MaterialPrecachePlan build_material_precache_plan(
    MaterialPrecacheReader const& reader,
    std::vector<std::string> const& material_references);
MaterialPrecachePlan build_material_precache_plan(
    AssetStore const& assets,
    std::vector<std::string> const& material_references);
} // namespace northstarcpp
