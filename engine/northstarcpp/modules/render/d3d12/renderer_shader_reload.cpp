// D3D12 shader hot reload: stage-specific DXIL, immutable async PSO builds.
// Existing Render ABI 5 and Material Graph variant caches are unchanged.
#include "renderer_private.hpp"
#include <cstdio>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace northstarcpp::render::d3d12::detail {
namespace {
// DXIL containers use DXBC envelope, with an actual DXIL part.
// Rejecting DXBC shader-model-5 blobs is essential: v2 promises DXIL only.
std::uint32_t word(const std::uint8_t* bytes,std::size_t pos) {
    return std::uint32_t(bytes[pos])|
           (std::uint32_t(bytes[pos+1])<<8)|
           (std::uint32_t(bytes[pos+2])<<16)|
           (std::uint32_t(bytes[pos+3])<<24);
}
bool valid_dxil(const std::uint8_t* bytes,std::uint32_t size) {
    if(!bytes || size<44 || size>NORTHSTARCPP_SHADER_RELOAD_MAX_BINARY_BYTES ||
       std::memcmp(bytes,"DXBC",4)!=0 || word(bytes,24)!=size)return false;
    const std::uint32_t parts=word(bytes,28);
    if(!parts || parts>128 || std::uint64_t(parts)*4+32>size)return false;
    bool dxil=false;
    for(std::uint32_t i=0;i<parts;++i) {
        const auto off=word(bytes,32+std::size_t(i)*4);
        if(off>size || size-off<8)return false;
        const auto partBytes=word(bytes,off+4);
        if(partBytes>size-off-8)return false;
        if(std::memcmp(bytes+off,"DXIL",4)==0)dxil=true;
    }
    return dxil;
}
std::size_t safe_len(const char* text) {
    if(!text)throw std::invalid_argument("Null shader material reference");
    std::size_t size=0;
    while(size<1025 && text[size])++size;
    if(!size || size>1024)
        throw std::invalid_argument("Shader material reference is out of bounds");
    return size;
}
}
void RendererState::set_hot_material_references(const char* const* refs,
                                                std::uint32_t count) {
    if(count> NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES ||
       count!=material_draws.size() || (count && !refs))
        throw std::invalid_argument("D3D12 shader binding count differs from material batches");
    std::vector<std::string> values;
    values.reserve(count);
    for(std::uint32_t i=0;i<count;++i)
        values.emplace_back(refs[i],safe_len(refs[i]));
    hot_references=std::move(values);
}
void RendererState::queue_hot_shader(const char* reference,std::uint32_t stage,
                                    std::uint32_t format,const std::uint8_t* bytes,
                                    std::uint32_t count) {
    const auto size=safe_len(reference);
    if(format!=NS_SHADER_BINARY_DXIL ||
       stage<NS_SHADER_PROGRAM_VERTEX ||
       stage>NS_SHADER_PROGRAM_FRAGMENT_GBUFFER ||
       !valid_dxil(bytes,count))
        throw std::invalid_argument("D3D12 hot reload requires valid DXIL SM6 stage bytecode");
    auto found=std::find(hot_references.begin(),hot_references.end(),
                         std::string(reference,size));
    if(found==hot_references.end())
        throw std::invalid_argument("D3D12 material shader reference is not bound");
    const std::size_t index=std::size_t(found-hot_references.begin());
    const auto& draw=material_draws.at(index);
    if((stage==NS_SHADER_PROGRAM_FRAGMENT_FORWARD && draw.blend_mode!=2) ||
       (stage==NS_SHADER_PROGRAM_FRAGMENT_GBUFFER && draw.blend_mode==2))
        throw std::invalid_argument("DXIL pixel stage does not match material render pass");
    const std::string key(reference,size);
    if(hot_shaders.find(key)==hot_shaders.end() && hot_shaders.size()>=32)
        throw std::runtime_error("D3D12 hot PSO budget is 32 material keys");
    auto& entry=hot_shaders[key];
    entry.binaries[stage-1].assign(bytes,bytes+count);
    entry.blend_mode=draw.blend_mode;
    entry.prefix=draw.variant_prefix;
    entry.revision=++hot_sequence;
    entry.state=NS_SHADER_RELOAD_QUEUED;
    entry.error.clear();
    if(std::find(hot_queue.begin(),hot_queue.end(),key)==hot_queue.end())
        hot_queue.push_back(key);
}
bool RendererState::poll_hot_shader(const char* reference,
    NorthStarShaderReloadStatus& status) const {
    if(status.struct_size<sizeof(status))return false;
    const auto len=safe_len(reference);
    status.state=NS_SHADER_RELOAD_UNKNOWN;
    status.revision=status.installed_revision=0;
    status.diagnostic[0]=0;
    auto item=hot_shaders.find(std::string(reference,len));
    if(item==hot_shaders.end())return true;
    auto const& entry=item->second;
    status.state=entry.state;
    status.revision=entry.revision;
    status.installed_revision=entry.installed_revision;
    if(!entry.error.empty())
        std::snprintf(status.diagnostic,sizeof(status.diagnostic),"%s",
                      entry.error.c_str());
    return true;
}
ID3D12PipelineState* RendererState::hot_pipeline(std::size_t index) const noexcept {
    if(index>=hot_references.size() || index>=material_draws.size())return nullptr;
    auto it=hot_shaders.find(hot_references[index]);
    if(it==hot_shaders.end() || (it->second.blend_mode!=material_draws[index].blend_mode ||
        it->second.prefix!=material_draws[index].variant_prefix))
        return nullptr;
    return it->second.installed.Get();
}
void RendererState::pump_hot_shaders() {
    // Caller holds a completed graphics fence (draw()/set_material_batches()).
    // Never release the currently used PSO while GPU commands still reference it.
    if(hot_worker.valid() &&
       hot_worker.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        HotBuild result;
        try{result=hot_worker.get();}
        catch(std::exception const& e){result.error=e.what();}
        catch(...){result.error="Unknown D3D12 PSO worker failure";}
        auto it=hot_shaders.find(hot_active_key);
        if(it!=hot_shaders.end() && it->second.revision==hot_active_revision) {
            auto& entry=it->second;
            if(result.pipeline) {
                entry.installed=std::move(result.pipeline);
                entry.installed_revision=entry.revision;
                entry.state=NS_SHADER_RELOAD_READY;
                entry.error.clear();
            } else {
                entry.state=NS_SHADER_RELOAD_FAILED;
                entry.error=result.error.empty()?"D3D12 failed to build DXIL PSO":
                                                   std::move(result.error);
            }
        }
        hot_active_key.clear();
        hot_active_revision=0;
    }
    if(hot_worker.valid())return;
    while(!hot_queue.empty()) {
        auto key=std::move(hot_queue.front());
        hot_queue.pop_front();
        auto found=hot_shaders.find(key);
        if(found==hot_shaders.end() ||
           found->second.state!=NS_SHADER_RELOAD_QUEUED)continue;
        auto& entry=found->second;
        hot_active_key=key;
        hot_active_revision=entry.revision;
        entry.state=NS_SHADER_RELOAD_BUILDING;
        const auto vertex=entry.binaries[0];
        const auto pixel=entry.binaries[entry.blend_mode==2?1:2];
        const auto prefix=entry.prefix;
        const auto mode=entry.blend_mode;
        const bool hdr=hdr_enabled;
        const bool v2=gbuffer_v2;
        auto context=device; // worker owns the D3D12 device and root signature
        auto signature=material_signature;
        hot_worker=std::async(std::launch::async,
            [context,signature,vertex,pixel,prefix,mode,hdr,v2]() -> HotBuild {
                try {
                    return {create_material_graph_pso(context,signature,prefix,
                                                    mode,hdr,v2,vertex,pixel),{}};
                }catch(std::exception const& e){return {{},e.what()};}
                catch(...){return {{}, "Unknown DXIL pipeline failure"};}
            });
        break;
    }
}
} // namespace northstarcpp::render::d3d12::detail
