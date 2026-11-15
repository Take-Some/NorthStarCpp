// D3D11 surfaces methods; independently compiled provider implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d11::detail {
void RendererState::set_triangles(NorthStarRenderTriangleVertex const* src,std::uint32_t count){
        constexpr std::size_t limit=300000;
        if (count>limit || count%3!=0 || (count && !src))
            throw std::invalid_argument("Invalid static mesh triangle buffer");
        std::vector<Vertex> submitted;
        submitted.reserve(count);
        for (std::uint32_t i=0;i<count;++i) {
            auto const& v=src[i];
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
                std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)})>1000000.f)
                throw std::invalid_argument("Mesh vertex position out of range");
            auto const rgb=v.rgb;
            submitted.push_back({v.x,v.y,v.z,
                float((rgb>>16)&255u)/255.f,
                float((rgb>>8)&255u)/255.f,
                float(rgb&255u)/255.f});
        }
        mesh_triangles.swap(submitted);
    }

void RendererState::set_surface_texture(NorthStarSurfaceTexture const& image){
        if(image.slot>=9 || (image.format_id!=102 && image.format_id!=105) ||
           !image.data || !image.mips || image.mip_count==0 ||
           image.mip_count>15 || image.data_bytes>32u*1024u*1024u)
            throw std::invalid_argument("Invalid world surface texture descriptor");
        DXGI_FORMAT const format=image.format_id==102 ?
            (image.srgb?DXGI_FORMAT_BC1_UNORM_SRGB:DXGI_FORMAT_BC1_UNORM):
            DXGI_FORMAT_BC5_UNORM;
        D3D11_TEXTURE2D_DESC info{};
        info.Width=image.mips[0].width;
        info.Height=image.mips[0].height;
        info.MipLevels=image.mip_count;
        info.ArraySize=1;
        info.Format=format;
        info.SampleDesc.Count=1;
        info.Usage=D3D11_USAGE_IMMUTABLE;
        info.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::vector<D3D11_SUBRESOURCE_DATA> levels;
        levels.reserve(image.mip_count);
        unsigned width=info.Width,height=info.Height;
        for(std::uint32_t i=0;i<image.mip_count;++i) {
            auto const& mip=image.mips[i];
            auto const row=((width+3)/4)*(image.format_id==102?8:16);
            auto const size=row*((height+3)/4);
            if(!width || !height || width>16384 || height>16384 ||
               mip.width!=width || mip.height!=height ||
               mip.bytes!=size || mip.offset>image.data_bytes ||
               mip.bytes>image.data_bytes-mip.offset)
                throw std::invalid_argument("Invalid world surface mip layout");
            levels.push_back({image.data+mip.offset,row,size});
            width=std::max(1u,width/2);
            height=std::max(1u,height/2);
        }
        Com<ID3D11Texture2D> texture;
        check(device->CreateTexture2D(&info,levels.data(),texture.put()),
              "Create world surface BCn texture");
        Com<ID3D11ShaderResourceView> srv;
        check(device->CreateShaderResourceView(texture.get(),nullptr,srv.put()),
              "Create world surface shader resource");
        surface_textures[image.slot].swap(texture);
        surface_views[image.slot].swap(srv);
    }
void RendererState::set_cube_surfaces(std::uint32_t const* ids,std::uint32_t count){
        if(count>northstarcpp::max_scene_objects || (count && !ids))
            throw std::invalid_argument("Invalid world surface cube selectors");
        if(count)surface_ids.assign(ids,ids+count);
        else surface_ids.clear();
        if(std::any_of(surface_ids.begin(),surface_ids.end(),
                       [](std::uint32_t id){return id>3;}))
            throw std::invalid_argument("World surface ID exceeds available materials");
    }

}
