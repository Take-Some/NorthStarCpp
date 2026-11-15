// Provider-neutral graph emission translated from the validated HLSL ABI v2
// macro DSL into GLSL. This is not a second material graph parser.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace northstarcpp::render::vulkan {
std::string validated_material_graph_glsl(
    const char* source,std::uint32_t size,
    std::uint32_t blend_mode,float alpha_cutoff);
std::vector<std::uint32_t> compile_material_fragment_spirv(
    const std::string& shader_source);
}
