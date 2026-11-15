// Resources subsystem: independent D3D12 implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
RendererState::~RendererState(){
        if(queue&&fence) {try{wait();}catch(...){}}
        if(vertex_buffer&&vertex_data)vertex_buffer->Unmap(0,nullptr);
        if(constant_buffer&&camera_data)constant_buffer->Unmap(0,nullptr);
        if(sky_camera&&sky_camera_data)sky_camera->Unmap(0,nullptr);
        if(sky_tint&&sky_tint_data)sky_tint->Unmap(0,nullptr);
        if(hud_camera&&hud_camera_data)hud_camera->Unmap(0,nullptr);
        if(grid_constant_buffer&&grid_constant_data)
            grid_constant_buffer->Unmap(0,nullptr);
        if(grid_previous_buffer&&grid_previous_data)
            grid_previous_buffer->Unmap(0,nullptr);
        if(grid_constant_buffer&&grid_constant_data)
            grid_constant_buffer->Unmap(0,nullptr);
        if(material_vertex_gpu&&material_vertex_data)
            material_vertex_gpu->Unmap(0,nullptr);
        if(material_constant_gpu&&material_constant_data)
            material_constant_gpu->Unmap(0,nullptr);
        if(material_camera_gpu&&material_camera_data)
            material_camera_gpu->Unmap(0,nullptr);
        if(deferred_constants&&deferred_constant_data)
            deferred_constants->Unmap(0,nullptr);
        if(dynamic_light_buffer&&dynamic_light_bytes)
            dynamic_light_buffer->Unmap(0,nullptr);
        if(tile_index_buffer&&tile_index_bytes)
            tile_index_buffer->Unmap(0,nullptr);
        if(fxaa_constants&&fxaa_constant_data)
            fxaa_constants->Unmap(0,nullptr);
        if(sun_shadow_constants&&sun_shadow_constants_data)
            sun_shadow_constants->Unmap(0,nullptr);
        if(event)CloseHandle(event);
    }
void RendererState::wait(){
        UINT64 next=++fence_value;
        check(queue->Signal(fence.Get(),next),"Signal D3D12 fence");
        if(fence->GetCompletedValue()<next) {
            check(fence->SetEventOnCompletion(next,event),"Wait D3D12 fence");
            if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)
                throw std::runtime_error("D3D12 GPU timeout");
        }
    }
