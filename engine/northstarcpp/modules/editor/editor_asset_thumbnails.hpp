// NorthStar Editor: actual decoded texture/material/mesh thumbnails.
#pragma once
#include <northstarcpp/shared_assets.hpp>
#include <cstdint>
#include <vector>
namespace northstarcpp::editor_tools {
struct Thumbnail {
    static constexpr int size=96;
    // BGRA DIB pixels, opaque alpha.
    std::vector<std::uint32_t> bgra;
    bool valid() const noexcept {return bgra.size()==size*size;}
};
Thumbnail thumbnail_texture(shared_assets::Texture const& texture);
Thumbnail thumbnail_drawable(shared_assets::Drawable const& drawable);
Thumbnail thumbnail_material(shared_assets::Material const& material);
// Large editor-only previews. Thumbnails keep their legacy 96px ABI.
// ARGB32 memory layout is compatible with Qt QImage::Format_ARGB32 on Win64.
struct AssetPreviewImage {
    int width=0, height=0;
    std::vector<std::uint32_t> argb;
    bool valid() const noexcept {
        return width>0 && height>0 &&
            argb.size()==std::size_t(width)*std::size_t(height);
    }
};
AssetPreviewImage preview_texture(
    shared_assets::Texture const& texture,int max_dimension=2048,
    std::size_t mip_level=0);
AssetPreviewImage preview_drawable(
    shared_assets::Drawable const& drawable,int width=720,int height=720,
    float yaw=0.6f,float pitch=0.3f,float zoom=1.f,
    bool wireframe=false);
// Studio-lit sphere with material parameters and decoded YTD bindings.
// Editor approximation: does not execute shader source / Vulkan RenderGraph.
AssetPreviewImage preview_material(
    shared_assets::Material const& material,
    std::vector<shared_assets::ResolvedAsset<shared_assets::Texture>> const& textures,
    int dimension=512,float yaw=0.f,float pitch=0.f,float zoom=1.f);


} // namespace northstarcpp::editor_tools
