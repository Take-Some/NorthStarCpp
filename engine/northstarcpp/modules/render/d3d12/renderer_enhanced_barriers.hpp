#pragma once
// D3D12 Enhanced Barrier lowering from backend-neutral RenderGraph usages.
// Capability-checked optional path. Existing production frame recorder still
// uses legacy barriers until state interop/device-loss QA is completed.
#include "../shared/render_graph.hpp"
#include <d3d12.h>
#include <stdexcept>

namespace northstarcpp::render::d3d12::detail {
struct EnhancedUsage {
    D3D12_BARRIER_SYNC sync;
    D3D12_BARRIER_ACCESS access;
    D3D12_BARRIER_LAYOUT layout;
};
inline EnhancedUsage enhanced_usage(northstarcpp::render::ResourceAccess type) {
    using northstarcpp::render::ResourceAccess;
    switch(type) {
    case ResourceAccess::Undefined:
        return {D3D12_BARRIER_SYNC_NONE,D3D12_BARRIER_ACCESS_NO_ACCESS,
                D3D12_BARRIER_LAYOUT_UNDEFINED};
    case ResourceAccess::ShaderRead:
        return {D3D12_BARRIER_SYNC_ALL_SHADING,D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE};
    case ResourceAccess::ColorAttachment:
        return {D3D12_BARRIER_SYNC_RENDER_TARGET,D3D12_BARRIER_ACCESS_RENDER_TARGET,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET};
    case ResourceAccess::DepthWrite:
        return {D3D12_BARRIER_SYNC_DEPTH_STENCIL,
                D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
                D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE};
    case ResourceAccess::DepthRead:
        return {D3D12_BARRIER_SYNC_DEPTH_STENCIL,
                D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ,
                D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ};
    case ResourceAccess::CopySource:
        return {D3D12_BARRIER_SYNC_COPY,D3D12_BARRIER_ACCESS_COPY_SOURCE,
                D3D12_BARRIER_LAYOUT_COPY_SOURCE};
    case ResourceAccess::CopyDestination:
        return {D3D12_BARRIER_SYNC_COPY,D3D12_BARRIER_ACCESS_COPY_DEST,
                D3D12_BARRIER_LAYOUT_COPY_DEST};
    case ResourceAccess::Present:
        return {D3D12_BARRIER_SYNC_NONE,D3D12_BARRIER_ACCESS_NO_ACCESS,
                D3D12_BARRIER_LAYOUT_PRESENT};
    }
    throw std::logic_error("Unknown enhanced resource access");
}
inline bool enhanced_barriers_supported(ID3D12Device* device) {
    if(!device)return false;
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
    return SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12,
        &options,sizeof(options))) && options.EnhancedBarriersSupported;
}
inline D3D12_TEXTURE_BARRIER enhanced_texture_barrier(
    ID3D12Resource* image, const northstarcpp::render::AccessTransition& transition) {
    if(!image||image->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        throw std::invalid_argument("Enhanced barrier requires a GPU texture");
    const auto before=enhanced_usage(transition.before);
    const auto after=enhanced_usage(transition.after);
    D3D12_TEXTURE_BARRIER out{};
    out.SyncBefore=before.sync;
    out.SyncAfter=after.sync;
    out.AccessBefore=before.access;
    out.AccessAfter=after.access;
    out.LayoutBefore=before.layout;
    out.LayoutAfter=after.layout;
    out.pResource=image;
    // NumMipLevels == 0 selects subresource-index addressing;
    // UINT32_MAX means all subresources (mips, array layers and planes).
    out.Subresources.IndexOrFirstMipLevel=UINT32_MAX;
    out.Subresources.NumMipLevels=0;
    out.Flags=transition.before==northstarcpp::render::ResourceAccess::Undefined?
        D3D12_TEXTURE_BARRIER_FLAG_DISCARD:D3D12_TEXTURE_BARRIER_FLAG_NONE;
    return out;
}
}