void RendererState::resize(UINT w,UINT h){
        if(w==width&&h==height)return;
        wait();
        // Fence was signaled before entering the pool: no live GPU users
        // retain these committed resources during swapchain recreation.
        texture_reuse_pool.recycle(device.Get(),depth,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        // Placed G-buffer normal and Bloom targets overlap physically. They
        // cannot enter the committed-texture reuse pool independently.
        if(transient_alias.enabled()) {
            gbuffer[1].Reset();
            bloom_surface.Reset();
            transient_alias.reset(); // after the queue fence and COM releases
        }
        for(auto& target:gbuffer)
            texture_reuse_pool.recycle(device.Get(),target,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        texture_reuse_pool.recycle(device.Get(),scene_color,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        texture_reuse_pool.recycle(device.Get(),bloom_surface,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        texture_reuse_pool.recycle(device.Get(),hdr_scene_color,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        for(auto& exposure:hdr_exposure)
            texture_reuse_pool.recycle(device.Get(),exposure,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        for(auto& b:backs)b.Reset();depth.Reset();
        for(auto& history:grid_history_textures)history.Reset();
        grid_history_valid=false;
        grid_history_index=0;
        for(auto& target:gbuffer)target.Reset();
        if(tile_index_buffer&&tile_index_bytes) {
            tile_index_buffer->Unmap(0,nullptr);
            tile_index_bytes=nullptr;
        }
        tile_index_buffer.Reset();
        tile_buffer_entries=0;
        scene_color.Reset();
        bloom_surface.Reset();
        hdr_scene_color.Reset();
        for(auto& exposure:hdr_exposure)exposure.Reset();
        exposure_frame_index=0;
        exposure_history_valid=false;
        check(swapchain->ResizeBuffers(buffer_count,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,0),
            "Resize D3D12 swapchain");
        auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<buffer_count;++i) {
            check(swapchain->GetBuffer(i,IID_PPV_ARGS(&backs[i])),"Get D3D12 buffer");
            device->CreateRenderTargetView(backs[i].Get(),nullptr,rtv);
            rtv.ptr+=stride;
        }
        D3D12_RESOURCE_DESC d{};
        d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
        d.DepthOrArraySize=1;d.MipLevels=1;d.Format=DXGI_FORMAT_R32_TYPELESS;
        d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth=1.f;
        texture_reuse_pool.acquire(device.Get(),d,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,clear,depth,"Create D3D12 depth");
        D3D12_DEPTH_STENCIL_VIEW_DESC depth_view{};
        depth_view.Format=DXGI_FORMAT_D32_FLOAT;
        depth_view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(depth.Get(),&depth_view,
            dsvs->GetCPUDescriptorHandleForHeapStart());
        // Same allocation, separate typed view, legal only after DEPTH_WRITE
        // -> PIXEL_SHADER_RESOURCE transition during the lighting pass.
        D3D12_SHADER_RESOURCE_VIEW_DESC depth_srv{};
        depth_srv.Format=DXGI_FORMAT_R32_FLOAT;
        depth_srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        depth_srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depth_srv.Texture2D.MipLevels=1;
        auto depth_handle=deferred_srvs->GetCPUDescriptorHandleForHeapStart();
        depth_handle.ptr+=UINT64(3)*deferred_srv_stride;
        device->CreateShaderResourceView(depth.Get(),&depth_srv,depth_handle);
        if(postfx_enabled()) {
            D3D12_RESOURCE_DESC scene_desc{};
            scene_desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            scene_desc.Width=w;scene_desc.Height=h;
            scene_desc.DepthOrArraySize=1;scene_desc.MipLevels=1;
            scene_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            scene_desc.SampleDesc.Count=1;
            scene_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE scene_clear{};
            scene_clear.Format=scene_desc.Format;
            texture_reuse_pool.acquire(device.Get(),scene_desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,scene_clear,
                scene_color,"Create post-process scene color");
            device->CreateRenderTargetView(scene_color.Get(),nullptr,
                scene_rtv_heap->GetCPUDescriptorHandleForHeapStart());
            D3D12_SHADER_RESOURCE_VIEW_DESC scene_view{};
            scene_view.Format=scene_desc.Format;
            scene_view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            scene_view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            scene_view.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(scene_color.Get(),&scene_view,
                scene_srv_heap->GetCPUDescriptorHandleForHeapStart());
            // The 2-slot table is fully populated even with bloom disabled.
            // Shaders skip t1 unless enabled; still avoid undefined descriptors.
            auto bloom_handle=scene_srv_heap->GetCPUDescriptorHandleForHeapStart();
            bloom_handle.ptr+=device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            if(bloom_enabled) {
                D3D12_RESOURCE_DESC bloom_desc=scene_desc;
                bloom_desc.Width=std::max<UINT>(1u,w/4u);
                bloom_desc.Height=std::max<UINT>(1u,h/4u);
                bloom_desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
                D3D12_CLEAR_VALUE bloom_clear{};
                bloom_clear.Format=bloom_desc.Format;
                if(transient_aliasing_enabled) {
                    // Graph lifetimes: GBufferNormal [G-buffer,Deferred] and
                    // Bloom [PostFX]. The graph plan must prove non-overlap.
                    auto normal_desc=scene_desc;
                    normal_desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
                    const auto graph=build_frame_graph();
                    transient_alias.configure(device.Get(),graph,graph.compile(),
                        static_cast<northstarcpp::render::ResourceId>(FrameResource::GBufferNormal),
                        normal_desc,
                        static_cast<northstarcpp::render::ResourceId>(FrameResource::Bloom),
                        bloom_desc);
                    transient_alias.create(device.Get(),
                        static_cast<northstarcpp::render::ResourceId>(FrameResource::Bloom),
                        bloom_desc,bloom_clear,bloom_surface);
                } else {
                    texture_reuse_pool.acquire(device.Get(),bloom_desc,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,bloom_clear,
                        bloom_surface,"Create quarter-resolution bloom surface");
                }
                device->CreateRenderTargetView(bloom_surface.Get(),nullptr,
                    bloom_rtv_heap->GetCPUDescriptorHandleForHeapStart());
                scene_view.Format=bloom_desc.Format;
                device->CreateShaderResourceView(bloom_surface.Get(),&scene_view,bloom_handle);
            } else {
                device->CreateShaderResourceView(scene_color.Get(),&scene_view,bloom_handle);
            }
            auto scene_inc=device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            auto third_scene_srv=scene_srv_heap->GetCPUDescriptorHandleForHeapStart();
            third_scene_srv.ptr+=UINT64(2)*scene_inc;
            scene_view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            device->CreateShaderResourceView(scene_color.Get(),&scene_view,third_scene_srv);
            if(hdr_enabled) {
                D3D12_RESOURCE_DESC hdr_desc=scene_desc;
                hdr_desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
                D3D12_CLEAR_VALUE hdr_clear{};
                hdr_clear.Format=hdr_desc.Format;
                texture_reuse_pool.acquire(device.Get(),hdr_desc,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,hdr_clear,
                    hdr_scene_color,"Create FP16 HDR scene target");
                device->CreateRenderTargetView(hdr_scene_color.Get(),nullptr,
                    hdr_rtv_heap->GetCPUDescriptorHandleForHeapStart());
                D3D12_RESOURCE_DESC exposure_desc=hdr_desc;
                exposure_desc.Width=1;exposure_desc.Height=1;
                for(UINT exposure_index=0;exposure_index<2;++exposure_index) {
                    texture_reuse_pool.acquire(device.Get(),exposure_desc,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,hdr_clear,
                        hdr_exposure[exposure_index],"Create HDR temporal exposure target");
                    auto handle=hdr_exposure_rtv_heap->GetCPUDescriptorHandleForHeapStart();
                    handle.ptr+=UINT64(exposure_index)*hdr_exposure_rtv_stride;
                    device->CreateRenderTargetView(hdr_exposure[exposure_index].Get(),
                        nullptr,handle);
                }
                auto make_hdr_views=[&](ID3D12DescriptorHeap* descriptor_heap,
                                        ID3D12Resource* exposure_source,
                                        bool null_bloom) {
                    auto srv=descriptor_heap->GetCPUDescriptorHandleForHeapStart();
                    D3D12_SHADER_RESOURCE_VIEW_DESC hdr_view{};
                    hdr_view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
                    hdr_view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    hdr_view.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
                    hdr_view.Texture2D.MipLevels=1;
                    device->CreateShaderResourceView(hdr_scene_color.Get(),&hdr_view,srv);
                    srv.ptr+=scene_inc;
                    device->CreateShaderResourceView(
                        null_bloom?nullptr:
                            (bloom_enabled?bloom_surface.Get():hdr_scene_color.Get()),
                        &hdr_view,srv);
                    srv.ptr+=scene_inc;
                    device->CreateShaderResourceView(
                        exposure_source,&hdr_view,srv);
                };
                for(UINT i=0;i<2;++i) {
                    make_hdr_views(hdr_srvs[i].Get(),hdr_exposure[i].Get(),false);
                    make_hdr_views(hdr_exposure_srvs[i].Get(),
                                   hdr_exposure[1-i].Get(),true);
                }
                make_hdr_views(hdr_bloom_srvs.Get(),nullptr,true);
            }
        }
        width=w;height=h;
        char trace_pool[8]{};
        if(GetEnvironmentVariableA("NORTHSTARCPP_TRACE_RENDER_POOL",
                trace_pool,sizeof(trace_pool))>0 && trace_pool[0]=='1') {
            std::fprintf(stderr,
                "[NorthStar D3D12][RenderTargetPool] resize=%ux%u hits=%llu "
                "cached_bytes=%llu cached_targets=%zu budget_bytes=%llu\n",
                unsigned(width),unsigned(height),
                static_cast<unsigned long long>(texture_reuse_pool.cache_hits()),
                static_cast<unsigned long long>(texture_reuse_pool.cached_bytes()),
                texture_reuse_pool.count(),
                static_cast<unsigned long long>(D3D12TextureReusePool::budget_bytes));
            if(transient_alias.enabled())
                std::fprintf(stderr,
                    "[NorthStar D3D12][TransientAlias] placed_heap=%llu "
                    "bytes_saved=%llu\n",
                    static_cast<unsigned long long>(transient_alias.allocated_bytes()),
                    static_cast<unsigned long long>(transient_alias.saved_bytes()));
        }
    }
void RendererState::ensure_gbuffer(){
        if(gbuffer[0])return;
        if(!width||!height)throw std::logic_error("Deferred targets need a viewport");
        DXGI_FORMAT const formats[3]{
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R16G16B16A16_FLOAT};
        auto rtv=deferred_rtvs->GetCPUDescriptorHandleForHeapStart();
        auto srv=deferred_srvs->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<3;++i) {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=width;desc.Height=height;
            desc.DepthOrArraySize=1;desc.MipLevels=1;
            desc.Format=formats[i];
            desc.SampleDesc.Count=1;
            desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE clear{};
            clear.Format=formats[i]; // zero alpha: no deferred coverage
            if(i==1 && transient_alias.enabled()) {
                transient_alias.create(device.Get(),
                    static_cast<northstarcpp::render::ResourceId>(FrameResource::GBufferNormal),
                    desc,clear,gbuffer[i]);
            } else {
                texture_reuse_pool.acquire(device.Get(),desc,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,clear,
                    gbuffer[i],"Allocate D3D12 deferred G-buffer");
            }
            device->CreateRenderTargetView(gbuffer[i].Get(),nullptr,rtv);
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format=formats[i];
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(gbuffer[i].Get(),&view,srv);
            rtv.ptr+=deferred_rtv_stride;
            srv.ptr+=deferred_srv_stride;
        }
    }
void RendererState::ensure_light_buffers(){
        auto upload=heap(D3D12_HEAP_TYPE_UPLOAD);
        if(!dynamic_light_buffer) {
            auto buffer=memory(UINT64(NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS)*
                               sizeof(NorthStarRenderLight));
            check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,
                &buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
                IID_PPV_ARGS(&dynamic_light_buffer)),
                "Create dynamic light upload");
            check(dynamic_light_buffer->Map(0,nullptr,
                reinterpret_cast<void**>(&dynamic_light_bytes)),
                "Map dynamic light upload");
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format=DXGI_FORMAT_UNKNOWN;
            view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Buffer.NumElements=NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS;
            view.Buffer.StructureByteStride=sizeof(NorthStarRenderLight);
            auto handle=deferred_srvs->GetCPUDescriptorHandleForHeapStart();
            handle.ptr+=UINT64(4)*deferred_srv_stride;
            device->CreateShaderResourceView(dynamic_light_buffer.Get(),&view,handle);
        }
        if(!tile_index_buffer) {
            auto const columns=(width+northstarcpp::render::light_tile_width-1)/
                               northstarcpp::render::light_tile_width;
            auto const rows=(height+northstarcpp::render::light_tile_width-1)/
                            northstarcpp::render::light_tile_width;
            UINT64 entries=UINT64(columns)*rows*northstarcpp::render::light_tile_stride;
            if(!entries||entries>UINT_MAX)
                throw std::overflow_error("Tiled lighting buffer too large");
            tile_buffer_entries=static_cast<UINT>(entries);
            auto buffer=memory(entries*sizeof(std::uint32_t));
            check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,
                &buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
                IID_PPV_ARGS(&tile_index_buffer)),
                "Create tiled light index upload");
            check(tile_index_buffer->Map(0,nullptr,
                reinterpret_cast<void**>(&tile_index_bytes)),
                "Map tiled light index upload");
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format=DXGI_FORMAT_R32_UINT;
            view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Buffer.NumElements=tile_buffer_entries;
            auto handle=deferred_srvs->GetCPUDescriptorHandleForHeapStart();
            handle.ptr+=UINT64(5)*deferred_srv_stride;
            device->CreateShaderResourceView(tile_index_buffer.Get(),&view,handle);
        }
    }
