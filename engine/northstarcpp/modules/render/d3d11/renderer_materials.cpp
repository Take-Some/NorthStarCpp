// D3D11 materials methods; independently compiled provider implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d11::detail {
std::shared_ptr<CachedTexture> RendererState::upload_material_texture(
        NorthStarMaterialTexture const& image,bool is_srgb){
        if(image.width==0 || image.height==0 ||
           image.width>4096 || image.height>4096 ||
           !image.rgba8 || std::uint64_t(image.width)*image.height*4!=image.data_bytes)
            throw std::invalid_argument("Invalid material texture RGBA8 payload");
        std::uint64_t hash=14695981039346656037ull;
        auto feed=[&](std::uint8_t byte){hash=(hash^byte)*1099511628211ull;};
        for(unsigned i=0;i<4;++i) {
            feed(std::uint8_t(image.width>>(i*8)));
            feed(std::uint8_t(image.height>>(i*8)));
        }
        feed(is_srgb?1:0);
        for(std::uint32_t i=0;i<image.data_bytes;++i)feed(image.rgba8[i]);
        if(auto it=material_texture_cache.find(hash);it!=material_texture_cache.end())
            return it->second;
        if(material_texture_cache_bytes + image.data_bytes > 96u*1024u*1024u) {
            material_texture_cache.clear();
            material_texture_cache_bytes=0;
        }
        auto result=std::make_shared<CachedTexture>();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=image.width;desc.Height=image.height;desc.MipLevels=1;
        desc.ArraySize=1;desc.SampleDesc.Count=1;
        desc.Format=is_srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                            DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage=D3D11_USAGE_IMMUTABLE;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA src{image.rgba8,image.width*4,0};
        check(device->CreateTexture2D(&desc,&src,result->gpu.put()),
              "Upload authored material texture");
        check(device->CreateShaderResourceView(result->gpu.get(),nullptr,
            result->view.put()),"Create authored material SRV");
        material_texture_cache.emplace(hash,result);
        material_texture_cache_bytes+=image.data_bytes;
        return result;
    }
void RendererState::set_material_batches(NorthStarMaterialBatch const* batches,std::uint32_t count){
        if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES || (count&&!batches))
            throw std::invalid_argument("Invalid material batches");
        std::vector<NorthStarMaterialVertex> submitted;
        std::vector<MaterialDraw> draws;
        draws.reserve(count);
        for(std::uint32_t index=0;index<count;++index) {
            auto const& input=batches[index];
            if(!input.vertex_count ||
               input.vertex_count%3 || !input.vertices ||
               input.vertex_count>NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES-
                                  submitted.size())
                throw std::invalid_argument("Invalid material geometry count");
            auto within=[](float v){return std::isfinite(v)&&v>=0.f&&v<=1.f;};
            for(float c:input.base_color)if(!within(c))
                throw std::invalid_argument("Invalid material Base Color");
            if(!within(input.roughness)||!within(input.metallic))
                throw std::invalid_argument("Invalid material PBR factors");
            MaterialDraw draw{};
            draw.first=static_cast<std::uint32_t>(submitted.size());
            draw.count=input.vertex_count;
            std::copy(std::begin(input.base_color),std::end(input.base_color),
                      std::begin(draw.params.base_color));
            draw.params.factors[0]=input.roughness;
            draw.params.factors[1]=input.metallic;
            draw.params.factors[2]=input.unlit?1.f:0.f;
            for(unsigned slot=0;slot<5;++slot) {
                auto const& tex=input.textures[slot];
                if(!tex.rgba8 && !tex.width && !tex.height && !tex.data_bytes)
                    continue;
                draw.maps[slot]=upload_material_texture(tex,slot==0 || slot==4);
                draw.params.present[0]|=1u<<slot;
            }
            for(std::uint32_t v=0;v<input.vertex_count;++v) {
                auto const& point=input.vertices[v];
                float const comps[]{point.x,point.y,point.z,point.nx,point.ny,
                                    point.nz,point.u,point.v};
                for(float component:comps)
                    if(!std::isfinite(component)||std::abs(component)>1.e6f)
                        throw std::invalid_argument("Invalid UV material vertex");
            }
            submitted.insert(submitted.end(),input.vertices,
                             input.vertices+input.vertex_count);
            draws.push_back(std::move(draw));
        }
        material_cpu_vertices.swap(submitted);
        material_draws.swap(draws);
    }
