// Nonblocking D3D12 GPU picking: three persistent offscreen/readback slots.
// No queue->Signal + CPU wait on the UI thread; poll only checks fence value.
#include "renderer_private.hpp"
#include <northstarcpp/render_pick_api.h>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace northstarcpp::render::d3d12::detail {
bool RendererState::enqueue_async_pick(NorthStarRenderPickAsyncRequest const& req) {
    if(!req.request_id || req.reserved || req.scene_generation<minimum_pick_generation ||
       !width || !height || req.x>=width || req.y>=height || !camera_data)
        throw std::invalid_argument("Invalid/stale D3D12 async GPU pick");
    if(!async_pick_queue) {
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&async_pick_queue)),
              "Create dedicated D3D12 picking queue");
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,
                                  IID_PPV_ARGS(&async_pick_fence)),
              "Create D3D12 picking fence");
    }
    AsyncPickSlot* slot=nullptr;
    auto const finished=async_pick_fence->GetCompletedValue();
    for(auto& candidate:async_pick_slots) {
        if(candidate.active && candidate.scene_generation<minimum_pick_generation &&
           candidate.fence_value<=finished)
            candidate.active=false;
        if(!candidate.active && !slot)slot=&candidate;
    }
    if(!slot)return false; // bounded queue full; never block the GUI thread

    std::vector<Vertex> vertices;
    vertices.reserve(pick_cubes.size()*36u+pick_world_vertices.size());
    std::vector<std::uint64_t> tokens;
    std::unordered_map<std::uint64_t,std::uint32_t> indices;
    auto tag_for=[&](std::uint64_t entity)->std::uint32_t {
        if(!entity)return 0;
        if(auto found=indices.find(entity);found!=indices.end())
            return found->second;
        if(tokens.size()>=0xfffffeu)
            throw std::runtime_error("Async GPU pick entity token limit exceeded");
        auto tag=static_cast<std::uint32_t>(tokens.size()+1u);
        tokens.push_back(entity);indices.emplace(entity,tag);return tag;
    };
    if(pick_cube_ids.size()<pick_cubes.size())
        throw std::runtime_error("D3D12 async pick: cube ID array absent");
    for(auto const& cubeRecord:pick_cubes) {
        if(cubeRecord.index>=pick_cube_ids.size())
            throw std::runtime_error("D3D12 async pick: cube ID index invalid");
        auto const tag=tag_for(pick_cube_ids[cubeRecord.index]);
        if(!tag)continue;
        auto const before=vertices.size();
        cube(vertices,cubeRecord.cube,false);
        auto const r=float(tag&255u)/255.f;
        auto const g=float((tag>>8)&255u)/255.f;
        auto const b=float((tag>>16)&255u)/255.f;
        for(auto i=before;i<vertices.size();++i) {
            vertices[i].r=r;vertices[i].g=g;vertices[i].b=b;
        }
    }
    for(auto const& point:pick_world_vertices) {
        auto tag=tag_for(point.entity_id);
        vertices.push_back({point.x,point.y,point.z,
            float(tag&255u)/255.f,float((tag>>8)&255u)/255.f,
            float((tag>>16)&255u)/255.f});
    }
    if(vertices.size()>capacity)
        throw std::runtime_error("D3D12 async GPU picking vertex budget exceeded");
    // An empty scene is still a completed background request, without a
    // GPU draw or indefinite pending state.
    if(vertices.empty()) {
        slot->active=true;
        slot->request_id=req.request_id;
        slot->scene_generation=req.scene_generation;
        slot->fence_value=0;
        slot->tokens.clear();
        slot->width=width;slot->height=height;
        return true;
    }
    if(!slot->allocator) {
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
              IID_PPV_ARGS(&slot->allocator)),
              "Create async pick allocator");
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,
              slot->allocator.Get(),nullptr,IID_PPV_ARGS(&slot->commands)),
              "Create async pick command list");
        check(slot->commands->Close(),"Close initial async pick command list");
    }
    auto allocateUpload=[&](ComPtr<ID3D12Resource>& destination,UINT64 size) {
        auto desc=memory(std::max<UINT64>(256,size));
        auto props=heap(D3D12_HEAP_TYPE_UPLOAD);
        check(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,
              D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
              IID_PPV_ARGS(&destination)),
              "Create async GPU picker upload buffer");
    };
    auto const needed=static_cast<UINT>(vertices.size());
    if(!slot->vertex_upload || slot->vertex_capacity<needed) {
        slot->vertex_capacity=std::min<UINT>(capacity,
            std::max<UINT>(needed,slot->vertex_capacity?
                                  slot->vertex_capacity*2:2048u));
        slot->vertex_upload.Reset();
        allocateUpload(slot->vertex_upload,
            UINT64(slot->vertex_capacity)*sizeof(Vertex));
        slot->vertex_view={
            slot->vertex_upload->GetGPUVirtualAddress(),
            UINT(slot->vertex_capacity*sizeof(Vertex)),UINT(sizeof(Vertex))};
    }
    if(!slot->camera_upload)allocateUpload(slot->camera_upload,256u);
    D3D12_RANGE none{0,0};
    void* mapped=nullptr;
    check(slot->vertex_upload->Map(0,&none,&mapped),
          "Map async pick vertex buffer");
    std::memcpy(mapped,vertices.data(),vertices.size()*sizeof(Vertex));
    slot->vertex_upload->Unmap(0,nullptr);
    check(slot->camera_upload->Map(0,&none,&mapped),
          "Map async pick camera buffer");
    // Camera constant-buffer layout is the standard 256-byte scene matrix
    // already used by the visible cube pass. Snapshot before next frame.
    std::memcpy(mapped,camera_data,256u);
    slot->camera_upload->Unmap(0,nullptr);
    if(!slot->target || slot->width!=width || slot->height!=height) {
        slot->target.Reset();slot->depth.Reset();
        slot->rtv.Reset();slot->dsv.Reset();slot->readback.Reset();
        auto props=heap(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC tex{};
        tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex.Width=width;tex.Height=height;
        tex.DepthOrArraySize=tex.MipLevels=1;
        tex.SampleDesc.Count=1;
        tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE colorClear{};
        colorClear.Format=tex.Format;
        check(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,
              &tex,D3D12_RESOURCE_STATE_RENDER_TARGET,&colorClear,
              IID_PPV_ARGS(&slot->target)),"Create async persistent ID target");
        tex.Format=DXGI_FORMAT_D32_FLOAT;
        tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE depthClear{};
        depthClear.Format=tex.Format;
        depthClear.DepthStencil.Depth=1.f;
        check(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,
              &tex,D3D12_RESOURCE_STATE_DEPTH_WRITE,&depthClear,
              IID_PPV_ARGS(&slot->depth)),"Create async persistent depth target");
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors=1;
        heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        check(device->CreateDescriptorHeap(&heapDesc,
              IID_PPV_ARGS(&slot->rtv)),"Create async pick RTV");
        heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        check(device->CreateDescriptorHeap(&heapDesc,
              IID_PPV_ARGS(&slot->dsv)),"Create async pick DSV");
        device->CreateRenderTargetView(slot->target.Get(),nullptr,
            slot->rtv->GetCPUDescriptorHandleForHeapStart());
        device->CreateDepthStencilView(slot->depth.Get(),nullptr,
            slot->dsv->GetCPUDescriptorHandleForHeapStart());
        auto stagingDesc=memory(256);
        auto stagingProps=heap(D3D12_HEAP_TYPE_READBACK);
        check(device->CreateCommittedResource(&stagingProps,
              D3D12_HEAP_FLAG_NONE,&stagingDesc,
              D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
              IID_PPV_ARGS(&slot->readback)),"Create async persistent readback");
        slot->width=width;slot->height=height;
        slot->target_in_copy_state=false;
    }

    check(slot->allocator->Reset(),"Reset completed async GPU pick allocator");
    check(slot->commands->Reset(slot->allocator.Get(),nullptr),
          "Reset async GPU pick command list");
    auto* list=slot->commands.Get();
    auto const rtv=slot->rtv->GetCPUDescriptorHandleForHeapStart();
    auto const dsv=slot->dsv->GetCPUDescriptorHandleForHeapStart();
    if(slot->target_in_copy_state) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=slot->target.Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1,&barrier);
    }
    list->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    float clear[4]{0.f,0.f,0.f,0.f};
    list->ClearRenderTargetView(rtv,clear,0,nullptr);
    list->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1.f,0,0,nullptr);
    D3D12_VIEWPORT viewport{0.f,0.f,float(width),float(height),0.f,1.f};
    D3D12_RECT scissor{0,0,LONG(width),LONG(height)};
    list->RSSetViewports(1,&viewport);
    list->RSSetScissorRects(1,&scissor);
    list->SetGraphicsRootSignature(signature.Get());
    list->SetGraphicsRootConstantBufferView(
        0,slot->camera_upload->GetGPUVirtualAddress());
    list->SetPipelineState(triangles.Get());
    list->IASetVertexBuffers(0,1,&slot->vertex_view);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(needed,1,0,0);
    D3D12_RESOURCE_BARRIER transition{};
    transition.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition.pResource=slot->target.Get();
    transition.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    transition.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
    transition.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1,&transition);
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource=slot->target.Get();
    src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource=slot->readback.Get();
    dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width=1;
    dst.PlacedFootprint.Footprint.Height=1;
    dst.PlacedFootprint.Footprint.Depth=1;
    dst.PlacedFootprint.Footprint.RowPitch=256;
    D3D12_BOX point{req.x,req.y,0,req.x+1,req.y+1,1};
    list->CopyTextureRegion(&dst,0,0,0,&src,&point);
    check(list->Close(),"Close async picking command list");
    ID3D12CommandList* tasks[]{list};
    async_pick_queue->ExecuteCommandLists(1,tasks);
    auto const fenceValue=++next_async_pick_fence;
    check(async_pick_queue->Signal(async_pick_fence.Get(),fenceValue),
          "Signal async GPU pick fence");
    slot->active=true;
    slot->request_id=req.request_id;
    slot->scene_generation=req.scene_generation;
    slot->fence_value=fenceValue;
    slot->target_in_copy_state=true;
    slot->tokens=std::move(tokens);
    return true;
}
NorthStarRenderPickAsyncResult RendererState::poll_async_pick(
    std::uint64_t request_id) {
    NorthStarRenderPickAsyncResult result{};
    result.request_id=request_id;
    result.status=NORTHSTARCPP_PICK_UNKNOWN;
    for(auto& slot:async_pick_slots) {
        if(!slot.active || slot.request_id!=request_id)continue;
        result.scene_generation=slot.scene_generation;
        if(slot.fence_value && async_pick_fence &&
           async_pick_fence->GetCompletedValue()<slot.fence_value) {
            result.status=NORTHSTARCPP_PICK_PENDING;
            return result;
        }
        if(slot.scene_generation<minimum_pick_generation ||
           slot.width!=width || slot.height!=height) {
            result.status=NORTHSTARCPP_PICK_STALE;
            slot.active=false;
            return result;
        }
        if(slot.fence_value && slot.readback) {
            void* data=nullptr;
            D3D12_RANGE readable{0,4};
            check(slot.readback->Map(0,&readable,&data),
                  "Map completed async GPU pick");
            auto* rgba=static_cast<std::uint8_t const*>(data);
            auto tag=std::uint32_t(rgba[0]) |
                (std::uint32_t(rgba[1])<<8) |
                (std::uint32_t(rgba[2])<<16);
            D3D12_RANGE written{0,0};
            slot.readback->Unmap(0,&written);
            if(tag && tag<=slot.tokens.size())
                result.entity_id=slot.tokens[tag-1u];
        }
        result.status=NORTHSTARCPP_PICK_READY;
        slot.active=false;
        return result;
    }
    return result;
}
void RendererState::invalidate_async_pick(std::uint64_t generation) noexcept {
    minimum_pick_generation=std::max(minimum_pick_generation,generation);
}
void RendererState::shutdown_async_pick() noexcept {
    if(!async_pick_queue || !async_pick_fence || !next_async_pick_fence)return;
    // Destructor-only sync; no GPU fence waits during Qt mouse input.
    HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!done)return;
    if(SUCCEEDED(async_pick_fence->SetEventOnCompletion(
         next_async_pick_fence,done)))
        WaitForSingleObject(done,10000);
    CloseHandle(done);
}
} // namespace northstarcpp::render::d3d12::detail
