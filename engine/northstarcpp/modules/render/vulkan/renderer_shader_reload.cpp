// Vulkan material-fragment hot swap, optional provider extension ABI.
// Shader source is compiled asynchronously by the editor (glslc). Vulkan
// PSO creation is scheduled separately from the GPU render submission thread.
// No vkDeviceWaitIdle on hot swap; retirement follows completed_ fence.
#include "renderer_private.hpp"
#include "material_spirv.hpp"
#include "material_spirv.hpp"
#include <iterator>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace northstarcpp::render::vulkan {
namespace {
bool shader_entry_point(const std::uint32_t* words,std::uint32_t count,
                        std::uint32_t stage) {
    if(!words || count<6 || words[0]!=0x07230203 ||
       words[1]==0 || words[3]==0)return false;
    bool entry=false;
    const std::uint32_t expected=stage==NS_SHADER_PROGRAM_VERTEX?0u:4u;
    for(std::uint32_t pos=5;pos<count;) {
        const auto instruction=words[pos];
        std::uint32_t const size=instruction>>16;
        std::uint32_t const code=instruction&0xffffu;
        if(!size || size>count-pos)return false;
        if(code==15u && size>=4u && words[pos+1]==expected)entry=true;
        pos+=size;
    }
    return entry;
}
void check(VkResult code,char const* call) {
    if(code!=VK_SUCCESS)
        throw std::runtime_error(std::string(call)+" returned VkResult="+
                                 std::to_string(static_cast<int>(code)));
}
} // namespace

