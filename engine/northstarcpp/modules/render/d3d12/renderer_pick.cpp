// Native D3D12 on-demand GPU object-ID pass for cube primitives.
// Pixel IDs live in a dedicated unlit RGBA8 render target with depth testing;
// ordinary scene postprocessing and materials never influence ID encoding.
#include "renderer_private.hpp"
#include <northstarcpp/render_pick_api.h>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <cmath>

namespace northstarcpp::render::d3d12::detail {

void RendererState::set_pick_triangles(NorthStarRenderPickVertex const* vertices,
                                      std::uint32_t count) {
    if(count%3 || count>NORTHSTARCPP_RENDER_PICK_MAX_VERTICES || (count&&!vertices))
        throw std::invalid_argument("Invalid D3D12 object-ID triangles");
    for(std::uint32_t i=0;i<count;i+=3) {
        auto id=vertices[i].entity_id;
        for(unsigned j=0;j<3;++j) {
            auto const& v=vertices[i+j];
            if(v.entity_id!=id || v.reserved || !std::isfinite(v.x) ||
               !std::isfinite(v.y) || !std::isfinite(v.z))
                throw std::invalid_argument("Invalid D3D12 pick triangle/ID");
        }
    }
    if(count)pick_world_vertices.assign(vertices,vertices+count);
    else pick_world_vertices.clear();
}
void RendererState::set_pick_cubes(std::uint64_t const* ids,std::uint32_t count) {
    if(count>200000u || (count&&!ids))
        throw std::invalid_argument("Invalid D3D12 pick cube IDs");
    if(count)pick_cube_ids.assign(ids,ids+count);
    else pick_cube_ids.clear();
}
std::uint64_t RendererState::pick_entity(std::uint32_t x,std::uint32_t y) {
    pick_entity_mode=true;last_picked_entity=0;
    try {pick_cube(x,y);}
    catch(...) {pick_entity_mode=false;throw;}
    pick_entity_mode=false;
    return last_picked_entity;
}
int RendererState::pick_cube(std::uint32_t x,std::uint32_t y) {
    if(!device || !width || !height || x>=width || y>=height)
        throw std::invalid_argument("D3D12 picking outside valid GPU viewport");
    // A cube-only ID attachment cannot account for real mesh occlusion.
    if(!pick_entity_mode && (!mesh_vertices.empty() || !material_draws.empty()))
        throw std::runtime_error("D3D12 GPU picking requires per-mesh object IDs");
    if(pick_cubes.empty() && (!pick_entity_mode || pick_world_vertices.empty()))return -1;
    std::vector<Vertex> vertices;
    vertices.reserve(pick_cubes.size()*36u+pick_world_vertices.size());
    std::vector<std::uint64_t> entity_tokens;
    std::unordered_map<std::uint64_t,std::uint32_t> token_map;
    auto token_for=[&](std::uint64_t id)->std::uint32_t {
        if(!id)return 0;
        if(auto found=token_map.find(id);found!=token_map.end())
            return found->second;
        if(entity_tokens.size()>=0xfffffeu)
            throw std::runtime_error("D3D12 pick token budget exceeded");
        auto const token=static_cast<std::uint32_t>(entity_tokens.size()+1);
        entity_tokens.push_back(id);token_map.emplace(id,token);
        return token;
    };
    for(auto const& record:pick_cubes) {
        if(record.index+1u>=1u<<24)
            throw std::runtime_error("D3D12 object ID exceeds RGB24");
        auto const first=vertices.size();
        cube(vertices,record.cube,false);
        const auto tag=pick_entity_mode?
            token_for(record.index<pick_cube_ids.size()?
                pick_cube_ids[record.index]:0):record.index+1u;
        if(!tag){vertices.resize(first);continue;}
        float const r=float(tag&255u)/255.f;
        float const g=float((tag>>8)&255u)/255.f;
        float const b=float((tag>>16)&255u)/255.f;
        for(auto n=first;n<vertices.size();++n) {
            // The visible cube helper shades faces; IDs must remain unlit.
            vertices[n].r=r;
            vertices[n].g=g;
            vertices[n].b=b;
        }
    }
    if(pick_entity_mode) {
        if(pick_cube_ids.size()<pick_cubes.size())
            throw std::runtime_error("D3D12 pick cube ID submission missing");
        for(auto const& vertex:pick_world_vertices) {
            auto const tag=token_for(vertex.entity_id);
            // Vertex triples retain their original order, including
            // unselectable zero-ID triangles (depth tested, no hit).
            vertices.push_back({vertex.x,vertex.y,vertex.z,
                float(tag&255u)/255.f,float((tag>>8)&255u)/255.f,
                float((tag>>16)&255u)/255.f});
        }
    }
    if(vertices.empty())return -1;
    if(vertices.size()>capacity)
        throw std::runtime_error("D3D12 object ID vertex capacity exceeded");
    wait(); // Finish previous frame before modifying shared upload memory.
    std::memcpy(vertex_data,vertices.data(),vertices.size()*sizeof(Vertex));

    auto default_heap=heap(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC color_desc{};
    color_desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    color_desc.Width=width;color_desc.Height=height;
    color_desc.DepthOrArraySize=color_desc.MipLevels=1;
    color_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    color_desc.SampleDesc.Count=1;
    color_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear_color{};
    clear_color.Format=color_desc.Format;
    ComPtr<ID3D12Resource> id_color;
    check(device->CreateCommittedResource(&default_heap,D3D12_HEAP_FLAG_NONE,
        &color_desc,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear_color,
        IID_PPV_ARGS(&id_color)),"Create D3D12 GPU ID attachment");

    D3D12_RESOURCE_DESC depth_desc=color_desc;
    depth_desc.Format=DXGI_FORMAT_D32_FLOAT;
    depth_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear_depth{};
    clear_depth.Format=DXGI_FORMAT_D32_FLOAT;
    clear_depth.DepthStencil.Depth=1.f;
    ComPtr<ID3D12Resource> id_depth;
    check(device->CreateCommittedResource(&default_heap,D3D12_HEAP_FLAG_NONE,
        &depth_desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear_depth,
        IID_PPV_ARGS(&id_depth)),"Create D3D12 GPU ID depth attachment");

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> color_heap;
    check(device->CreateDescriptorHeap(&heap_desc,
        IID_PPV_ARGS(&color_heap)),"Create D3D12 GPU ID RTV");
    heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ComPtr<ID3D12DescriptorHeap> depth_heap;
    check(device->CreateDescriptorHeap(&heap_desc,
        IID_PPV_ARGS(&depth_heap)),"Create D3D12 GPU ID DSV");
    auto const rtv=color_heap->GetCPUDescriptorHandleForHeapStart();
    auto const dsv=depth_heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(id_color.Get(),nullptr,rtv);
    device->CreateDepthStencilView(id_depth.Get(),nullptr,dsv);

    auto read_heap=heap(D3D12_HEAP_TYPE_READBACK);
    auto const read_desc=memory(256); // D3D12 texture row pitch alignment.
    ComPtr<ID3D12Resource> staging;
    check(device->CreateCommittedResource(&read_heap,D3D12_HEAP_FLAG_NONE,
        &read_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
        IID_PPV_ARGS(&staging)),"Create D3D12 ID staging readback");

    check(allocator->Reset(),"Reset D3D12 GPU ID command allocator");
    check(commands->Reset(allocator.Get(),nullptr),"Reset D3D12 GPU ID command list");
    commands->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    float const clear[4]{0.f,0.f,0.f,0.f};
    commands->ClearRenderTargetView(rtv,clear,0,nullptr);
    commands->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1.f,0,0,nullptr);
    D3D12_VIEWPORT viewport{0.f,0.f,float(width),float(height),0.f,1.f};
    D3D12_RECT scissor{0,0,LONG(width),LONG(height)};
    commands->RSSetViewports(1,&viewport);
    commands->RSSetScissorRects(1,&scissor);
    commands->SetGraphicsRootSignature(signature.Get());
    commands->SetGraphicsRootConstantBufferView(
        0,constant_buffer->GetGPUVirtualAddress());
    commands->SetPipelineState(triangles.Get());
    commands->IASetVertexBuffers(0,1,&vertex_view);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(static_cast<UINT>(vertices.size()),1,0,0);
    D3D12_RESOURCE_BARRIER transition{};
    transition.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition.pResource=id_color.Get();
    transition.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    transition.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
    transition.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1,&transition);

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource=id_color.Get();
    src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource=staging.Get();
    dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width=1;
    dst.PlacedFootprint.Footprint.Height=1;
    dst.PlacedFootprint.Footprint.Depth=1;
    dst.PlacedFootprint.Footprint.RowPitch=256;
    D3D12_BOX const point{x,y,0,x+1,y+1,1};
    commands->CopyTextureRegion(&dst,0,0,0,&src,&point);
    check(commands->Close(),"Close D3D12 GPU ID command list");
    ID3D12CommandList* lists[]{commands.Get()};
    queue->ExecuteCommandLists(1,lists);
    wait();
    D3D12_RANGE const read_range{0,4};
    void* pixel=nullptr;
    check(staging->Map(0,&read_range,&pixel),"Map D3D12 GPU ID result");
    auto const* rgba=static_cast<std::uint8_t const*>(pixel);
    auto const tag=std::uint32_t(rgba[0]) |
        (std::uint32_t(rgba[1])<<8) |
        (std::uint32_t(rgba[2])<<16);
    D3D12_RANGE const written{0,0};
    staging->Unmap(0,&written);
    if(pick_entity_mode) {
        last_picked_entity=tag && tag<=entity_tokens.size()?
            entity_tokens[tag-1u]:0;
        return -1;
    }
    return tag?int(tag-1u):-1;
}
} // namespace northstarcpp::render::d3d12::detail
