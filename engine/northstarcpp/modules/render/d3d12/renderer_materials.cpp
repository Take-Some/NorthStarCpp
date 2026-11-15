// Materials subsystem: independent D3D12 implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
std::shared_ptr<MaterialGpuTexture> RendererState::material_texture(
        NorthStarMaterialTexture const& image,bool is_srgb,
        std::vector<MaterialTextureUpload>& pending){
        if(!image.rgba8 || !image.width || !image.height ||
           image.width>4096 || image.height>4096 ||
           std::uint64_t(image.width)*image.height*4!=image.data_bytes)
            throw std::invalid_argument("Invalid D3D12 authored material texture");
        std::uint64_t hash=14695981039346656037ull;
        auto feed=[&](std::uint8_t byte){hash=(hash^byte)*1099511628211ull;};
        for(unsigned b=0;b<4;++b) {
            feed(std::uint8_t(image.width>>(b*8)));
            feed(std::uint8_t(image.height>>(b*8)));
        }
        feed(is_srgb?1:0);
        for(std::uint32_t i=0;i<image.data_bytes;++i)feed(image.rgba8[i]);
        if(auto it=material_texture_cache.find(hash);it!=material_texture_cache.end())
            return it->second;
        if(material_cache_bytes+image.data_bytes>96u*1024u*1024u) {
            material_texture_cache.clear();
            material_cache_bytes=0;
        }
        auto texture=std::make_shared<MaterialGpuTexture>();
        texture->format=is_srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                                 DXGI_FORMAT_R8G8B8A8_UNORM;
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
        auto staging_desc=memory(UINT64(pitch)*image.height);
        auto upload=heap(D3D12_HEAP_TYPE_UPLOAD);
        MaterialTextureUpload item{};
        item.texture=texture;item.width=image.width;item.height=image.height;
        item.pitch=pitch;
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,
            &staging_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
            IID_PPV_ARGS(&item.staging)),"Create material texture staging");
        unsigned char* data=nullptr;
        check(item.staging->Map(0,nullptr,reinterpret_cast<void**>(&data)),
            "Map material texture staging");
        for(UINT y=0;y<image.height;++y)
            std::memcpy(data+UINT64(y)*pitch,
                        image.rgba8+UINT64(y)*image.width*4,
                        UINT64(image.width)*4);
        item.staging->Unmap(0,nullptr);
        pending.push_back(std::move(item));
        material_texture_cache.emplace(hash,texture);
        material_cache_bytes+=image.data_bytes;
        return texture;
    }
void RendererState::set_material_batches(NorthStarMaterialBatch const* batches,UINT count){
        if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES||(count&&!batches))
            throw std::invalid_argument("Invalid D3D12 material batch count");
        if(!count) {
            material_draws.clear();
            material_cpu_vertices.clear();
            return;
        }
        // Descriptors and persistently mapped GPU uploads cannot be modified
        // before the prior submitted frame has finished consuming them.
        wait();
        ensure_material_buffers();
        std::vector<NorthStarMaterialVertex> vertices;
        std::vector<MaterialDraw> draws;
        std::vector<MaterialTextureUpload> pending;
        draws.reserve(count);
        for(UINT i=0;i<count;++i) {
            auto const& input=batches[i];
            if(!input.vertices || !input.vertex_count ||
               input.vertex_count%3 ||
               input.vertex_count>NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES-
                                  vertices.size())
                throw std::invalid_argument("Invalid material draw vertices");
            auto finite_unit=[](float f){
                return std::isfinite(f)&&f>=0.f&&f<=1.f;
            };
            for(float color:input.base_color)
                if(!finite_unit(color))
                    throw std::invalid_argument("Invalid material base color");
            if(!finite_unit(input.roughness)||!finite_unit(input.metallic))
                throw std::invalid_argument("Invalid material PBR constants");
            MaterialDraw draw{};
            draw.first=static_cast<UINT>(vertices.size());
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
                draw.maps[slot]=material_texture(texture,slot==0||slot==4,pending);
                draw.params.present[0]|=1u<<slot;
            }
            for(UINT v=0;v<input.vertex_count;++v) {
                auto const& point=input.vertices[v];
                float const components[]{
                    point.x,point.y,point.z,point.nx,point.ny,point.nz,point.u,point.v};
                for(float f:components)
                    if(!std::isfinite(f)||std::abs(f)>1.e6f)
                        throw std::invalid_argument("Nonfinite material mesh vertex");
            }
            vertices.insert(vertices.end(),input.vertices,
                            input.vertices+input.vertex_count);
            draws.push_back(std::move(draw));
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
                source.pResource=image.staging.Get();
                source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
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
            queue->ExecuteCommandLists(1,work);
            wait(); // Upload staging may now be released safely.
        }
        auto heap_start=material_srv_heap->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<draws.size();++i) {
            auto const& draw=draws[i];
            std::memcpy(material_constant_data+UINT64(i)*256,
                        &draw.params,sizeof(MaterialParams));
            for(unsigned slot=0;slot<5;++slot) {
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
        material_cpu_vertices.swap(vertices);
        material_draws.swap(draws);
    }
ComPtr<ID3D12PipelineState> RendererState::material_graph_pipeline(
        std::string const& prefix,unsigned blend_mode){
        if(auto it=material_shader_cache.find(prefix);
           it!=material_shader_cache.end())return it->second;
        if(material_shader_cache.size()>=128)material_shader_cache.clear();
        std::string const source=(hdr_enabled&&blend_mode==2?
            "#define NS_HDR_OUTPUT 1\n":"")+prefix+authored_material_shader;
        auto vs=compile_material_variant(source,"material_vs","vs_5_0");
        auto ps=compile_material_variant(source,blend_mode==2?
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
        desc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
        desc.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
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
            desc.NumRenderTargets=3;
            desc.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.RTVFormats[1]=DXGI_FORMAT_R16G16B16A16_FLOAT;
            desc.RTVFormats[2]=DXGI_FORMAT_R16G16B16A16_FLOAT;
            for(UINT i=0;i<3;++i)
                desc.BlendState.RenderTarget[i].RenderTargetWriteMask=
                    D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        ComPtr<ID3D12PipelineState> pipeline;
        check(device->CreateGraphicsPipelineState(&desc,
            IID_PPV_ARGS(&pipeline)),"Create D3D12 Material Graph variant PSO");
        material_shader_cache.emplace(prefix,pipeline);
        return pipeline;
    }
void RendererState::set_material_batches_v2(NorthStarMaterialBatchV2 const* batches,UINT count){
        if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES||(count&&!batches))
            throw std::invalid_argument("Invalid D3D12 Material Graph batch count");
        std::vector<NorthStarMaterialBatch> base;
        base.reserve(count);
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
            draw.variant_pipeline=material_graph_pipeline(prefix,item.blend_mode);
        }
    }
} // namespace northstarcpp::render::d3d12::detail
