// Immutable local-space indexed geometry and per-frame rigid instance matrices.
// D3D12 provider-specific implementation of optional render ABI v1.
#include "renderer_private.hpp"
#include "../shared/frame_texture_memo.hpp"
#include "../../shared/frame_memory_counters.hpp"
#include <northstarcpp/render_rigid_instances_api.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace northstarcpp::render::d3d12::detail {
namespace {
// Keep CPU SRV writes and GPU descriptor tables on exactly the same layout.
// The previous implementation wrote every instance at slot zero while
// DrawIndexedInstanced read five slots per instance.
constexpr UINT kRigidSrvSlots=NORTHSTARCPP_RENDER_MATERIAL_TEXTURE_SLOTS;
static_assert(kRigidSrvSlots==5u);
constexpr UINT rigid_srv_index(UINT instance,UINT slot=0) noexcept {
    return instance*kRigidSrvSlots+slot;
}
static_assert(rigid_srv_index(0,4)==4u &&
              rigid_srv_index(1)==5u &&
              rigid_srv_index(2,4)==14u);
struct RigidConstant {
    MaterialParams material;
    float world[16];
};
static_assert(sizeof(RigidConstant)<=256);
void validate_mesh(NorthStarRigidMesh const& mesh) {
    if(!mesh.key || !mesh.vertices || !mesh.indices ||
       !mesh.vertex_count || !mesh.index_count || mesh.index_count%3 ||
       mesh.vertex_count>NORTHSTARCPP_RIGID_MAX_MESH_VERTICES ||
       mesh.index_count>NORTHSTARCPP_RIGID_MAX_MESH_INDICES)
        throw std::invalid_argument("Invalid immutable rigid mesh");
    for(UINT i=0;i<mesh.vertex_count;++i) {
        auto const& v=mesh.vertices[i];
        for(float x:{v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v})
            if(!std::isfinite(x)||std::abs(x)>1.e7f)
                throw std::invalid_argument("Invalid rigid vertex");
    }
    for(UINT i=0;i<mesh.index_count;++i)
        if(mesh.indices[i]>=mesh.vertex_count)
            throw std::invalid_argument("Rigid mesh index out of range");
}
}
void RendererState::upload_rigid_mesh(NorthStarRigidMesh const& source) {
    if(auto found=rigid_meshes.find(source.key);found!=rigid_meshes.end()) {
        if(found->second.vertex_count!=source.vertex_count ||
           found->second.index_count!=source.index_count)
            throw std::invalid_argument("Rigid mesh key collision");
        return;
    }
    validate_mesh(source);
    const UINT64 vertex_bytes=UINT64(source.vertex_count)*sizeof(NorthStarMaterialVertex);
    const UINT64 index_bytes=UINT64(source.index_count)*sizeof(std::uint32_t);
    if(vertex_bytes>UINT_MAX || index_bytes>UINT_MAX ||
       rigid_mesh_bytes+vertex_bytes+index_bytes>1024ull*1024*1024)
        throw std::length_error("Rigid GPU mesh residency exceeds budget");
    // Upload once. GPU immutable DEFAULT heap contains original LOCAL vertices
    // and indices; subsequent scene frames need only world transforms.
    wait();
    auto upload=heap(D3D12_HEAP_TYPE_UPLOAD);
    auto gpu=heap(D3D12_HEAP_TYPE_DEFAULT);
    auto vb_desc=memory(vertex_bytes);
    auto ib_desc=memory(index_bytes);
    RigidMeshGpu next{};
    ComPtr<ID3D12Resource> staging_v,staging_i;
    check(device->CreateCommittedResource(&gpu,D3D12_HEAP_FLAG_NONE,
        &vb_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
        IID_PPV_ARGS(&next.vertices)),"Create immutable D3D12 rigid vertices");
    check(device->CreateCommittedResource(&gpu,D3D12_HEAP_FLAG_NONE,
        &ib_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
        IID_PPV_ARGS(&next.indices)),"Create immutable D3D12 rigid indices");
    check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,
        &vb_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
        IID_PPV_ARGS(&staging_v)),"Stage rigid vertices");
    check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,
        &ib_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
        IID_PPV_ARGS(&staging_i)),"Stage rigid indices");
    void* mapped=nullptr;
    check(staging_v->Map(0,nullptr,&mapped),"Map rigid vertices");
    std::memcpy(mapped,source.vertices,static_cast<std::size_t>(vertex_bytes));
    staging_v->Unmap(0,nullptr);
    check(staging_i->Map(0,nullptr,&mapped),"Map rigid indices");
    std::memcpy(mapped,source.indices,static_cast<std::size_t>(index_bytes));
    staging_i->Unmap(0,nullptr);
    check(allocator->Reset(),"Reset rigid upload allocator");
    check(commands->Reset(allocator.Get(),nullptr),"Reset rigid upload list");
    commands->CopyBufferRegion(next.vertices.Get(),0,staging_v.Get(),0,vertex_bytes);
    commands->CopyBufferRegion(next.indices.Get(),0,staging_i.Get(),0,index_bytes);
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for(unsigned i=0;i<2;++i) {
        auto& barrier=barriers[i];
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=i?next.indices.Get():next.vertices.Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter=i?D3D12_RESOURCE_STATE_INDEX_BUFFER:
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    }
    commands->ResourceBarrier(2,barriers);
    check(commands->Close(),"Close rigid mesh copy");
    ID3D12CommandList* lists[]{commands.Get()};
    queue->ExecuteCommandLists(1,lists);
    auto const ticket=++fence_value;
    check(queue->Signal(fence.Get(),ticket),"Signal rigid mesh copy");
    wait_for_fence(ticket); // Once per mesh; staging cannot be freed earlier.
    next.vertex_view={next.vertices->GetGPUVirtualAddress(),
        static_cast<UINT>(vertex_bytes),sizeof(NorthStarMaterialVertex)};
    next.index_view={next.indices->GetGPUVirtualAddress(),
        static_cast<UINT>(index_bytes),DXGI_FORMAT_R32_UINT};
    next.vertex_count=source.vertex_count;
    next.index_count=source.index_count;
    next.bytes=vertex_bytes+index_bytes;
    rigid_meshes.emplace(source.key,std::move(next));
    rigid_mesh_bytes+=vertex_bytes+index_bytes;
    rigid_uploaded_bytes+=vertex_bytes+index_bytes;
    diagnostics::frame_memory::copied(
        &NorthStarFrameMemoryCounters::geometry_copy_bytes,
        vertex_bytes+index_bytes);
}
void RendererState::clear_rigid_meshes() {
    wait();
    rigid_draws.clear();
    rigid_meshes.clear();
    rigid_mesh_bytes=0;
}
// ABI v2 optional per-mesh eviction. The queue fence is awaited exactly
// once for the entire batch so no D3D12 index/vertex buffer can be destroyed
// while referenced by a recorded command list or a queued GPU frame.
void RendererState::release_rigid_meshes(std::uint64_t const* keys,UINT count) {
    if((count&&!keys)||count>65536u)
        throw std::invalid_argument("Invalid rigid GPU eviction batch");
    if(!count)return;
    for(UINT i=0;i<count;++i) {
        if(!keys[i])throw std::invalid_argument("Invalid rigid mesh key");
        for(auto const& draw:rigid_draws)
            if(draw.mesh_key==keys[i])
                throw std::invalid_argument("Cannot evict currently submitted rigid mesh");
    }
    wait(); // queue->Signal followed by verified fence completion
    for(UINT i=0;i<count;++i) {
        auto const it=rigid_meshes.find(keys[i]);
        if(it==rigid_meshes.end())continue;
        rigid_mesh_bytes-=it->second.bytes;
        rigid_meshes.erase(it); // COM release after GPU fence
    }
}
void RendererState::set_rigid_instances(NorthStarRigidInstance const* source,UINT count) {
    if((count&&!source) || count>16384u)
        throw std::invalid_argument("Invalid rigid instance count");
    if(count> D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1/5u)
        throw std::length_error("Rigid instance descriptor heap exceeded");
    std::vector<RigidDrawGpu> draws;
    draws.reserve(count);
    std::vector<MaterialTextureUpload> pending;
    UploadPages::Batch uploads(upload_pages,device.Get(),fence->GetCompletedValue());
    northstarcpp::render::FrameTextureMemo<
        std::shared_ptr<MaterialGpuTexture>> textures;
    for(UINT i=0;i<count;++i) {
        auto const& input=source[i];
        auto const mesh=rigid_meshes.find(input.mesh_key);
        if(mesh==rigid_meshes.end() || input.index_count==0 ||
           input.index_count%3 || input.first_index>mesh->second.index_count ||
           input.index_count>mesh->second.index_count-input.first_index)
            throw std::invalid_argument("Rigid instance refers to unknown mesh/range");
        RigidDrawGpu draw{};
        draw.mesh_key=input.mesh_key;draw.entity_id=input.entity_id;
        draw.first_index=input.first_index;draw.index_count=input.index_count;
        for(unsigned j=0;j<16;++j) {
            if(!std::isfinite(input.world[j])||std::abs(input.world[j])>1.e7f)
                throw std::invalid_argument("Invalid rigid world matrix");
            draw.world[j]=input.world[j];
        }
        auto const& mat=input.material;
        for(unsigned j=0;j<4;++j) {
            if(!std::isfinite(mat.base_color[j])||
               mat.base_color[j]<0.f||mat.base_color[j]>1.f)
                throw std::invalid_argument("Invalid rigid PBR color");
            draw.params.base_color[j]=mat.base_color[j];
        }
        if(!std::isfinite(mat.roughness)||mat.roughness<0.f||mat.roughness>1.f||
           !std::isfinite(mat.metallic)||mat.metallic<0.f||mat.metallic>1.f)
            throw std::invalid_argument("Invalid rigid PBR factors");
        draw.params.factors[0]=mat.roughness;
        draw.params.factors[1]=mat.metallic;
        draw.params.factors[2]=mat.unlit?1.f:0.f;
        for(unsigned slot=0;slot<5;++slot) {
            auto const& image=mat.textures[slot];
            if(!image.rgba8&&!image.width&&!image.height&&!image.data_bytes)
                continue;
            auto const srgb=slot==0||slot==4;
            draw.maps[slot]=textures.get(image,srgb?1u:0u,[&] {
                return material_texture(image,srgb,pending,uploads);
            });
            draw.params.present[0]|=1u<<slot;
        }
        draws.push_back(std::move(draw));
    }
    // The renderer owns a single GPU frame in flight. The constant upload
    // resource/descriptors cannot be rewritten until the prior frame retires.
    if(count) {
        wait();
        // The indexed-only frame does not call the legacy materials path.
        // Nevertheless it uses the shared camera CBV for the PBR vertex
        // shader, so provision that buffer before record_gbuffer_pass().
        ensure_material_buffers(0,0);
    }
    if(count>rigid_draw_capacity) {
        rigid_draw_capacity=std::max<std::size_t>(64,count);
        auto const bytes=UINT64(rigid_draw_capacity)*256u;
        auto desc=memory(bytes);
        auto upload=heap(D3D12_HEAP_TYPE_UPLOAD);
        rigid_constants.Reset();
        rigid_constant_bytes=nullptr;
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,
            &desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
            IID_PPV_ARGS(&rigid_constants)),"Create rigid per-instance matrices");
        check(rigid_constants->Map(0,nullptr,
            reinterpret_cast<void**>(&rigid_constant_bytes)),
            "Map rigid per-instance matrices");
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.NumDescriptors=static_cast<UINT>(rigid_draw_capacity*5u);
        heap_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&heap_desc,
            IID_PPV_ARGS(&rigid_srvs)),"Create rigid instance SRVs");
    }
    if(!pending.empty()) {
        check(allocator->Reset(),"Reset rigid texture upload allocator");
        check(commands->Reset(allocator.Get(),nullptr),"Reset rigid texture upload list");
        for(auto const& image:pending) {
            D3D12_TEXTURE_COPY_LOCATION dest{},src{};
            dest.pResource=image.texture->texture.Get();
            dest.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.pResource=image.staging.resource;
            src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset=image.staging.offset;
            src.PlacedFootprint.Footprint.Format=image.texture->format;
            src.PlacedFootprint.Footprint.Width=image.width;
            src.PlacedFootprint.Footprint.Height=image.height;
            src.PlacedFootprint.Footprint.Depth=1;
            src.PlacedFootprint.Footprint.RowPitch=image.pitch;
            commands->CopyTextureRegion(&dest,0,0,0,&src,nullptr);
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource=image.texture->texture.Get();
            barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            commands->ResourceBarrier(1,&barrier);
        }
        check(commands->Close(),"Close rigid texture upload");
        ID3D12CommandList* lists[]{commands.Get()};
        queue->ExecuteCommandLists(1,lists);
        auto const ticket=++fence_value;
        uploads.retire(ticket);
        check(queue->Signal(fence.Get(),ticket),"Signal rigid texture upload");
        wait_for_fence(ticket);
        for(auto const& image:pending) {
            material_texture_cache.emplace(image.fingerprint,image.texture);
            material_cache_bytes+=image.texture->source.size();
        }
    }
    for(UINT i=0;i<count;++i) {
        auto* bytes=rigid_constant_bytes+UINT64(i)*256u;
        RigidConstant constant{};
        constant.material=draws[i].params;
        std::memcpy(constant.world,draws[i].world,sizeof(constant.world));
        std::memcpy(bytes,&constant,sizeof(constant));
        // Every rigid instance owns five contiguous SRVs. Starting each
        // write at heap slot 0 overwrote the previous material and left
        // later instances with uninitialized descriptors (black surfaces).
        // The shader's GPU descriptor table already uses the matching i*5
        // offset in record_rigid_opaque().
        auto handle=rigid_srvs->GetCPUDescriptorHandleForHeapStart();
        handle.ptr+=SIZE_T(rigid_srv_index(i))*material_descriptor_increment;
        for(unsigned slot=0;slot<5;++slot) {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            auto const& texture=draws[i].maps[slot];
            view.Format=texture?texture->format:DXGI_FORMAT_R8G8B8A8_UNORM;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(texture?texture->texture.Get():nullptr,
                                              &view,handle);
            handle.ptr+=material_descriptor_increment;
        }
    }
    rigid_last_indices=0;
    for(auto const& d:draws)rigid_last_indices+=d.index_count;
    rigid_draws.swap(draws);
}
void RendererState::record_rigid_opaque() {
    if(rigid_draws.empty())return;
    ID3D12DescriptorHeap* heaps[]{rigid_srvs.Get()};
    commands->SetDescriptorHeaps(1,heaps);
    commands->SetGraphicsRootSignature(material_signature.Get());
    commands->SetGraphicsRootConstantBufferView(0,
        material_camera_gpu->GetGPUVirtualAddress());
    commands->SetPipelineState(rigid_material_pipeline.Get());
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto const base=rigid_srvs->GetGPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<rigid_draws.size();++i) {
        auto const& d=rigid_draws[i];
        auto const& mesh=rigid_meshes.at(d.mesh_key);
        commands->IASetVertexBuffers(0,1,&mesh.vertex_view);
        commands->IASetIndexBuffer(&mesh.index_view);
        commands->SetGraphicsRootConstantBufferView(1,
            rigid_constants->GetGPUVirtualAddress()+UINT64(i)*256u);
        auto handle=base;
        handle.ptr+=UINT64(rigid_srv_index(i))*material_descriptor_increment;
        commands->SetGraphicsRootDescriptorTable(2,handle);
        commands->DrawIndexedInstanced(d.index_count,1,d.first_index,0,0);
    }
}
void RendererState::record_rigid_shadow(UINT cascade) {
    if(rigid_draws.empty())return;
    commands->SetGraphicsRootSignature(material_signature.Get());
    commands->SetPipelineState(rigid_shadow_pipeline.Get());
    commands->SetGraphicsRootConstantBufferView(0,
        sun_shadow_constants->GetGPUVirtualAddress()+UINT64(cascade)*256u);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(UINT i=0;i<rigid_draws.size();++i) {
        auto const& d=rigid_draws[i];
        auto const& mesh=rigid_meshes.at(d.mesh_key);
        commands->IASetVertexBuffers(0,1,&mesh.vertex_view);
        commands->IASetIndexBuffer(&mesh.index_view);
        commands->SetGraphicsRootConstantBufferView(1,
            rigid_constants->GetGPUVirtualAddress()+UINT64(i)*256u);
        commands->DrawIndexedInstanced(d.index_count,1,d.first_index,0,0);
    }
}
void RendererState::read_rigid_stats(
    NorthStarRigidInstanceStats& stats) const noexcept {
    stats={};
    stats.struct_size=sizeof(stats);
    stats.active_draws=static_cast<std::uint32_t>(rigid_draws.size());
    stats.resident_meshes=static_cast<std::uint32_t>(rigid_meshes.size());
    stats.resident_geometry_bytes=rigid_mesh_bytes;
    stats.lifetime_geometry_uploaded_bytes=rigid_uploaded_bytes;
    stats.last_transform_bytes=rigid_draws.size()*16u*sizeof(float);
    stats.last_material_constant_bytes=rigid_draws.size()*256u;
    stats.last_drawn_indices=rigid_last_indices;
}
} // namespace northstarcpp::render::d3d12::detail