void RendererState::set_material_batches_v2(NorthStarMaterialBatchV2 const* batches,
                                 std::uint32_t count){
        if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES || (count&&!batches))
            throw std::invalid_argument("Invalid Material Graph batch count");
        std::vector<NorthStarMaterialBatch> base;
        base.reserve(count);
        for(std::uint32_t i=0;i<count;++i) {
            auto src=batches[i].material;
            if(!std::isfinite(batches[i].opacity)||
               batches[i].opacity<0.f||batches[i].opacity>1.f)
                throw std::invalid_argument("Invalid material instance opacity");
            src.base_color[3]*=batches[i].opacity;
            base.push_back(src);
        }
        set_material_batches(base.data(),count);
        for(std::uint32_t i=0;i<count;++i) {
            auto const& input=batches[i];
            auto& draw=material_draws[i];
            draw.blend_mode=input.blend_mode;
            auto const prefix=northstarcpp::shaders::material_shader_prefix(
                input.graph_hlsl_defines,input.graph_hlsl_bytes,
                input.blend_mode,input.alpha_cutoff);
            if(input.graph_hlsl_bytes==0 && input.blend_mode==0)continue;
            auto found=material_shader_cache.find(prefix);
            if(found==material_shader_cache.end()) {
                if(material_shader_cache.size()>=128)material_shader_cache.clear();
                auto object=std::make_shared<MaterialPixelVariant>();
                std::string const source=prefix+authored_material_shader;
                Com<ID3DBlob> compiled,errors;
                HRESULT const status=D3DCompile(source.data(),source.size(),
                    "NorthStar Material Graph",nullptr,nullptr,
                    "material_ps","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,
                    compiled.put(),errors.put());
                if(FAILED(status)&&errors.get())
                    throw std::runtime_error(
                        static_cast<char const*>(errors->GetBufferPointer()));
                check(status,"Compile Material Graph PS");
                check(device->CreatePixelShader(compiled->GetBufferPointer(),
                    compiled->GetBufferSize(),nullptr,object->shader.put()),
                    "Create Material Graph GPU program");
                found=material_shader_cache.emplace(prefix,std::move(object)).first;
            }
            draw.shader_variant=found->second;
        }
    }
