// D3D12 lowering of the typed backend-neutral Render Graph usage plan.
// The compiler derives access transitions; this executor binds logical
// resources and converts declared usages into D3D12 legacy states.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
namespace {
D3D12_RESOURCE_STATES d3d12_state(northstarcpp::render::ResourceAccess access) {
    using northstarcpp::render::ResourceAccess;
    switch(access) {
    case ResourceAccess::ShaderRead:return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case ResourceAccess::ColorAttachment:return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case ResourceAccess::DepthWrite:return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case ResourceAccess::DepthRead:return D3D12_RESOURCE_STATE_DEPTH_READ;
    case ResourceAccess::CopySource:return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceAccess::CopyDestination:return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceAccess::Present:return D3D12_RESOURCE_STATE_PRESENT;
    default:throw std::logic_error("Cannot lower undefined graph access to D3D12");
    }
}
}
void RendererState::apply_graph_resource_states(
    const northstarcpp::render::ScheduledPass& pass,FramePassContext& ctx) {
    auto& states=ctx.states;
    auto* command=commands.Get();
    for(const auto& request:pass.transitions) {
        const auto resource=static_cast<FrameResource>(request.resource);
        ID3D12Resource* target=nullptr;
        switch(resource) {
        case FrameResource::Shadow:
            if(shadows_enabled && ctx.has_deferred_geometry)
                target=sun_shadow_map.Get();
            break;
        case FrameResource::GBufferAlbedo:
            if(ctx.has_deferred_geometry)target=gbuffer[0].Get();
            break;
        case FrameResource::GBufferNormal:
            if(ctx.has_deferred_geometry) {
                target=gbuffer[1].Get();
                if(transient_alias.enabled())
                    transient_alias.activate(command,request.resource);
            }
            break;
        case FrameResource::GBufferMaterial:
            if(ctx.has_deferred_geometry)target=gbuffer[2].Get();
            break;
        case FrameResource::GBufferAuxiliary:
            if(gbuffer_v2 && ctx.has_deferred_geometry)target=gbuffer[3].Get();
            break;
        case FrameResource::Depth:target=depth.Get();break;
        case FrameResource::Scene:
            // Without PostFX there is no scene SRV; scene color is the
            // swapchain attachment and remains RTV throughout the frame.
            if(!postfx_enabled() &&
               pass.id>=static_cast<northstarcpp::render::PassId>(FrameStage::Bloom))
                break;
            target=postfx_enabled()?
                (hdr_enabled?hdr_scene_color.Get():scene_color.Get()):
                backs[ctx.index].Get();
            break;
        case FrameResource::Output:target=backs[ctx.index].Get();break;
        case FrameResource::Bloom:
            if(bloom_enabled) {
                target=bloom_surface.Get();
                if(transient_alias.enabled())
                    transient_alias.activate(command,request.resource);
            }
            break;
        case FrameResource::Exposure:
            if(hdr_enabled)
                target=hdr_exposure[exposure_frame_index&1u].Get();
            break;
        case FrameResource::ToneMapped:
            // Tone mapping is skipped in LDR, but the final composite still
            // reads the already-shader-readable LDR scene target.
            if(postfx_enabled() &&
               (hdr_enabled ||
                pass.id>=static_cast<northstarcpp::render::PassId>(FrameStage::Composite)))
                target=scene_color.Get();
            break;
        default:throw std::logic_error("Unknown typed D3D12 graph resource ID");
        }
        if(target)
            states.transition(command,target,d3d12_state(request.after));
    }
}
}
