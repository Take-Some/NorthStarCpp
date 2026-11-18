// Materials subsystem: independent D3D12 implementation.
#include "renderer_private.hpp"
#include "../../shared/frame_memory_counters.hpp"
#include "../shared/shader_disk_cache.hpp"
#include "../shared/frame_texture_memo.hpp"
namespace frame_memory = northstarcpp::diagnostics::frame_memory;

namespace northstarcpp::render::d3d12::detail {
namespace {
std::uint64_t material_texture_fingerprint(NorthStarMaterialTexture const& image,
                                          DXGI_FORMAT format)
{
    std::uint64_t hash=14695981039346656037ull;
    auto mix=[&](std::uint64_t value) { hash=(hash^value)*1099511628211ull; };
    mix(image.width);
    mix(image.height);
    mix(format);
    // Sample for lookup only. Full byte comparison determines identity,
    // including when the caller reuses a pointer for changed content.
    const auto stride=std::max(1u,image.data_bytes/64);
    for(std::uint64_t index=0;index<image.data_bytes;index+=stride)
        mix(image.rgba8[index]);
    return hash;
}

bool matches_vertices(std::vector<NorthStarMaterialVertex> const& previous,
                      std::size_t offset,NorthStarMaterialBatch const& batch)
{
    return offset<=previous.size() && batch.vertex_count<=previous.size()-offset &&
        std::memcmp(previous.data()+offset,batch.vertices,
                    std::size_t(batch.vertex_count)*sizeof(NorthStarMaterialVertex))==0;
}
} // namespace
std::shared_ptr<MaterialGpuTexture> RendererState::material_texture(
        NorthStarMaterialTexture const& image,bool is_srgb,
        std::vector<MaterialTextureUpload>& pending,UploadPages::Batch& uploads){
        if(!image.rgba8 || !image.width || !image.height ||
           image.width>4096 || image.height>4096 ||
           std::uint64_t(image.width)*image.height*4!=image.data_bytes)
            throw std::invalid_argument("Invalid D3D12 authored material texture");
        const auto format=is_srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                                  DXGI_FORMAT_R8G8B8A8_UNORM;
        const auto key=material_texture_fingerprint(image,format);
        auto matches=[&](MaterialGpuTexture const& candidate) {
            return candidate.format==format && candidate.width==image.width &&
                candidate.height==image.height && candidate.source.size()==image.data_bytes &&
                std::memcmp(candidate.source.data(),image.rgba8,image.data_bytes)==0;
        };
        auto const [begin,end]=material_texture_cache.equal_range(key);
        for(auto it=begin;it!=end;++it)
            if(matches(*it->second))return it->second;
        for(auto const& upload:pending)
            if(upload.fingerprint==key && matches(*upload.texture))return upload.texture;

        auto texture=std::make_shared<MaterialGpuTexture>();
        texture->format=format;
        texture->width=image.width;
        texture->height=image.height;
        frame_memory::assign(texture->source,image.rgba8,image.rgba8+image.data_bytes,
            &NorthStarFrameMemoryCounters::texture_copy_bytes);
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=image.width;desc.Height=image.height;
        desc.DepthOrArraySize=1;desc.MipLevels=1;
        desc.Format=texture->format;desc.SampleDesc.Count=1;
        desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        auto gpu=heap(D3D12_HEAP_TYPE_DEFAULT);
        check(device->CreateCommittedResource(&gpu,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
            IID_PPV_ARGS(&texture->texture)),"Create D3D12 authored texture");
        UINT const pitch=(image.width*4+255u)&~255u;
        MaterialTextureUpload item{};
        item.texture=texture;item.width=image.width;item.height=image.height;
        item.pitch=pitch;
        item.fingerprint=key;
        item.staging=uploads.allocate(UINT64(pitch)*image.height);
        auto* data=item.staging.data;
        for(UINT y=0;y<image.height;++y)
            std::memcpy(data+UINT64(y)*pitch,
                        image.rgba8+UINT64(y)*image.width*4,
                        UINT64(image.width)*4);
        frame_memory::copied(&NorthStarFrameMemoryCounters::texture_copy_bytes,image.data_bytes);
        pending.push_back(std::move(item));
        return texture;
    }
void RendererState::set_material_batches(NorthStarMaterialBatch const* batches,UINT count){
        if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES||(count&&!batches))
            throw std::invalid_argument("Invalid D3D12 material batch count");
        std::size_t required_vertices=0;
        bool geometry_unchanged=true;
        for(UINT i=0;i<count;++i) {
            auto const& entry=batches[i];
            if(!entry.vertices || !entry.vertex_count || entry.vertex_count%3 ||
               entry.vertex_count>NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES-required_vertices)
                throw std::invalid_argument("Invalid high-detail D3D12 material vertices");
            auto finite_unit=[](float f){
                return std::isfinite(f)&&f>=0.f&&f<=1.f;
            };
            for(float color:entry.base_color)
                if(!finite_unit(color))
                    throw std::invalid_argument("Invalid material base color");
            if(!finite_unit(entry.roughness)||!finite_unit(entry.metallic))
                throw std::invalid_argument("Invalid material PBR constants");
            if(!matches_vertices(material_cpu_vertices,required_vertices,entry)) {
                geometry_unchanged=false;
                for(UINT v=0;v<entry.vertex_count;++v) {
                    auto const& point=entry.vertices[v];
                    for(float value:{point.x,point.y,point.z,point.nx,point.ny,
                                     point.nz,point.u,point.v})
                        if(!std::isfinite(value)||std::abs(value)>1.e6f)
                            throw std::invalid_argument("Nonfinite material mesh vertex");
                }
            }
            required_vertices+=entry.vertex_count;
        }
        geometry_unchanged &= required_vertices==material_cpu_vertices.size();
        auto& vertices=material_vertex_scratch;
        vertices.clear();
        if(!geometry_unchanged)
            frame_memory::reserve(vertices,required_vertices);
        std::vector<MaterialDraw> draws;
        std::vector<MaterialTextureUpload> pending;
        UploadPages::Batch uploads(upload_pages,device.Get(),fence->GetCompletedValue());
        frame_memory::reserve(draws,count);
        // One-call cache: repeated YDR sections share a texture handle.
        // A later frame always revalidates content, even at the same address.
        northstarcpp::render::FrameTextureMemo<
            std::shared_ptr<MaterialGpuTexture>> texture_memo;
        UINT first_vertex=0;
        for(UINT i=0;i<count;++i) {
            auto const& input=batches[i];
            MaterialDraw draw{};
            draw.first=first_vertex;
            first_vertex+=input.vertex_count;
            draw.count=input.vertex_count;
            std::copy(std::begin(input.base_color),std::end(input.base_color),
                      std::begin(draw.params.base_color));
            draw.params.factors[0]=input.roughness;
            draw.params.factors[1]=input.metallic;
            draw.params.factors[2]=input.unlit?1.f:0.f;
            for(unsigned slot=0;slot<5;++slot) {
                auto const& texture=input.textures[slot];
                if(!texture.rgba8&&!texture.width&&!texture.height&&!texture.data_bytes)
                    continue;
                auto const is_srgb=slot==0 || slot==4;
                draw.maps[slot]=texture_memo.get(texture,is_srgb?1u:0u,[&] {
                    return material_texture(texture,is_srgb,pending,uploads);
                });
                draw.params.present[0]|=1u<<slot;
            }
            if(!geometry_unchanged)
                frame_memory::append_geometry(vertices,input.vertices,
                                              input.vertices+input.vertex_count);
            draws.push_back(std::move(draw));
        }
        // Upload resources and descriptors are single-buffered. Validate and
        // prepare all incoming batches before touching the active frame.
        // Single-buffered GPU resources remain untouched when the exact
        // original materials, images and full-resolution vertices are reused.
        // If ANY content differs, preserve the fence/wait before writing.
        bool const reuse_existing_gpu=[&] {
            if(!geometry_unchanged || !pending.empty() ||
               count!=material_draws.size() || !material_vertex_gpu ||
               !material_constant_gpu || !material_srv_heap ||
               required_vertices>material_vertex_capacity ||
               count>material_batch_capacity)return false;
            for(UINT i=0;i<count;++i) {
                if(std::memcmp(&draws[i].params,&material_draws[i].params,
                               sizeof(MaterialParams))!=0)return false;
                for(unsigned slot=0;slot<5;++slot)
                    if(draws[i].maps[slot]!=material_draws[i].maps[slot])
                        return false;
            }
            return true;
        }();
        auto* old_heap=material_srv_heap.Get();
        if(count && !reuse_existing_gpu) {
            wait();
            ensure_material_buffers(required_vertices,count);
        }
        if(!pending.empty()) {
            check(allocator->Reset(),"Reset material upload command allocator");
            check(commands->Reset(allocator.Get(),nullptr),
                  "Reset material texture upload commands");
            for(auto const& image:pending) {
                D3D12_TEXTURE_COPY_LOCATION dest{};
                dest.pResource=image.texture->texture.Get();
                dest.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dest.SubresourceIndex=0;
                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource=image.staging.resource;
                source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                source.PlacedFootprint.Offset=image.staging.offset;
                source.PlacedFootprint.Footprint.Format=image.texture->format;
                source.PlacedFootprint.Footprint.Width=image.width;
                source.PlacedFootprint.Footprint.Height=image.height;
                source.PlacedFootprint.Footprint.Depth=1;
                source.PlacedFootprint.Footprint.RowPitch=image.pitch;
                commands->CopyTextureRegion(&dest,0,0,0,&source,nullptr);
                D3D12_RESOURCE_BARRIER transition{};
                transition.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                transition.Transition.pResource=image.texture->texture.Get();
                transition.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                transition.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
                transition.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                commands->ResourceBarrier(1,&transition);
            }
            check(commands->Close(),"Close authored material texture upload");
            ID3D12CommandList* work[]{commands.Get()};
            const auto ticket=++fence_value;
            material_uploads_in_flight.swap(pending);
            uploads.retire(ticket);
            queue->ExecuteCommandLists(1,work);
            check(queue->Signal(fence.Get(),ticket),"Signal D3D12 material upload fence");
            wait_for_fence(ticket);
            for(auto const& image:material_uploads_in_flight) {
                material_texture_cache.emplace(image.fingerprint,image.texture);
                material_cache_bytes+=image.texture->source.size();
            }
            material_uploads_in_flight.clear();
        }
        const auto heap_start=count?
            material_srv_heap->GetCPUDescriptorHandleForHeapStart():D3D12_CPU_DESCRIPTOR_HANDLE{};
        for(UINT i=0;i<draws.size();++i) {
            auto const& draw=draws[i];
            auto const* old=old_heap==material_srv_heap.Get() && i<material_draws.size()?
                &material_draws[i]:nullptr;
            if(!old || std::memcmp(&old->params,&draw.params,sizeof(MaterialParams))!=0)
                std::memcpy(material_constant_data+UINT64(i)*256,&draw.params,sizeof(MaterialParams));
            for(unsigned slot=0;slot<5;++slot) {
                if(old && old->maps[slot]==draw.maps[slot])continue;
                auto handle=heap_start;
                handle.ptr+=SIZE_T(i*5+slot)*material_descriptor_increment;
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format=draw.maps[slot]?draw.maps[slot]->format:
                    DXGI_FORMAT_R8G8B8A8_UNORM;
                srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Texture2D.MipLevels=1;
                device->CreateShaderResourceView(draw.maps[slot]?
                    draw.maps[slot]->texture.Get():nullptr,&srv,handle);
            }
        }
        if(!geometry_unchanged) {
            material_cpu_vertices.swap(vertices);
            material_vertices_dirty=true;
        }
        material_draws.swap(draws);
        draws.clear();
        // Keep active textures resident even above the cache target. Evict
        // only entries the committed draw list no longer references.
        constexpr std::uint64_t cache_target=96ull*1024*1024;
        for(auto it=material_texture_cache.begin();
            it!=material_texture_cache.end() && material_cache_bytes>cache_target;) {
            if(it->second.use_count()==1) {
                material_cache_bytes-=it->second->source.size();
                it=material_texture_cache.erase(it);
            } else ++it;
        }
    }