void Renderer::set_material_references(const char* const* references,
                                        std::uint32_t count) {
    if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES ||
       count+(sky_enabled_?1u:0u)!=material_sources_.size() ||
       (count && !references))
        { std::fprintf(stderr,"[WARN] [vulkan.shader] ref_count=%u material_count=%zu sky=%u\n",count,material_sources_.size(),sky_enabled_?1u:0u);
          throw std::invalid_argument("Shader reload material binding count mismatch"); }
    std::vector<std::string> next;
    next.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        if(!references[i])
            throw std::invalid_argument("Null material shader binding");
        auto const len=strnlen(references[i],1025);
        if(len==0 || len>1024)
            throw std::invalid_argument("Invalid material shader binding length");
        next.emplace_back(references[i],len);
    }
    material_references_=std::move(next);
}
void Renderer::queue_material_fragment_spirv(const char* ref,
    const std::uint32_t* spirv,std::uint32_t count) {
    if(!ref)throw std::invalid_argument("Null material");
    auto const found=std::find(material_references_.begin(),
                              material_references_.end(),std::string(ref));
    if(found==material_references_.end())
        throw std::invalid_argument("Material is not bound to current render batches");
    auto index=std::size_t(found-material_references_.begin());
    if(deferred_enabled_ && material_sources_.at(index).blend_mode!=2)
        throw std::invalid_argument("Shader Reload v1 is forward only; use v2 for GBuffer");
    queue_material_stage_spirv(ref,NS_SHADER_PROGRAM_FRAGMENT_FORWARD,spirv,count);
}
void Renderer::queue_material_stage_spirv(const char* ref,std::uint32_t stage,
    const std::uint32_t* spirv,std::uint32_t count) {
    if(!ref || !spirv || !count || count>NORTHSTARCPP_SHADER_RELOAD_MAX_SPIRV_WORDS ||
       stage<NS_SHADER_PROGRAM_VERTEX || stage>NS_SHADER_PROGRAM_FRAGMENT_GBUFFER)
        throw std::invalid_argument("Invalid shader hot-reload SPIR-V size");
    std::size_t const length=strnlen(ref,1025);
    if(!length||length>1024)
        throw std::invalid_argument("Invalid hot-reload material reference");
    auto const found=std::find(material_references_.begin(),
                              material_references_.end(),std::string(ref,length));
    if(found==material_references_.end())
        throw std::invalid_argument("Material reference is not bound to a current render batch");
    auto const index=std::size_t(found-material_references_.begin());
    auto const mode=material_sources_.at(index).blend_mode;
    if(stage==NS_SHADER_PROGRAM_FRAGMENT_GBUFFER &&
       (!deferred_enabled_ || mode==2))
        throw std::invalid_argument("GBuffer requires a deferred opaque/masked material");
    if(!shader_entry_point(spirv,count,stage))
        throw std::invalid_argument("Shader hot reload requires valid Fragment-stage SPIR-V");
    std::string key(ref,length);
    if(hot_materials_.find(key)==hot_materials_.end() &&
       hot_materials_.size()>=32)
        throw std::runtime_error("Hot shader cache is limited to 32 material keys");
    auto& entry=hot_materials_[key];
    entry.stages[stage-1].assign(spirv,spirv+count);
    entry.blend_mode=mode;
    entry.revision=++hot_sequence_;
    entry.state=NS_SHADER_RELOAD_QUEUED;
    entry.error.clear();
    // Keep only the latest source revision if saving while an old PSO builds.
    if(std::find(hot_pending_.begin(),hot_pending_.end(),key)==hot_pending_.end())
        hot_pending_.push_back(key);
}
bool Renderer::shader_reload_status(
    const char* ref,NorthStarShaderReloadStatus& result) const {
    if(!ref || result.struct_size<sizeof(NorthStarShaderReloadStatus))
        return false;
    auto const size=strnlen(ref,1025);
    if(!size || size>1024)return false;
    auto found=hot_materials_.find(std::string(ref,size));
    result.state=NS_SHADER_RELOAD_UNKNOWN;
    result.revision=0;
    result.installed_revision=0;
    result.diagnostic[0]=0;
    if(found==hot_materials_.end())return true;
    auto const& item=found->second;
    result.state=item.state;
    result.revision=item.revision;
    result.installed_revision=item.installed_revision;
    if(!item.error.empty())
        std::snprintf(result.diagnostic,sizeof(result.diagnostic),"%s",
                      item.error.c_str());
    return true;
}
Renderer::HotBuild Renderer::build_shader_override(
    const std::array<std::vector<std::uint32_t>,3>& code,std::uint32_t mode) {
    VkShaderModule vs=VK_NULL_HANDLE,fs=VK_NULL_HANDLE,gb=VK_NULL_HANDLE;
    auto shader=[&](const std::uint32_t* words,std::size_t count,
                    VkShaderModule& out) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.pCode=words;ci.codeSize=count*4;
        check(vkCreateShaderModule(device_,&ci,nullptr,&out),
              "vkCreateShaderModule(hot shader program)");
    };
    HotBuild result{};
    try {
        if(!code[0].empty())shader(code[0].data(),code[0].size(),vs);
        if(!code[1].empty())shader(code[1].data(),code[1].size(),fs);
        result.forward=create_pipeline_for_material_fragment(
            fs?fs:material_fragment_shader_,mode,false,vs);
        if(deferred_enabled_ && mode!=2) {
            if(!code[2].empty())
                shader(code[2].data(),code[2].size(),gb);
            else shader(shaders::material_gbuffer_fragment,
                        std::size(shaders::material_gbuffer_fragment),gb);
            result.gbuffer=create_pipeline_for_material_fragment(
                gb,mode,true,vs);
        }
        if(vs)vkDestroyShaderModule(device_,vs,nullptr);
        if(fs)vkDestroyShaderModule(device_,fs,nullptr);
        if(gb)vkDestroyShaderModule(device_,gb,nullptr);
        return result;
    }catch(...) {
        if(result.forward)vkDestroyPipeline(device_,result.forward,nullptr);
        if(result.gbuffer)vkDestroyPipeline(device_,result.gbuffer,nullptr);
        if(vs)vkDestroyShaderModule(device_,vs,nullptr);
        if(fs)vkDestroyShaderModule(device_,fs,nullptr);
        if(gb)vkDestroyShaderModule(device_,gb,nullptr);
        throw;
    }
}
void Renderer::pump_shader_reload() {
    // Called only after the frame's graphics completion fence is signaled.
    // The previous recorded command buffer is no longer using old PSOs.
    if(hot_worker_.valid() &&
       hot_worker_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        HotBuild completed;
        try {completed=hot_worker_.get();}
        catch(std::exception const& e) {completed.error=e.what();}
        catch(...) {completed.error="Unknown Vulkan pipeline compilation failure";}
        auto found=hot_materials_.find(hot_build_key_);
        if(found!=hot_materials_.end() &&
           found->second.revision==hot_build_revision_) {
            auto& item=found->second;
            if(completed.forward &&
               (!deferred_enabled_ || item.blend_mode==2 || completed.gbuffer)) {
                if(item.installed_forward)
                    vkDestroyPipeline(device_,item.installed_forward,nullptr);
                if(item.installed_gbuffer)
                    vkDestroyPipeline(device_,item.installed_gbuffer,nullptr);
                item.installed_forward=completed.forward;
                item.installed_gbuffer=completed.gbuffer;
                completed.forward=completed.gbuffer=VK_NULL_HANDLE;
                item.installed_revision=item.revision;
                item.state=NS_SHADER_RELOAD_READY;
                item.error.clear();
            } else {
                item.state=NS_SHADER_RELOAD_FAILED;
                item.error=completed.error.empty()?
                    "Vulkan driver rejected shader pipeline":completed.error;
            }
        }
        if(completed.forward)vkDestroyPipeline(device_,completed.forward,nullptr);
        if(completed.gbuffer)vkDestroyPipeline(device_,completed.gbuffer,nullptr);
        hot_build_key_.clear();
        hot_build_revision_=0;
    }
    if(hot_worker_.valid() || hot_pending_.empty())return;
    while(!hot_pending_.empty()) {
        auto key=std::move(hot_pending_.front());
        hot_pending_.pop_front();
        auto found=hot_materials_.find(key);
        if(found==hot_materials_.end() ||
           found->second.state!=NS_SHADER_RELOAD_QUEUED)continue;
        auto& item=found->second;
        auto bytes=item.stages; // immutable worker-owned SPIR-V stage snapshots
        auto const mode=item.blend_mode;
        hot_build_key_=key;
        hot_build_revision_=item.revision;
        item.state=NS_SHADER_RELOAD_BUILDING;
        hot_worker_=std::async(std::launch::async,
            [this,bytes=std::move(bytes),mode]() -> HotBuild {
                try {return build_shader_override(bytes,mode);}
                catch(std::exception const& e) {
                    return {VK_NULL_HANDLE,VK_NULL_HANDLE,e.what()};
                }
                catch(...) {
                    return {VK_NULL_HANDLE,VK_NULL_HANDLE,"Unknown shader pipeline error"};
                }
            });
        break;
    }
}
void Renderer::stop_shader_reload() noexcept {
    // Resize and shutdown always synchronize the device before disposing PSOs.
    if(hot_worker_.valid()) {
        try {
            auto result=hot_worker_.get();
            if(result.forward)vkDestroyPipeline(device_,result.forward,nullptr);
            if(result.gbuffer)vkDestroyPipeline(device_,result.gbuffer,nullptr);
        }catch(...) {}
    }
    hot_build_key_.clear();hot_build_revision_=0;
    hot_pending_.clear();
    for(auto& [key,item]:hot_materials_) {
        if(item.installed_forward)vkDestroyPipeline(device_,item.installed_forward,nullptr);
        if(item.installed_gbuffer)vkDestroyPipeline(device_,item.installed_gbuffer,nullptr);
        item.installed_forward=item.installed_gbuffer=VK_NULL_HANDLE;
        item.state=NS_SHADER_RELOAD_QUEUED;
        hot_pending_.push_back(key);
    }
}
} // namespace northstarcpp::render::vulkan
