// Scene subsystem: independent D3D12 implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
void RendererState::set_triangles(const NorthStarRenderTriangleVertex* source,UINT count){
        if(count>300000||count%3||(count&&!source))
            throw std::invalid_argument("Invalid D3D12 mesh triangle count");
        std::vector<Vertex> result;result.reserve(count);
        for(UINT i=0;i<count;++i) {
            auto const& v=source[i];
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
                throw std::invalid_argument("Invalid D3D12 mesh vertex");
            result.push_back({v.x,v.y,v.z,float((v.rgb>>16)&255)/255.f,
                float((v.rgb>>8)&255)/255.f,float(v.rgb&255)/255.f});
        }
        mesh_vertices.swap(result);
    }
    // Own all geometry and texture contents after return; the host may free its
    // YDD/YTD/ymat buffers immediately.
void RendererState::set_gpu_sky_uniforms(NorthStarGpuSkyUniforms const* source) {
    if(!source) {
        gpu_sky_uniforms={};
        height_fog_density=0.f;
        return;
    }
    auto const* values=reinterpret_cast<float const*>(source);
    for(std::size_t i=0;i<sizeof(*source)/sizeof(float);++i)
        if(!std::isfinite(values[i]))
            throw std::invalid_argument("Non-finite GPU sky uniform value");
    if(source->atmosphere[0]<0.f||source->atmosphere[0]>1.f||
       source->atmosphere[1]<0.f||source->atmosphere[1]>1.f||
       source->cloud[0]<0.f||source->cloud[0]>1.f||
       source->cloud[2]<0.f||source->cloud[2]>=1.f)
        throw std::invalid_argument("GPU sky range violation");
    if(source->sun_direction[3]<0.f || source->sun_direction[3]>.25f)
        throw std::invalid_argument("D3D12 sky fog density out of range");
    gpu_sky_uniforms=*source; // No GPU resource creation or queue wait.
    height_fog_density=source->flags[0]>.5f ? source->sun_direction[3] : 0.f;
}
void RendererState::set_sky(NorthStarSky const* source){
        wait();
        if(!source) {
            sky_index_count=0;sky_gpu_bytes=0;
            sky_vertex.Reset();sky_index.Reset();sky_texture.Reset();
            return;
        }
        if(!source->vertices||!source->indices||!source->rgba8||
           source->vertex_count<3||source->vertex_count>65536||
           source->index_count<3||source->index_count>1000000||
           source->index_count%3||!source->width||!source->height||
           source->width>8192||source->height>8192)
            throw std::invalid_argument("Invalid D3D12 skydome resource dimensions");
        for(UINT i=0;i<source->index_count;++i)
            if(source->indices[i]>=source->vertex_count)
                throw std::invalid_argument("D3D12 skydome index out of bounds");
        for(UINT i=0;i<source->vertex_count;++i) {
            auto const& v=source->vertices[i];
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||
               !std::isfinite(v.u)||!std::isfinite(v.v))
                throw std::invalid_argument("Non-finite D3D12 skydome vertex");
        }
        float tint[4]={source->tint_r,source->tint_g,source->tint_b,1.f};
        for(float c:tint)
            if(!std::isfinite(c)||c<0.f||c>4.f)
                throw std::invalid_argument("Invalid skydome material tint");
        auto upload=heap(D3D12_HEAP_TYPE_UPLOAD);
        auto upload_buffer=[&](const void* data,UINT64 bytes) {
            ComPtr<ID3D12Resource> resource;
            auto d=memory(bytes);
            check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&d,
                D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource)),
                "Create D3D12 skydome geometry buffer");
            void* mapped=nullptr;
            check(resource->Map(0,nullptr,&mapped),"Map skydome geometry");
            std::memcpy(mapped,data,size_t(bytes));
            resource->Unmap(0,nullptr);
            return resource;
        };
        UINT64 vb_bytes=UINT64(source->vertex_count)*sizeof(SkyVertex);
        UINT64 ib_bytes=UINT64(source->index_count)*sizeof(uint32_t);
        auto new_vertex=upload_buffer(source->vertices,vb_bytes);
        auto new_index=upload_buffer(source->indices,ib_bytes);
        UINT pitch=(source->width*4u+255u)&~255u;
        auto staging_desc=memory(UINT64(pitch)*source->height);
        ComPtr<ID3D12Resource> staging;
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,
            &staging_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
            IID_PPV_ARGS(&staging)),"Allocate skydome texture staging");
        unsigned char* mapped=nullptr;
        check(staging->Map(0,nullptr,reinterpret_cast<void**>(&mapped)),
              "Map skydome texture staging");
        for(UINT y=0;y<source->height;++y)
            std::memcpy(mapped+size_t(y)*pitch,
                source->rgba8+size_t(y)*source->width*4u,source->width*4u);
        staging->Unmap(0,nullptr);
        D3D12_RESOURCE_DESC tex{};
        tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex.Width=source->width;tex.Height=source->height;
        tex.DepthOrArraySize=1;tex.MipLevels=1;
        tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;tex.SampleDesc.Count=1;
        auto default_heap=heap(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> new_texture;
        check(device->CreateCommittedResource(&default_heap,D3D12_HEAP_FLAG_NONE,
            &tex,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
            IID_PPV_ARGS(&new_texture)),"Allocate D3D12 skydome texture");
        check(allocator->Reset(),"Reset skydome upload allocator");
        check(commands->Reset(allocator.Get(),nullptr),"Reset skydome upload list");
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource=new_texture.Get();
        dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource=staging.Get();
        src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format=tex.Format;
        src.PlacedFootprint.Footprint.Width=source->width;
        src.PlacedFootprint.Footprint.Height=source->height;
        src.PlacedFootprint.Footprint.Depth=1;
        src.PlacedFootprint.Footprint.RowPitch=pitch;
        commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=new_texture.Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commands->ResourceBarrier(1,&barrier);
        check(commands->Close(),"Close skydome upload");
        ID3D12CommandList* lists[]={commands.Get()};
        queue->ExecuteCommandLists(1,lists);
        wait();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format=tex.Format;
        srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(new_texture.Get(),&srv,
            sky_srv_heap->GetCPUDescriptorHandleForHeapStart());
        sky_vertex=std::move(new_vertex);
        sky_index=std::move(new_index);
        sky_texture=std::move(new_texture);
        sky_vertex_view={sky_vertex->GetGPUVirtualAddress(),UINT(vb_bytes),sizeof(SkyVertex)};
        sky_index_view={sky_index->GetGPUVirtualAddress(),UINT(ib_bytes),DXGI_FORMAT_R32_UINT};
        std::memcpy(sky_tint_data,tint,sizeof(tint));
        sky_index_count=source->index_count;
        sky_gpu_bytes=vb_bytes+ib_bytes+UINT64(source->width)*source->height*4u;
    }

} // namespace northstarcpp::render::d3d12::detail
