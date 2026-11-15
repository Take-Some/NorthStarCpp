// Contract-level regression. Feature declarations are NOT runtime validation.
#include <northstarcpp/render_feature_requirements.hpp>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
    using namespace northstarcpp::render;
    try {
        validate_feature_requirements({});
        validate_feature_requirements({"depth","material_graph","hdr"});
        bool rejected=false;
        try {validate_feature_requirements({"hdr","hdr"});}
        catch(std::invalid_argument const&){rejected=true;}
        if(!rejected)return 1;
        rejected=false;
        try {validate_feature_requirements({"not_a_renderer_feature"});}
        catch(std::invalid_argument const&){rejected=true;}
        if(!rejected)return 2;

        constexpr auto vulkanApis=NS_RENDER_FEATURE_COLORED_GEOMETRY |
                                  NS_RENDER_FEATURE_TEXTURED_MATERIALS |
                                  NS_RENDER_FEATURE_MATERIAL_GRAPH |
                                  NS_RENDER_FEATURE_GPU_SKINNING |
                                  NS_RENDER_FEATURE_WORLD_SURFACES |
                                  NS_RENDER_FEATURE_DYNAMIC_LIGHTS |
                                  NS_RENDER_FEATURE_GPU_TIMING;
        constexpr auto vulkanPasses=NS_RENDER_FEATURE_COLORED_GEOMETRY |
                                    NS_RENDER_FEATURE_DEPTH_TEST |
                                    NS_RENDER_FEATURE_GPU_TIMING;
        auto missing=missing_feature_requirements(
            {"depth","material_graph","hdr","gpu_timing"},
            vulkanApis,vulkanPasses,true);
        if(missing!=std::vector<std::string>({"hdr"}))return 3;
        if(!missing_feature_requirements({"depth"},0,0,false).size())return 4;
        if(!missing_feature_requirements(
            {"textured_materials","material_graph","gpu_skinning",
             "world_surfaces","dynamic_lights"},vulkanApis,
            vulkanPasses,true).empty())return 10;
        if(!missing_feature_requirements({},0,0,false).empty())return 5;

        // D3D12 may have published but not yet runtime-validated passes;
        // feature declarations are enough to satisfy the *prelaunch* contract.
        constexpr auto dxApis=NS_RENDER_FEATURE_COLORED_GEOMETRY |
                              NS_RENDER_FEATURE_TEXTURED_MATERIALS |
                              NS_RENDER_FEATURE_MATERIAL_GRAPH |
                              NS_RENDER_FEATURE_DYNAMIC_LIGHTS |
                              NS_RENDER_FEATURE_GPU_TIMING;
        constexpr auto dxPasses=NS_RENDER_FEATURE_DEPTH_TEST |
                                NS_RENDER_FEATURE_HDR |
                                NS_RENDER_FEATURE_CSM |
                                NS_RENDER_FEATURE_BLOOM;
        if(!missing_feature_requirements(
            {"depth","textured_materials","material_graph","hdr","csm","bloom"},
            dxApis,dxPasses,true).empty())return 6;
        if(missing_feature_requirements(
            {"gpu_skinning","world_surfaces"},dxApis,dxPasses,true).size()!=2)return 7;
        if(missing_feature_requirements(
            {"hdr"},dxApis,dxPasses,false)!=std::vector<std::string>{"hdr"})
            return 8;
        return 0;
    }catch(...) {return 9;}
}
