// D3D12 analytic grid and true grid-only temporal accumulation.
// The history contains ONLY the grid layer. Opaque geometry and HUD never
// enter the temporal feedback; Editor's grid remains occluded by scene depth.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
void RendererState::set_editor_grid(
    NorthStarRenderEditorGridSettings const* settings) {
    northstarcpp::render::validate_editor_grid(settings);
    if(!settings || !settings->enabled || !editor_grid.enabled ||
       settings->minor_step!=editor_grid.minor_step)
        grid_history_valid=false;
    editor_grid=settings?*settings:NorthStarRenderEditorGridSettings{};
}
void RendererState::draw_editor_grid(FramePassContext& context) {
    auto const& frame=context.frame;
    if(!editor_grid.enabled || !frame.camera_override || frame.first_person)
        return;
    // Reallocate only on resize (which resets the old textures after a GPU
    // fence). The editor grid is never serialized into scene/ECS objects.
    if(!grid_history_textures[0]) {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=width;desc.Height=height;
        desc.DepthOrArraySize=1;desc.MipLevels=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        auto const gpu_heap=heap(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format=desc.Format;
        srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels=1;
        auto rtv=grid_history_rtvs->GetCPUDescriptorHandleForHeapStart();
        auto handle=grid_history_srvs->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<2;++i) {
            check(device->CreateCommittedResource(&gpu_heap,D3D12_HEAP_FLAG_NONE,
                &desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,
                IID_PPV_ARGS(&grid_history_textures[i])),
                "Create grid-only D3D12 TAA history");
            device->CreateRenderTargetView(
                grid_history_textures[i].Get(),nullptr,rtv);
            device->CreateShaderResourceView(
                grid_history_textures[i].Get(),&srv,handle);
            rtv.ptr+=grid_rtv_stride;
            handle.ptr+=grid_srv_stride;
        }
        grid_history_valid=false;
        grid_history_index=0;
    }
    auto current=northstarcpp::render::grid_constants(
        frame,editor_grid,width,height);
    current.grid_options[2]=float(grid_frame_index++%4u);
    auto previous=grid_previous_values;
    if(grid_history_valid) {
        float distance2=0.f;
        for(int i=0;i<3;++i) {
            auto delta=current.eye_spacing[i]-previous.eye_spacing[i];
            distance2+=delta*delta;
        }
        if(distance2>400.f ||
           std::abs(current.camera_angles[0]-previous.camera_angles[0])>.5f ||
           std::abs(current.camera_angles[1]-previous.camera_angles[1])>.5f ||
           std::abs(current.camera_angles[3]-previous.camera_angles[3])>.2f)
            grid_history_valid=false;
    }
    previous.grid_options[0]=grid_history_valid?1.f:0.f;
    std::memcpy(grid_constant_data,&current,sizeof(current));
    std::memcpy(grid_previous_data,&previous,sizeof(previous));
    UINT const next=(grid_history_index+1u)&1u;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=grid_history_textures[next].Get();
    barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
    commands->ResourceBarrier(1,&barrier);
    auto rtv=grid_history_rtvs->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr+=UINT64(next)*grid_rtv_stride;
    commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    float constexpr clear[4]{0,0,0,0};
    commands->ClearRenderTargetView(rtv,clear,0,nullptr);
    ID3D12DescriptorHeap* descriptors[]{grid_history_srvs.Get()};
    commands->SetDescriptorHeaps(1,descriptors);
    commands->SetGraphicsRootSignature(grid_signature.Get());
    commands->SetGraphicsRootConstantBufferView(
        0,grid_constant_buffer->GetGPUVirtualAddress());
    commands->SetGraphicsRootConstantBufferView(
        1,grid_previous_buffer->GetGPUVirtualAddress());
    auto history=grid_history_srvs->GetGPUDescriptorHandleForHeapStart();
    history.ptr+=UINT64(grid_history_index)*grid_srv_stride;
    commands->SetGraphicsRootDescriptorTable(2,history);
    commands->SetPipelineState(grid_temporal_pipeline.Get());
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3,1,0,0);
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commands->ResourceBarrier(1,&barrier);

    // Restore the actual Render Graph scene target after the offscreen
    // history pass, and alpha-composite only the resolved grid.
    commands->OMSetRenderTargets(1,&context.scene_rtv,FALSE,&context.dsv);
    auto composite=grid_history_srvs->GetGPUDescriptorHandleForHeapStart();
    composite.ptr+=UINT64(next)*grid_srv_stride;
    commands->SetGraphicsRootDescriptorTable(2,composite);
    commands->SetPipelineState(hdr_enabled?
        grid_hdr_pipeline.Get():grid_pipeline.Get());
    commands->DrawInstanced(3,1,0,0);
    grid_history_index=next;
    grid_history_valid=true;
    grid_previous_values=current;
}
}
