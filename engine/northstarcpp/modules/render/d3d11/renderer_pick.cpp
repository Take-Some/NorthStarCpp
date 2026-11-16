// On-demand hardware ID pass for D3D11 cube primitives.
// Separate RGBA8/DSV attachments preserve the visible scene backbuffer.
// No OS-level readback of the swapchain or color heuristics from lit pixels.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d11::detail {
int RendererState::pick_cube(std::uint32_t x,std::uint32_t y) {
    if(x>=width || y>=height || !device.get() || !context.get() ||
       pick_cubes.empty())
        return -1;
    D3D11_TEXTURE2D_DESC descriptor{};
    descriptor.Width=width;
    descriptor.Height=height;
    descriptor.MipLevels=1;
    descriptor.ArraySize=1;
    descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.SampleDesc.Count=1;
    descriptor.Usage=D3D11_USAGE_DEFAULT;
    descriptor.BindFlags=D3D11_BIND_RENDER_TARGET;
    Com<ID3D11Texture2D> id_texture;
    check(device->CreateTexture2D(&descriptor,nullptr,id_texture.put()),
          "Create GPU ID attachment");
    Com<ID3D11RenderTargetView> id_target;
    check(device->CreateRenderTargetView(id_texture.get(),nullptr,id_target.put()),
          "Create ID attachment RTV");
    descriptor.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
    descriptor.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    Com<ID3D11Texture2D> id_depth_texture;
    check(device->CreateTexture2D(&descriptor,nullptr,id_depth_texture.put()),
          "Create GPU ID depth attachment");
    Com<ID3D11DepthStencilView> id_depth_view;
    check(device->CreateDepthStencilView(id_depth_texture.get(),nullptr,
                                         id_depth_view.put()),
          "Create GPU ID DSV");
    D3D11_DEPTH_STENCIL_DESC depth_state_desc{};
    depth_state_desc.DepthEnable=TRUE;
    depth_state_desc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
    depth_state_desc.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    depth_state_desc.StencilEnable=FALSE;
    Com<ID3D11DepthStencilState> id_depth_state;
    check(device->CreateDepthStencilState(&depth_state_desc,id_depth_state.put()),
          "Create GPU ID depth test");
    descriptor.Width=1;
    descriptor.Height=1;
    descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.Usage=D3D11_USAGE_STAGING;
    descriptor.BindFlags=0;
    descriptor.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Com<ID3D11Texture2D> staging;
    check(device->CreateTexture2D(&descriptor,nullptr,staging.put()),
          "Create GPU ID staging pixel");

    // Build ID vertices only for an actual pick request. Normal frames keep
    // 64-byte per-cube transforms, not 36 vertices per cube.
    pick_cube_vertices.clear();
    pick_cube_vertices.reserve(pick_cubes.size()*36u);
    for(auto const& cube:pick_cubes) {
        auto const start=pick_cube_vertices.size();
        add_cube(pick_cube_vertices,cube.transform,cube.id_color);
        auto const r=float((cube.id_color>>16)&255u)/255.f;
        auto const g=float((cube.id_color>>8)&255u)/255.f;
        auto const b=float(cube.id_color&255u)/255.f;
        for(auto v=start;v<pick_cube_vertices.size();++v) {
            pick_cube_vertices[v].r=r;
            pick_cube_vertices[v].g=g;
            pick_cube_vertices[v].b=b;
        }
    }
    ensure_vertex_capacity(pick_cube_vertices.size());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(vertex_buffer.get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),
          "Upload GPU ID vertex colors");
    std::memcpy(mapped.pData,pick_cube_vertices.data(),
                pick_cube_vertices.size()*sizeof(Vertex));
    context->Unmap(vertex_buffer.get(),0);

    // Avoid leaving the offscreen attachments bound if Map/readback fails.
    struct Restore {
        RendererState* state;
        ~Restore() {
            auto* target=state->render_target.get();
            state->context->OMSetRenderTargets(1,&target,state->depth.get());
        }
    } restore{this};

    auto* target=id_target.get();
    context->OMSetRenderTargets(1,&target,id_depth_view.get());
    float const clear[4]{0.f,0.f,0.f,0.f};
    context->ClearRenderTargetView(target,clear);
    context->ClearDepthStencilView(id_depth_view.get(),D3D11_CLEAR_DEPTH,1.f,0);
    D3D11_VIEWPORT view{0.f,0.f,float(width),float(height),0.f,1.f};
    context->RSSetViewports(1,&view);
    context->RSSetState(rasterizer.get());
    context->OMSetBlendState(nullptr,nullptr,0xffffffffu);
    context->OMSetDepthStencilState(id_depth_state.get(),0);
    auto* buffer=vertex_buffer.get();
    UINT stride=sizeof(Vertex),offset=0;
    context->IASetInputLayout(layout.get());
    context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto* camera_buffer=camera.get();
    context->VSSetShader(vertex_shader.get(),nullptr,0);
    context->VSSetConstantBuffers(0,1,&camera_buffer);
    context->PSSetShader(pixel_shader.get(),nullptr,0);
    ID3D11ShaderResourceView* null_views[9]{};
    context->PSSetShaderResources(1,9,null_views);
    context->Draw(static_cast<UINT>(pick_cube_vertices.size()),0);

    D3D11_BOX box{x,y,0,x+1,y+1,1};
    context->CopySubresourceRegion(staging.get(),0,0,0,0,
                                   id_texture.get(),0,&box);
    D3D11_MAPPED_SUBRESOURCE pixel{};
    check(context->Map(staging.get(),0,D3D11_MAP_READ,0,&pixel),
          "Map GPU ID pixel");
    auto const* rgba=static_cast<std::uint8_t const*>(pixel.pData);
    std::uint32_t tag=std::uint32_t(rgba[0]) |
        (std::uint32_t(rgba[1])<<8) |
        (std::uint32_t(rgba[2])<<16);
    context->Unmap(staging.get(),0);

    return tag && tag<=northstarcpp::max_scene_objects
        ?static_cast<int>(tag-1u):-1;
}
} // namespace northstarcpp::render::d3d11::detail
