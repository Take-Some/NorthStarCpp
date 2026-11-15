// Frame subsystem: independent D3D12 implementation.
#include "renderer_private.hpp"
#include "renderer_pass_graph.hpp"
namespace northstarcpp::render::d3d12::detail {
void RendererState::draw_translucent_materials(){
        if(material_draws.empty())return;
        ID3D12DescriptorHeap* heaps[]{material_srv_heap.Get()};
        commands->SetDescriptorHeaps(1,heaps);
        commands->SetGraphicsRootSignature(material_signature.Get());
        commands->SetGraphicsRootConstantBufferView(
            0,material_camera_gpu->GetGPUVirtualAddress());
        commands->IASetVertexBuffers(0,1,&material_vertex_view);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto base=material_srv_heap->GetGPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<material_draws.size();++i) {
            auto const& draw=material_draws[i];
            if(draw.blend_mode!=2 || !draw.variant_pipeline)continue;
            auto handle=base;
            handle.ptr+=UINT64(i*5)*material_descriptor_increment;
            commands->SetPipelineState(draw.variant_pipeline.Get());
            commands->SetGraphicsRootConstantBufferView(1,
                material_constant_gpu->GetGPUVirtualAddress()+UINT64(i)*256u);
            commands->SetGraphicsRootDescriptorTable(2,handle);
            commands->DrawInstanced(draw.count,1,draw.first,0);
        }
    }
