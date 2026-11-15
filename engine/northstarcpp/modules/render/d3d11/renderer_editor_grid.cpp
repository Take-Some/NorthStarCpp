// Analytic editor grid with grid-only temporally reprojected history.
// Fullscreen triangles; no geometry streaming and no game-scene history.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d11::detail {
void RendererState::set_editor_grid(
    NorthStarRenderEditorGridSettings const* settings) {
    northstarcpp::render::validate_editor_grid(settings);
    if(!settings || !settings->enabled ||
       !editor_grid.enabled ||
       settings->minor_step!=editor_grid.minor_step)
        grid_history_valid=false;
    editor_grid=settings?*settings:NorthStarRenderEditorGridSettings{};
}
void RendererState::draw_editor_grid(NorthStarRenderFrame const& frame) {
    if(!editor_grid.enabled || !frame.camera_override || frame.first_person)
        return;
    if(!grid_history_textures[0].get()) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width;desc.Height=height;
        desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        for(unsigned i=0;i<2;++i) {
            check(device->CreateTexture2D(&desc,nullptr,
                grid_history_textures[i].put()),
                "Create D3D11 grid-only TAA history");
            check(device->CreateRenderTargetView(
                grid_history_textures[i].get(),nullptr,
                grid_history_rtvs[i].put()),"Create grid history RTV");
            check(device->CreateShaderResourceView(
                grid_history_textures[i].get(),nullptr,
                grid_history_srvs[i].put()),"Create grid history SRV");
        }
        grid_history_valid=false;
        grid_history_index=0;
    }
    auto current=northstarcpp::render::grid_constants(
        frame,editor_grid,width,height);
    current.grid_options[2]=float(grid_frame_index++%4u);
    auto prior=grid_previous_values;
    if(grid_history_valid) {
        float sum=0.f;
        for(int i=0;i<3;++i) {
            const auto d=current.eye_spacing[i]-prior.eye_spacing[i];
            sum+=d*d;
        }
        // Never carry history through teleports, hard cuts, abrupt FOV,
        // or camera rotations that cannot reliably reproject the grid.
        if(sum>400.f ||
           std::abs(current.camera_angles[0]-prior.camera_angles[0])>.5f ||
           std::abs(current.camera_angles[1]-prior.camera_angles[1])>.5f ||
           std::abs(current.camera_angles[3]-prior.camera_angles[3])>.2f)
            grid_history_valid=false;
    }
    prior.grid_options[0]=grid_history_valid?1.f:0.f;
    context->UpdateSubresource(grid_constants.get(),0,nullptr,&current,0,0);
    context->UpdateSubresource(grid_previous_buffer.get(),0,nullptr,&prior,0,0);
    ID3D11Buffer* buffers[2]{grid_constants.get(),grid_previous_buffer.get()};
    auto* sampler=grid_sampler.get();
    auto* history_read=grid_history_srvs[grid_history_index].get();
    const auto next=(grid_history_index+1)%2u;
    auto* history_write=grid_history_rtvs[next].get();
    ID3D11ShaderResourceView* empty=nullptr;
    context->PSSetShaderResources(0,1,&empty);
    context->OMSetRenderTargets(1,&history_write,nullptr);
    const float transparent[4]{0,0,0,0};
    context->ClearRenderTargetView(history_write,transparent);
    context->IASetInputLayout(nullptr);
    context->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(grid_vs.get(),nullptr,0);
    context->PSSetShader(grid_temporal_ps.get(),nullptr,0);
    context->PSSetConstantBuffers(0,2,buffers);
    context->PSSetSamplers(0,1,&sampler);
    context->PSSetShaderResources(0,1,&history_read);
    context->OMSetDepthStencilState(grid_no_depth.get(),0);
    context->OMSetBlendState(nullptr,nullptr,0xffffffffu);
    context->Draw(3,0);
    context->PSSetShaderResources(0,1,&empty);

    auto* scene_target=render_target.get();
    context->OMSetRenderTargets(1,&scene_target,depth.get());
    auto* composite=grid_history_srvs[next].get();
    context->OMSetBlendState(grid_blend.get(),nullptr,0xffffffffu);
    context->PSSetShader(grid_composite_ps.get(),nullptr,0);
    context->PSSetShaderResources(0,1,&composite);
    context->Draw(3,0);
    context->PSSetShaderResources(0,1,&empty);
    context->OMSetBlendState(nullptr,nullptr,0xffffffffu);
    context->OMSetDepthStencilState(nullptr,0);
    grid_history_index=next;
    grid_history_valid=true;
    grid_previous_values=current;
}
}