void RendererState::set_skinned_material_batches(
        NorthStarSkinnedMaterialBatch const* batches,std::uint32_t count){
        if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES || (count&&!batches))
            throw std::invalid_argument("Invalid GPU skinned material batch count");
        std::vector<NorthStarSkinMaterialVertex> submitted;
        std::vector<SkinnedMaterialDraw> draws;
        draws.reserve(count);
        for(std::uint32_t index=0;index<count;++index) {
            auto const& entry=batches[index];
            auto const& settings=entry.material;
            auto const& source=settings.material;
            if(!entry.vertices || !entry.palette || entry.joint_count==0 ||
               entry.joint_count>NORTHSTARCPP_SKIN_MAX_JOINTS ||
               !entry.vertex_count || entry.vertex_count%3 ||
               entry.vertex_count>
                   NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES-submitted.size())
                throw std::invalid_argument("Invalid native GPU skinning topology");
            // Graphs/transparent materials can use the proven CPU fallback
            // until the versioned GPU skin API supports those variants.
            if(settings.blend_mode!=0 || settings.graph_hlsl_bytes!=0 ||
               settings.opacity!=1.f)
                throw std::invalid_argument("GPU skin v1 supports opaque PBR only");
            auto within=[](float v){return std::isfinite(v)&&v>=0.f&&v<=1.f;};
            for(float value:source.base_color)
                if(!within(value))
                    throw std::invalid_argument("Invalid GPU skin material color");
            if(!within(source.roughness)||!within(source.metallic))
                throw std::invalid_argument("Invalid GPU skin material factors");
            SkinnedMaterialDraw draw;
            auto& base=draw.material;
            base.first=static_cast<std::uint32_t>(submitted.size());
            base.count=entry.vertex_count;
            std::copy(std::begin(source.base_color),std::end(source.base_color),
                      std::begin(base.params.base_color));
            base.params.factors[0]=source.roughness;
            base.params.factors[1]=source.metallic;
            base.params.factors[2]=source.unlit?1.f:0.f;
            for(unsigned slot=0;slot<5;++slot) {
                auto const& tex=source.textures[slot];
                if(!tex.rgba8 && !tex.width && !tex.height && !tex.data_bytes)
                    continue;
                base.maps[slot]=upload_material_texture(tex,slot==0 || slot==4);
                base.params.present[0]|=1u<<slot;
            }
            draw.palette.assign(entry.palette,
                entry.palette+std::size_t(entry.joint_count)*16u);
            for(auto value:draw.palette)
                if(!std::isfinite(value)||std::abs(value)>1e6f)
                    throw std::invalid_argument("Invalid native GPU skin palette");
            for(std::uint32_t vertex=0;vertex<entry.vertex_count;++vertex) {
                auto const& v=entry.vertices[vertex];
                for(float f:{v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v})
                    if(!std::isfinite(f)||std::abs(f)>1e6f)
                        throw std::invalid_argument("Invalid GPU skin vertex");
                float sum=0.f;
                for(unsigned influence=0;influence<8;++influence) {
                    auto const w=v.weights[influence];
                    if(!within(w) || (w>0.f &&
                       v.joints[influence]>=entry.joint_count))
                        throw std::invalid_argument("Invalid native GPU bone influence");
                    sum+=w;
                }
                if(std::abs(sum-1.f)>0.01f)
                    throw std::invalid_argument("Native GPU skin weights must sum to one");
            }
            submitted.insert(submitted.end(),entry.vertices,
                             entry.vertices+entry.vertex_count);
            draws.push_back(std::move(draw));
        }
        skinned_vertices.swap(submitted);
        skinned_draws.swap(draws);
    }