void RendererState::set_lights(NorthStarRenderLight const* data,UINT count){
        if(count>NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS||(count&&!data))
            throw std::invalid_argument("Invalid dynamic light count");
        for(UINT i=0;i<count;++i) {
            auto const& source=data[i];
            for(float value:source.position_range)
                if(!std::isfinite(value))throw std::invalid_argument("Invalid light position");
            for(float value:source.color_intensity)
                if(!std::isfinite(value)||value<0)
                    throw std::invalid_argument("Invalid light color/intensity");
            for(float value:source.direction_cone)
                if(!std::isfinite(value))throw std::invalid_argument("Invalid spot direction");
            if(source.position_range[3]<=0 || source.position_range[3]>100000.f ||
               source.direction_cone[3]<-1.f || source.direction_cone[3]>1.f)
                throw std::invalid_argument("Invalid light attenuation or cone");
            if(source.direction_cone[3]>=0) {
                float const len=source.direction_cone[0]*source.direction_cone[0]+
                    source.direction_cone[1]*source.direction_cone[1]+
                    source.direction_cone[2]*source.direction_cone[2];
                if(len<1.e-8f)throw std::invalid_argument("Invalid spot axis");
            }
        }
        if(!count)dynamic_lights.clear();
        else dynamic_lights.assign(data,data+count);
    }