void RendererState::draw(const NorthStarRenderFrame& frame){
        if(frame.count>northstarcpp::max_scene_objects||(frame.count&&!frame.cubes))
            throw std::invalid_argument("Invalid render frame");
        RECT rect{};GetClientRect(hwnd,&rect);
        if(rect.right<=0||rect.bottom<=0)return;
        resize(rect.right,rect.bottom);
        resolve_material_graph_pipelines();
        using namespace northstarcpp::math;
        float3 focus(frame.camera_x,frame.camera_y,frame.camera_z);
        float3 direction(std::sin(frame.camera_yaw)*std::cos(frame.camera_pitch),
            std::sin(frame.camera_pitch),std::cos(frame.camera_yaw)*std::cos(frame.camera_pitch));
        float3 right(std::cos(frame.camera_yaw),0.f,-std::sin(frame.camera_yaw));
        float3 up=frame.camera_override
            ?(direction^right)*std::cos(frame.camera_roll)+right*std::sin(frame.camera_roll)
            :float3(0.f,1.f,0.f);
        auto view=frame.camera_override||frame.first_person
            ?create_camera_at(focus,focus+direction,up)
            :create_camera_at(focus+float3(8.f,7.f,-10.f),focus,float3(0.f,1.f,0.f));
        float fov=frame.camera_override?frame.camera_fov_y_degrees*.01745329252f:pi_d4;
        if(!std::isfinite(fov)||fov<=0.f||fov>3.f)
            throw std::invalid_argument("Invalid camera field of view");
        auto projection=create_projection(fov,float(height)/width,
            frame.camera_override?frame.camera_near_clip:.1f,
            frame.camera_override?frame.camera_far_clip:250.f);
        auto matrix=mul4x4(view,projection);
        // Practical-split CSM with light-space texel snapping; each
        // cascade covers an actual camera frustum slice, not a fixed cube.
        northstarcpp::render::ShadowCascadePlan csm{};
        const auto camera_eye=frame.camera_override||frame.first_person?
            focus:focus+float3(8.f,7.f,-10.f);
        float3 camera_forward=direction;
        if(!(frame.camera_override||frame.first_person)) {
            camera_forward=focus-camera_eye;
            const float length=std::sqrt(camera_forward.x*camera_forward.x+
                camera_forward.y*camera_forward.y+
                camera_forward.z*camera_forward.z);
            camera_forward=camera_forward*(1.f/std::max(length,1.e-6f));
        }
        if(shadows_enabled) {
            std::array<float,16> vp{};
            std::memcpy(vp.data(),&matrix,sizeof(vp));
            auto inverse=northstarcpp::render::inverse_view_projection(vp.data());
            csm=northstarcpp::render::build_shadow_cascades(
                vp,inverse,{camera_eye.x,camera_eye.y,camera_eye.z},
                {camera_forward.x,camera_forward.y,camera_forward.z},
                frame.camera_override?frame.camera_near_clip:.1f,
                frame.camera_override?frame.camera_far_clip:250.f,
                sun_shadow_resolution);
        }
        std::vector<Vertex> vertices;
        vertices.reserve(std::min<size_t>(capacity,frame.count*36u+84u+mesh_vertices.size()));
        if(!frame.first_person && !frame.camera_override)
            for(int n=-10;n<=10;++n) {
            float k=float(n),tone=n==0?.38f:.23f;
            vertices.push_back({k,-.01f,-10.f,tone,tone,tone});
            vertices.push_back({k,-.01f,10.f,tone,tone,tone});
            vertices.push_back({-10.f,-.01f,k,tone,tone,tone});
            vertices.push_back({10.f,-.01f,k,tone,tone,tone});
        }
        UINT grid=UINT(vertices.size());
        frustum visible(matrix,clip_depth::zero_to_one);
        for(UINT i=0;i<frame.count;++i) {
            auto const& c=frame.cubes[i];
            if(!std::isfinite(c.size)||c.size<=0.f)continue;
            float half=c.size*.5f;
            float4x4 tr;
            tr.i=float4(c.rotation[0]*half,c.rotation[1]*half,c.rotation[2]*half,0.f);
            tr.j=float4(c.rotation[3]*half,c.rotation[4]*half,c.rotation[5]*half,0.f);
            tr.k=float4(c.rotation[6]*half,c.rotation[7]*half,c.rotation[8]*half,0.f);
            tr.c=float4(c.x,c.y+half,c.z,1.f);
            aabb bounds;bounds.min={-1.f,-1.f,-1.f};bounds.max={1.f,1.f,1.f};
            bounds.modify(tr);bounds+=.05f;
            if(visible.test_inexact(bounds)==intersection_outside) {
                // A caster outside the camera frustum may still cast into
                // a visible CSM slice. Cull against all shadow volumes before
                // rejecting it, instead of camera-only culling.
                bool inside_shadow_volume=false;
                if(shadows_enabled) {
                    float const caster_radius=half*1.732051f*
                        std::max({1.f,
                            std::sqrt(c.rotation[0]*c.rotation[0]+
                                c.rotation[1]*c.rotation[1]+c.rotation[2]*c.rotation[2]),
                            std::sqrt(c.rotation[3]*c.rotation[3]+
                                c.rotation[4]*c.rotation[4]+c.rotation[5]*c.rotation[5]),
                            std::sqrt(c.rotation[6]*c.rotation[6]+
                                c.rotation[7]*c.rotation[7]+c.rotation[8]*c.rotation[8])});
                    for(auto const& cascade:csm.cascades) {
                        auto const& m=cascade.matrix;
                        float const x=c.x*m[0]+(c.y+half)*m[4]+c.z*m[8]+m[12];
                        float const y=c.x*m[1]+(c.y+half)*m[5]+c.z*m[9]+m[13];
                        float const z=c.x*m[2]+(c.y+half)*m[6]+c.z*m[10]+m[14];
                        float const dx=caster_radius*std::sqrt(
                            m[0]*m[0]+m[4]*m[4]+m[8]*m[8]);
                        float const dy=caster_radius*std::sqrt(
                            m[1]*m[1]+m[5]*m[5]+m[9]*m[9]);
                        float const dz=caster_radius*std::sqrt(
                            m[2]*m[2]+m[6]*m[6]+m[10]*m[10]);
                        if(std::abs(x)<=1.f+dx&&std::abs(y)<=1.f+dy&&
                           z>=-dz&&z<=1.f+dz) {
                            inside_shadow_volume=true;break;
                        }
                    }
                }
                if(!inside_shadow_volume)continue;
            }
            if(vertices.size()+36u+mesh_vertices.size()>capacity)
                throw std::runtime_error("D3D12 transient vertex budget exceeded");
            cube(vertices,c,i==UINT(frame.selected));
        }
        if(vertices.size()+mesh_vertices.size()>capacity)
            throw std::runtime_error("D3D12 triangle budget exceeded");
        vertices.insert(vertices.end(),mesh_vertices.begin(),mesh_vertices.end());
        // HUD vertices use native clip coordinates and a separate identity matrix.
        UINT hud_tri_start=UINT(vertices.size());
        std::vector<Vertex> hud_tri,hud_line;
        if(frame.first_person&&frame.player_max_health)
            make_hud(frame,width,height,hud_tri,hud_line);
        if(vertices.size()+hud_tri.size()+hud_line.size()>capacity)
            throw std::runtime_error("D3D12 HUD vertex budget exceeded");
        vertices.insert(vertices.end(),hud_tri.begin(),hud_tri.end());
        UINT hud_line_start=UINT(vertices.size());
        vertices.insert(vertices.end(),hud_line.begin(),hud_line.end());
        bool const has_deferred_geometry =
            hud_tri_start>grid || !material_draws.empty();
        if(has_deferred_geometry) {
            ensure_gbuffer();
            ensure_light_buffers();
        }
        // A deliberately conservative single frame in flight, safe for resizing
        // and transient CPU-side uploads; optimize once profiling is available.
        wait();
        if(!vertices.empty())
            std::memcpy(vertex_data,vertices.data(),vertices.size()*sizeof(Vertex));
        std::memcpy(camera_data,&matrix,sizeof(matrix));
        if(sky_index_count) {
            auto sky_matrix=mul4x4(create_camera_at(float3(0.f,0.f,0.f),
                direction,up),projection);
            std::memcpy(sky_camera_data,&sky_matrix,sizeof(sky_matrix));
        }
        UINT index=swapchain->GetCurrentBackBufferIndex();
        check(allocator->Reset(),"Reset allocator");
        check(commands->Reset(allocator.Get(),nullptr),"Reset command list");
        D3D12ResourceStates states;
        states.seed(backs[index].Get(),D3D12_RESOURCE_STATE_PRESENT);
        states.seed(depth.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE);
        if(has_deferred_geometry)for(auto& target:gbuffer)
            states.seed(target.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if(shadows_enabled)
            states.seed(sun_shadow_map.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if(postfx_enabled()) {
            states.seed(scene_color.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            if(hdr_enabled) {
                states.seed(hdr_scene_color.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                for(auto& target:hdr_exposure)
                    states.seed(target.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
            if(bloom_enabled)
                states.seed(bloom_surface.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        states.transition(commands.Get(),backs[index].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=index*stride;
        auto scene_rtv=hdr_enabled?
            hdr_rtv_heap->GetCPUDescriptorHandleForHeapStart():
            (postfx_enabled()?scene_rtv_heap->GetCPUDescriptorHandleForHeapStart():rtv);
        if(postfx_enabled())
            states.transition(commands.Get(),
                hdr_enabled?hdr_scene_color.Get():scene_color.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto dsv=dsvs->GetCPUDescriptorHandleForHeapStart();
        commands->OMSetRenderTargets(1,&scene_rtv,FALSE,&dsv);
        const float clear[]={.055f,.075f,.105f,1.f};
        commands->ClearRenderTargetView(scene_rtv,clear,0,nullptr);
        commands->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1.f,0,0,nullptr);
        D3D12_VIEWPORT viewport{0.f,0.f,float(width),float(height),0.f,1.f};
        D3D12_RECT scissors{0,0,LONG(width),LONG(height)};
        commands->RSSetViewports(1,&viewport);
        commands->RSSetScissorRects(1,&scissors);
        if(timestamp_heap)commands->EndQuery(timestamp_heap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,0);
        FramePassContext context{frame,matrix,focus,camera_forward,csm,
            vertices,grid,hud_tri_start,hud_line_start,index,has_deferred_geometry,
            states,rtv,scene_rtv,dsv,viewport,scissors};
        // The backend-neutral graph compiles actual pass dependencies.
        // The selected GPU pass records directly into this D3D12 command list.
        // Resource-state transitions are owned by individual pass implementations.
        static const auto frame_graph_plan=build_frame_graph().compile();
        for(auto const& pass:frame_graph_plan.passes) {
            const auto stage=static_cast<FrameStage>(pass.id);
            apply_graph_resource_states(pass,context);
            switch(stage) {
            case FrameStage::Shadow:record_shadow_pass(context);break;
            case FrameStage::GBuffer:record_gbuffer_pass(context);break;
            case FrameStage::Deferred:record_deferred_pass(context);break;
            case FrameStage::Translucency:record_translucency_pass(context);break;
            case FrameStage::Bloom:record_bloom_pass(context);break;
            case FrameStage::Exposure:record_exposure_pass(context);break;
            case FrameStage::Tonemap:record_tonemap_pass(context);break;
            case FrameStage::Composite:record_composite_pass(context);break;
            case FrameStage::Present:record_present_pass(context);break;
            default:throw std::logic_error("Unknown D3D12 render graph pass");
            }
        }
        // Validate the exact end-of-frame contract assumed when the next
        // command-list frame seeds its known initial resource states.
        if(states.state(backs[index].Get())!=D3D12_RESOURCE_STATE_PRESENT ||
           states.state(depth.Get())!=D3D12_RESOURCE_STATE_DEPTH_WRITE)
            throw std::logic_error("Render Graph did not restore backbuffer/depth states");
        if(shadows_enabled &&
           states.state(sun_shadow_map.Get())!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            throw std::logic_error("Render Graph did not restore shadow SRV");
        if(has_deferred_geometry)
            for(auto& target:gbuffer)
                if(states.state(target.Get())!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
                    throw std::logic_error("Render Graph did not restore G-buffer SRV");
        if(postfx_enabled()) {
            if(states.state(scene_color.Get())!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
                throw std::logic_error("Render Graph did not restore scene color SRV");
            if(bloom_enabled &&
               states.state(bloom_surface.Get())!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
                throw std::logic_error("Render Graph did not restore bloom SRV");
            if(hdr_enabled) {
                if(states.state(hdr_scene_color.Get())!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
                    throw std::logic_error("Render Graph did not restore HDR SRV");
                for(auto& exposure:hdr_exposure)
                    if(states.state(exposure.Get())!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
                        throw std::logic_error("Render Graph did not restore exposure SRV");
            }
        }
}
} // namespace northstarcpp::render::d3d12::detail
