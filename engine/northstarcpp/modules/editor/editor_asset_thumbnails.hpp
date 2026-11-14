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
} // namespace northstarcpp::editor_tools