void RendererState::ensure_material_buffers(){
        if(material_vertex_gpu)return;
        auto upload=heap(D3D12_HEAP_TYPE_UPLOAD);
        auto vertices=memory(UINT64(NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES)*
                             sizeof(NorthStarMaterialVertex));
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&vertices,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
            IID_PPV_ARGS(&material_vertex_gpu)),"Create D3D12 material vertex upload");
        check(material_vertex_gpu->Map(0,nullptr,
            reinterpret_cast<void**>(&material_vertex_data)),"Map D3D12 material vertices");
        material_vertex_view.BufferLocation=material_vertex_gpu->GetGPUVirtualAddress();
        material_vertex_view.StrideInBytes=sizeof(NorthStarMaterialVertex);
        material_vertex_view.SizeInBytes=static_cast<UINT>(vertices.Width);
        auto constants=memory(UINT64(NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES)*256u);
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&constants,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
            IID_PPV_ARGS(&material_constant_gpu)),"Create D3D12 PBR constants");
        check(material_constant_gpu->Map(0,nullptr,
            reinterpret_cast<void**>(&material_constant_data)),"Map D3D12 PBR constants");
        auto camera_desc=memory(256);
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&camera_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
            IID_PPV_ARGS(&material_camera_gpu)),"Create PBR camera constant");
        check(material_camera_gpu->Map(0,nullptr,
            reinterpret_cast<void**>(&material_camera_data)),"Map PBR camera constant");
    }
} // namespace northstarcpp::render::d3d12::detail
