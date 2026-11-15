// D3D11 resources methods; independently compiled provider implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d11::detail {
void RendererState::resize(unsigned new_width, unsigned new_height){
        if (width == new_width && height == new_height)
            return;
        context->OMSetRenderTargets(0, nullptr, nullptr);
        render_target.reset();
        depth.reset();
        check(swap_chain->ResizeBuffers(0, new_width, new_height, DXGI_FORMAT_UNKNOWN, 0),
            "Resize viewport swap chain");
        Com<ID3D11Texture2D> back_buffer;
        check(swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(back_buffer.put())), "Get viewport buffer");
        check(device->CreateRenderTargetView(back_buffer.get(), nullptr, render_target.put()),
            "Create viewport render target");
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = new_width;
        texture.Height = new_height;
        texture.MipLevels = 1;
        texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        texture.SampleDesc.Count = 1;
        texture.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        Com<ID3D11Texture2D> depth_buffer;
        check(device->CreateTexture2D(&texture, nullptr, depth_buffer.put()), "Create depth buffer");
        check(device->CreateDepthStencilView(depth_buffer.get(), nullptr, depth.put()),
            "Create depth view");
        width = new_width;
        height = new_height;
    }

void RendererState::verify_frame(std::uint32_t mode){
        Com<ID3D11Texture2D> back_buffer;
        check(swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(back_buffer.put())), "Read viewport buffer");
        D3D11_TEXTURE2D_DESC desc{};
        back_buffer->GetDesc(&desc);
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Com<ID3D11Texture2D> copy;
        check(device->CreateTexture2D(&desc, nullptr, copy.put()), "Create viewport readback");
        context->CopyResource(copy.get(), back_buffer.get());
        D3D11_MAPPED_SUBRESOURCE data{};
        check(context->Map(copy.get(), 0, D3D11_MAP_READ, 0, &data), "Map viewport readback");
        unsigned colored = 0;
        unsigned material_red=0,material_green=0;
        unsigned sky_samples = 0, sky_pixels = 0;
        std::unordered_set<std::uint32_t> world_colors;
        bool const verify_surfaces=sky_index_count==0 &&
            std::any_of(surface_ids.begin(),surface_ids.end(),
                        [](auto id){return id>0;});
        for (unsigned y = 0; y < height; ++y) {
            auto const* row = static_cast<unsigned char const*>(data.pData) + y * data.RowPitch;
            for (unsigned x = 0; x < width; ++x) {
                auto const* pixel = row + x * 4;
                if (pixel[0] > 110 || pixel[1] > 110 || pixel[2] > 110)
                    ++colored;
                if((mode==2u||mode==3u) && !(x%2) && !(y%2)) {
                    // Swapchain is R8G8B8A8_UNORM, *not* BGRA.
                    // Verify two independent UV texture colors were sampled.
                    if(pixel[0]>130 && pixel[1]<90 && pixel[2]<90)
                        ++material_red;
                    if(pixel[1]>130 && pixel[0]<90 && pixel[2]<90)
                        ++material_green;
                }
                // The untextured geometry has only a few flat face colors.
                // Real world texture sampling produces dozens of distinct
                // albedo variations even at a 4-pixel readback stride.
                if(verify_surfaces && !(x%4) && !(y%4) &&
                   (pixel[0]>40 || pixel[1]>40 || pixel[2]>40))
                    world_colors.insert((unsigned(pixel[0])>>3) |
                        ((unsigned(pixel[1])>>3)<<5) |
                        ((unsigned(pixel[2])>>3)<<10));
                // Validate sky coverage away from health/ammo HUD, and avoid
                // passing merely because world cubes or reticle are colored.
                if (sky_index_count && y < height / 5 &&
                    x > width / 3 && x < width * 2 / 3) {
                    ++sky_samples;
                    if (pixel[0] > 62 || pixel[1] > 62 || pixel[2] > 62)
                        ++sky_pixels;
                }
            }
        }
        if(verify_surfaces) {
            if(auto* capture=std::getenv("NORTHSTARCPP_WORLD_SURFACE_QA_PPM")) {
                if(auto* out=std::fopen(capture,"wb")) {
                    std::fprintf(out,"P6\n%u %u\n255\n",width,height);
                    for(unsigned y=0;y<height;++y) {
                        auto const* row=static_cast<unsigned char const*>(data.pData)+
                            y*data.RowPitch;
                        for(unsigned x=0;x<width;++x) {
                            auto const* bgra=row+x*4;
                            unsigned char const rgb[3]{bgra[2],bgra[1],bgra[0]};
                            std::fwrite(rgb,1,3,out);
                        }
                    }
                    std::fclose(out);
                }
            }
        }
        context->Unmap(copy.get(), 0);
        if(mode==2u &&
           ((material_draws.empty()&&skinned_draws.empty())||
            material_red<100||material_green<100))
            throw std::runtime_error(
                "Authored/Skinned material GPU readback: red="+
                std::to_string(material_red)+" green="+
                std::to_string(material_green)+" static="+
                std::to_string(material_draws.size())+" skinned="+
                std::to_string(skinned_draws.size()));
        if(mode==3u &&
           (material_draws.empty()||material_green<100||material_red>25))
            throw std::runtime_error(
                "Generated Material Graph GPU color mismatch: green="+
                std::to_string(material_green)+" red="+
                std::to_string(material_red)+" draws="+
                std::to_string(material_draws.size()));
        if (!verify_surfaces && colored < 100)
            throw std::runtime_error("Viewport readback did not contain a rendered object");
        if(verify_surfaces && world_colors.size()<32)
            throw std::runtime_error(
                "World surface GPU readback lacks varied texture coverage: "+
                std::to_string(world_colors.size())+
                " quantized colors, "+std::to_string(colored)+
                " bright pixels, "+std::to_string(surface_ids.size())+
                " surface selectors");
        if (sky_index_count && (!sky_samples || sky_pixels * 10 < sky_samples))
            throw std::runtime_error("Skydome GPU readback lacks textured sky coverage");
    }

}