void RendererState::draw_skinned_materials(){
        if(skinned_draws.empty())return;
        if(skinned_vertices.size()>skinned_vertex_capacity) {
            auto const next=std::max<std::size_t>(4096,
                (skinned_vertices.size()+1023u)&~std::size_t(1023));
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth=static_cast<UINT>(
                next*sizeof(NorthStarSkinMaterialVertex));
            desc.Usage=D3D11_USAGE_DYNAMIC;
            desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            check(device->CreateBuffer(&desc,nullptr,skinned_vertex_buffer.put()),
                "Allocate native GPU skin vertices");
            skinned_vertex_capacity=next;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(skinned_vertex_buffer.get(),0,
            D3D11_MAP_WRITE_DISCARD,0,&mapped),"Upload original YDD bone indices");
        std::memcpy(mapped.pData,skinned_vertices.data(),
            skinned_vertices.size()*sizeof(NorthStarSkinMaterialVertex));
        context->Unmap(skinned_vertex_buffer.get(),0);
        auto* gpu=skinned_vertex_buffer.get();
        UINT const stride=sizeof(NorthStarSkinMaterialVertex),offset=0;
        context->IASetInputLayout(skinned_layout.get());
        context->IASetVertexBuffers(0,1,&gpu,&stride,&offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(skinned_vs.get(),nullptr,0);
        auto* camera_buffer=authored_camera.get();
        context->VSSetConstantBuffers(0,1,&camera_buffer);
        context->PSSetConstantBuffers(0,1,&camera_buffer);
        auto* palette_buffer=skinned_palette_buffer.get();
        context->VSSetConstantBuffers(2,1,&palette_buffer);
        auto* material_buffer=authored_material.get();
        context->PSSetConstantBuffers(1,1,&material_buffer);
        auto* sampler=sky_sampler.get();
        context->PSSetSamplers(0,1,&sampler);
        context->OMSetDepthStencilState(nullptr,0);
        context->OMSetBlendState(nullptr,nullptr,0xffffffffu);
        for(auto const& draw:skinned_draws) {
            std::array<float,NORTHSTARCPP_SKIN_MAX_JOINTS*16u> palette{};
            std::copy(draw.palette.begin(),draw.palette.end(),palette.begin());
            context->UpdateSubresource(palette_buffer,0,nullptr,
                palette.data(),0,0);
            auto const& material=draw.material;
            context->PSSetShader(authored_ps.get(),nullptr,0);
            ID3D11ShaderResourceView* images[5]{};
            for(unsigned slot=0;slot<5;++slot)
                images[slot]=material.maps[slot]?
                    material.maps[slot]->view.get():nullptr;
            context->PSSetShaderResources(0,5,images);
            context->UpdateSubresource(material_buffer,0,nullptr,
                &material.params,0,0);
            context->Draw(material.count,material.first);
        }
        ID3D11ShaderResourceView* empty[5]{};
        context->PSSetShaderResources(0,5,empty);
        ID3D11Buffer* null_bones=nullptr;
        context->VSSetConstantBuffers(2,1,&null_bones);
    }

void RendererState::draw_authored_materials(){
        if(material_draws.empty())return;
        if(material_cpu_vertices.size()>material_gpu_capacity) {
            auto new_capacity=std::max<std::size_t>(4096,
                (material_cpu_vertices.size()+1023u)&~std::size_t(1023));
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth=static_cast<UINT>(new_capacity*sizeof(NorthStarMaterialVertex));
            desc.Usage=D3D11_USAGE_DYNAMIC;
            desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            check(device->CreateBuffer(&desc,nullptr,material_gpu_vertices.put()),
                "Create authored material GPU vertex buffer");
            material_gpu_capacity=new_capacity;
        }
        D3D11_MAPPED_SUBRESOURCE map{};
        check(context->Map(material_gpu_vertices.get(),0,
            D3D11_MAP_WRITE_DISCARD,0,&map),"Map authored material mesh");
        std::memcpy(map.pData,material_cpu_vertices.data(),
            material_cpu_vertices.size()*sizeof(NorthStarMaterialVertex));
        context->Unmap(material_gpu_vertices.get(),0);
        auto* gpu=material_gpu_vertices.get();
        UINT const stride=sizeof(NorthStarMaterialVertex),offset=0;
        context->IASetInputLayout(authored_layout.get());
        context->IASetVertexBuffers(0,1,&gpu,&stride,&offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(authored_vs.get(),nullptr,0);
        context->PSSetShader(authored_ps.get(),nullptr,0);
        auto* constants=authored_material.get();
        auto* eye_constants=authored_camera.get();
        context->VSSetConstantBuffers(0,1,&eye_constants);
        context->PSSetConstantBuffers(0,1,&eye_constants);
        context->PSSetConstantBuffers(1,1,&constants);
        auto* sampler=sky_sampler.get();
        context->PSSetSamplers(0,1,&sampler);
        for(unsigned pass=0;pass<2;++pass)
        for(auto const& draw:material_draws) {
            if((draw.blend_mode==2)!=(pass==1))continue;
            if(draw.blend_mode==2) {
                float const factor[4]{0,0,0,0};
                context->OMSetBlendState(material_alpha_blend.get(),factor,0xffffffffu);
                context->OMSetDepthStencilState(material_transparent_depth.get(),0);
            }else {
                context->OMSetBlendState(nullptr,nullptr,0xffffffffu);
                context->OMSetDepthStencilState(nullptr,0);
            }
            context->PSSetShader(draw.shader_variant?
                draw.shader_variant->shader.get():authored_ps.get(),nullptr,0);
            ID3D11ShaderResourceView* views[5]{};
            for(unsigned i=0;i<5;++i)
                views[i]=draw.maps[i]?draw.maps[i]->view.get():nullptr;
            context->PSSetShaderResources(0,5,views);
            context->UpdateSubresource(constants,0,nullptr,&draw.params,0,0);
            context->Draw(draw.count,draw.first);
        }
        context->OMSetBlendState(nullptr,nullptr,0xffffffffu);
        context->OMSetDepthStencilState(nullptr,0);
        // Prevent stale shader resource bindings from aliasing future
        // provider-owned upload resources.
        ID3D11ShaderResourceView* null_views[5]{};
        context->PSSetShaderResources(0,5,null_views);
    }
}