ComPtr<ID3D12PipelineState> create_material_graph_pso(
    ComPtr<ID3D12Device> device,
    ComPtr<ID3D12RootSignature> material_signature,
    std::string const& prefix,unsigned blend_mode,bool hdr_enabled,bool gbuffer_v2,
    const std::vector<std::uint8_t>& vertex,
    const std::vector<std::uint8_t>& pixel) {
        std::string const source=std::string(gbuffer_v2 && blend_mode!=2?
            "#define NS_GBUFFER_V2 1\n":"")+(hdr_enabled&&blend_mode==2?
            "#define NS_HDR_OUTPUT 1\n":"")+prefix+authored_material_shader;
        ComPtr<ID3DBlob> vs,ps;
        if(vertex.empty())vs=compile_material_variant(source,"material_vs","vs_5_0");
        if(pixel.empty())ps=compile_material_variant(source,blend_mode==2?
            "material_ps":"material_gbuffer_ps","ps_5_0");
        D3D12_INPUT_ELEMENT_DESC const input[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,
                offsetof(NorthStarMaterialVertex,x),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,
                offsetof(NorthStarMaterialVertex,nx),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,
                offsetof(NorthStarMaterialVertex,u),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature=material_signature.Get();
        desc.VS=vertex.empty()?D3D12_SHADER_BYTECODE{vs->GetBufferPointer(),vs->GetBufferSize()}:
            D3D12_SHADER_BYTECODE{vertex.data(),vertex.size()};
        desc.PS=pixel.empty()?D3D12_SHADER_BYTECODE{ps->GetBufferPointer(),ps->GetBufferSize()}:
            D3D12_SHADER_BYTECODE{pixel.data(),pixel.size()};
        desc.InputLayout={input,3};
        desc.SampleMask=UINT_MAX;desc.SampleDesc.Count=1;
        desc.DSVFormat=DXGI_FORMAT_D32_FLOAT;
        desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable=TRUE;
        desc.DepthStencilState.DepthEnable=TRUE;
        desc.DepthStencilState.DepthWriteMask=blend_mode==2?
            D3D12_DEPTH_WRITE_MASK_ZERO:D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
        desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        if(blend_mode==2) {
            desc.NumRenderTargets=1;
            desc.RTVFormats[0]=hdr_enabled?
                DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;
            auto& b=desc.BlendState.RenderTarget[0];
            b.BlendEnable=TRUE;
            b.SrcBlend=D3D12_BLEND_SRC_ALPHA;
            b.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
            b.BlendOp=D3D12_BLEND_OP_ADD;
            b.SrcBlendAlpha=D3D12_BLEND_ONE;
            b.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
            b.BlendOpAlpha=D3D12_BLEND_OP_ADD;
            b.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        }else {
            desc.NumRenderTargets=gbuffer_v2?4:3;
            desc.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.RTVFormats[1]=DXGI_FORMAT_R16G16B16A16_FLOAT;
            desc.RTVFormats[2]=DXGI_FORMAT_R16G16B16A16_FLOAT;
            desc.RTVFormats[3]=gbuffer_v2?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_UNKNOWN;
            for(UINT i=0;i<3;++i)
                desc.BlendState.RenderTarget[i].RenderTargetWriteMask=
                    D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        // Revision-bound DXIL PSOs must never use old cached shader bytecode.
        if(!vertex.empty() || !pixel.empty()) {
            ComPtr<ID3D12PipelineState> hot;
            check(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&hot)),
                  "Create D3D12 DXIL material hot pipeline");
            return hot;
        }
        // D3D12 cached pipelines are device/driver specific. Include the
        // adapter LUID, shader source, shader targets and pipeline mode.
        // The D3D runtime validates every cached PSO before accepting it.
        auto const adapter=device->GetAdapterLuid();
        std::string const diskKey="d3d12|material|ps_5_0|vs_5_0|"+
            std::to_string(adapter.HighPart)+":"+
            std::to_string(adapter.LowPart)+"|"+
            std::to_string(blend_mode)+"|"+
            (hdr_enabled?"HDR|":"LDR|")+source;
        auto cached=shader_disk_cache::read("d3d12",diskKey);
        ComPtr<ID3D12PipelineState> pipeline;
        HRESULT result=E_FAIL;
        if(cached && !cached->empty()) {
            desc.CachedPSO={cached->data(),cached->size()};
            result=device->CreateGraphicsPipelineState(
                &desc,IID_PPV_ARGS(&pipeline));
            if(FAILED(result)) {
                pipeline.Reset();
                desc.CachedPSO={};
                shader_disk_cache::erase("d3d12",diskKey);
            }
        }
        if(!pipeline) {
            check(device->CreateGraphicsPipelineState(&desc,
                IID_PPV_ARGS(&pipeline)),
                "Create D3D12 Material Graph variant PSO");
            ComPtr<ID3DBlob> blob;
            if(SUCCEEDED(pipeline->GetCachedBlob(&blob))&&blob &&
               blob->GetBufferSize()>0)
                shader_disk_cache::write("d3d12",diskKey,
                    blob->GetBufferPointer(),blob->GetBufferSize());
        }
        return pipeline;
}
ComPtr<ID3D12PipelineState> RendererState::material_graph_pipeline(
        std::string const& prefix,unsigned blend_mode) {
    // The prefix already contains validated graph defines and blend settings.
    // HDR is included in the key to avoid stale PSOs after mode changes.
    std::string const key=(hdr_enabled?"hdr:":"ldr:")+
                          std::to_string(blend_mode)+":"+prefix;
    if(auto it=material_shader_cache.find(key);
       it!=material_shader_cache.end())return it->second;
    if(material_pipeline_failed.count(key))return {};
    auto pending=material_pipeline_pending.find(key);
    if(pending!=material_pipeline_pending.end()) {
        if(pending->second.wait_for(std::chrono::milliseconds(0))!=
           std::future_status::ready)return {};
        try {
            auto pipeline=pending->second.get();
            if(material_shader_cache.size()>=128)material_shader_cache.clear();
            material_shader_cache.emplace(key,pipeline);
            material_pipeline_pending.erase(pending);
            return pipeline;
        }catch(std::exception const& error) {
            material_pipeline_pending.erase(pending);
            material_pipeline_failed.insert(key);
            std::fprintf(stderr,
                "[WARN] D3D12 Material Graph PSO variant failed: %s\n",
                error.what());
            return {};
        }
    }
    if(material_pipeline_pending.size()>=8)return {};
    auto context=device;
    auto signature=material_signature;
    bool const hdr=hdr_enabled;
    bool const v2=gbuffer_v2;
    material_pipeline_pending.emplace(key,std::async(std::launch::async,
        [context,signature,prefix,blend_mode,hdr,v2]() {
            return create_material_graph_pso(
                context,signature,prefix,blend_mode,hdr,v2);
        }));
    return {}; // Keep last valid/fallback pipeline during async compile.
}
void RendererState::resolve_material_graph_pipelines() {
    for(auto& draw:material_draws) {
        if(!draw.variant_pipeline&&!draw.variant_prefix.empty())
            draw.variant_pipeline=material_graph_pipeline(
                draw.variant_prefix,draw.blend_mode);
    }
}
void RendererState::set_material_batches_v2(NorthStarMaterialBatchV2 const* batches,UINT count){
        if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES||(count&&!batches))
            throw std::invalid_argument("Invalid D3D12 Material Graph batch count");
        std::vector<NorthStarMaterialBatch> base;
        frame_memory::reserve(base,count);
        for(UINT i=0;i<count;++i) {
            auto item=batches[i].material;
            float const opacity=batches[i].opacity;
            if(!std::isfinite(opacity)||opacity<0.f||opacity>1.f)
                throw std::invalid_argument("D3D12 material opacity must be [0,1]");
            item.base_color[3]*=opacity;
            base.push_back(item);
        }
        set_material_batches(base.data(),count);
        for(UINT i=0;i<count;++i) {
            auto const& item=batches[i];
            auto const prefix=northstarcpp::shaders::material_shader_prefix(
                item.graph_hlsl_defines,item.graph_hlsl_bytes,
                item.blend_mode,item.alpha_cutoff);
            auto& draw=material_draws[i];
            draw.blend_mode=item.blend_mode;
            if(item.graph_hlsl_bytes==0&&item.blend_mode==0)continue;
            draw.variant_prefix=prefix;
            draw.variant_pipeline=material_graph_pipeline(prefix,item.blend_mode);
        }
    }
} // namespace northstarcpp::render::d3d12::detail
