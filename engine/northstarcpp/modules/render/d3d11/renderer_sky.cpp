// D3D11 sky methods; independently compiled provider implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d11::detail {
void RendererState::set_sky(NorthStarSky const* source){
        // The render provider owns its D3D resources after this call; no
        // retained pointers to host or AssetManager memory are allowed.
        sky_index_count = 0;
        sky_gpu_bytes=0;
        sky_vertex_buffer.reset();
        sky_index_buffer.reset();
        sky_texture.reset();
        sky_view.reset();
        if (!source) return;
        if (!source->vertices || !source->indices || !source->rgba8 ||
            source->vertex_count < 3 || source->vertex_count > 65536 ||
            source->index_count < 3 || source->index_count > 1000000 ||
            source->index_count % 3 != 0 || source->width == 0 || source->height == 0 ||
            source->width > 8192 || source->height > 8192)
            throw std::runtime_error("Invalid skydome geometry or dimensions");
        for (unsigned i=0; i<source->index_count; ++i)
            if (source->indices[i] >= source->vertex_count)
                throw std::runtime_error("Skydome index exceeds vertex count");
        for (unsigned i=0; i<source->vertex_count; ++i) {
            auto const& v=source->vertices[i];
            if (!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||
                !std::isfinite(v.u)||!std::isfinite(v.v))
                throw std::runtime_error("Skydome vertex contains nonfinite coordinates");
        }
        float const colors[]{source->tint_r,source->tint_g,source->tint_b,1.f};
        for (float component: colors)
            if (!std::isfinite(component) || component < 0.f || component > 4.f)
                throw std::runtime_error("Invalid skydome tint");
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=source->vertex_count * sizeof(SkyVertex);
        desc.Usage=D3D11_USAGE_IMMUTABLE;
        desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertices_data{source->vertices,0,0};
        check(device->CreateBuffer(&desc,&vertices_data,sky_vertex_buffer.put()),
            "Upload skydome YDD vertices");
        desc.ByteWidth=source->index_count * sizeof(uint32_t);
        desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA indices_data{source->indices,0,0};
        check(device->CreateBuffer(&desc,&indices_data,sky_index_buffer.put()),
            "Upload skydome YDD indices");
        D3D11_TEXTURE2D_DESC tex{};
        tex.Width=source->width;
        tex.Height=source->height;
        tex.MipLevels=1;
        tex.ArraySize=1;
        tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        tex.SampleDesc.Count=1;
        tex.Usage=D3D11_USAGE_IMMUTABLE;
        tex.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA tex_data{source->rgba8,source->width*4u,0};
        check(device->CreateTexture2D(&tex,&tex_data,sky_texture.put()),
            "Upload native YTD skydome RGBA8");
        check(device->CreateShaderResourceView(sky_texture.get(),nullptr,sky_view.put()),
            "Create skydome texture view");
        context->UpdateSubresource(sky_tint.get(),0,nullptr,colors,0,0);
        sky_index_count=source->index_count;
        sky_gpu_bytes=static_cast<std::uint64_t>(source->vertex_count)*sizeof(SkyVertex)+
            static_cast<std::uint64_t>(source->index_count)*sizeof(std::uint32_t)+
            static_cast<std::uint64_t>(source->width)*source->height*4u;
    }

NorthStarRenderMemoryStats RendererState::memory_stats() const{
        NorthStarRenderMemoryStats stats{};
        stats.vertex_buffer_bytes=
            static_cast<std::uint64_t>(vertex_buffer_capacity)*sizeof(Vertex);
        stats.sky_geometry_and_texture_bytes=sky_gpu_bytes;
        stats.vertex_buffer_reallocations=vertex_buffer_reallocations;
        stats.world_vertex_capacity=vertex_buffer_capacity;
        return stats;
    }

void RendererState::draw_sky(northstarcpp::math::float3 const& direction,
                  northstarcpp::math::float3 const& up,
                  northstarcpp::math::float4x4 const& projection){
        if (!sky_index_count) return;
        using namespace northstarcpp::math;
        // Rotation only: translation is removed from the sky view so that the
        // authored dome always follows the camera without parallax.
        auto const view=create_camera_at(float3(0.f,0.f,0.f),direction,up);
        auto const vp=mul4x4(view,projection);
        context->UpdateSubresource(camera.get(),0,nullptr,&vp,0,0);
        UINT const stride=sizeof(SkyVertex), offset=0;
        auto* buffer=sky_vertex_buffer.get();
        auto* index=sky_index_buffer.get();
        auto* vertex=sky_vertex_shader.get();
        auto* pixel=sky_pixel_shader.get();
        auto* texture=sky_view.get();
        auto* sampler=sky_sampler.get();
        auto* constants=sky_tint.get();
        context->IASetInputLayout(sky_layout.get());
        context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        context->IASetIndexBuffer(index,DXGI_FORMAT_R32_UINT,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex,nullptr,0);
        auto* camera_buffer=camera.get();
        context->VSSetConstantBuffers(0,1,&camera_buffer);
        context->PSSetShader(pixel,nullptr,0);
        context->PSSetConstantBuffers(1,1,&constants);
        context->PSSetShaderResources(0,1,&texture);
        context->PSSetSamplers(0,1,&sampler);
        context->OMSetDepthStencilState(sky_no_depth.get(),0);
        context->DrawIndexed(sky_index_count,0,0);
        ID3D11ShaderResourceView* empty=nullptr;
        context->PSSetShaderResources(0,1,&empty);
        context->OMSetDepthStencilState(nullptr,0);
    }

}
