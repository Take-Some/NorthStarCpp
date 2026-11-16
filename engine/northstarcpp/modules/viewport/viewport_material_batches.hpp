#pragma once
#include "../shared/frame_memory_counters.hpp"

#include <northstarcpp/material.hpp>
#include <northstarcpp/math.hpp>
#include <northstarcpp/render_skinning_api.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace northstarcpp::viewport_detail {

inline NorthStarMaterialBatch material_batch(MaterialDefinition const& material)
{
    NorthStarMaterialBatch batch{};
    std::copy(material.base_color.begin(), material.base_color.end(), batch.base_color);
    batch.roughness = material.roughness;
    batch.metallic = material.metallic;
    batch.unlit = material.unlit ? 1u : 0u;
    for (unsigned slot = 0; slot < NORTHSTARCPP_RENDER_MATERIAL_TEXTURE_SLOTS; ++slot) {
        auto const& texture = material.textures[slot];
        if (!texture.rgba8.empty())
            batch.textures[slot] = {texture.rgba8.data(), texture.width, texture.height,
                static_cast<std::uint32_t>(texture.rgba8.size())};
    }
    return batch;
}

class SkinFrameBatches {
public:
    struct Palette {
        std::size_t offset;
        std::uint32_t joint_count;
    };

    void begin_frame()
    {
        draws_.clear();
        matrices_.clear();
        batches_.clear();
    }

    std::size_t draw_count() const { return draws_.size(); }

    Palette append_palette(std::vector<math::float4x4> const& joints,
                           math::float4x4 const& world)
    {
        const Palette palette{matrices_.size(), static_cast<std::uint32_t>(joints.size())};
        if (joints.empty())
            return palette;
        diagnostics::frame_memory::resize(matrices_,palette.offset + joints.size() * 16);
        auto* packed = matrices_.data() + palette.offset;
        for (auto const& joint : joints) {
            auto const combined = joint * world;
            for (auto const& row : combined.elements)
                packed = std::copy_n(row, 4, packed);
        }
        return palette;
    }

    // Use this pointer before the next palette append. Draws retain offsets
    // so growing the buffer for another actor cannot invalidate them.
    float const* palette_data(Palette palette) const
    {
        return palette.joint_count ? matrices_.data() + palette.offset : nullptr;
    }

    void add_draw(MaterialDefinition const& material,
                  std::vector<NorthStarSkinMaterialVertex> const& vertices,
                  Palette palette)
    {
        diagnostics::frame_memory::track_capacity(draws_,[&] {
            draws_.push_back({&material, &vertices, palette});
        });
    }

    // Resolve ABI pointers only after collecting the frame. The provider
    // consumes these views synchronously in set_batches.
    std::vector<NorthStarSkinnedMaterialBatch> const& build()
    {
        batches_.clear();
        diagnostics::frame_memory::reserve(batches_,draws_.size());
        for (auto const& draw : draws_) {
            NorthStarSkinnedMaterialBatch batch{};
            batch.material.material = material_batch(*draw.material);
            batch.material.opacity = 1.f;
            batch.material.alpha_cutoff = draw.material->alpha_cutoff;
            batch.vertices = draw.vertices->data();
            batch.vertex_count = static_cast<std::uint32_t>(draw.vertices->size());
            batch.palette = palette_data(draw.palette);
            batch.joint_count = draw.palette.joint_count;
            batches_.push_back(batch);
        }
        return batches_;
    }

private:
    struct Draw {
        MaterialDefinition const* material;
        std::vector<NorthStarSkinMaterialVertex> const* vertices;
        Palette palette;
    };
    std::vector<Draw> draws_;
    std::vector<float> matrices_;
    std::vector<NorthStarSkinnedMaterialBatch> batches_;
};

} // namespace northstarcpp::viewport_detail
