#pragma once
// Frame-to-frame tiled-light classification cache. A change in ANY light,
// view matrix, eye or viewport invalidates the conservative light lists.
// Owns data; no host pointers survive engine.render ABI calls.
#include "tiled_lights.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace northstarcpp::render {
class TileLightCache final {
public:
    // True means GPU upload must be refreshed; false means both the
    // classification AND the existing persistent GPU light index buffer
    // can be reused. Call invalidate() when GPU resources are recreated.
    bool update(std::uint32_t width,std::uint32_t height,
                std::array<float,16> const& inverse,
                std::array<float,3> const& eye,
                NorthStarRenderLight const* lights,std::uint32_t count) {
        if(valid_ && width_==width && height_==height && inverse_==inverse &&
           eye_==eye && lights_.size()==count && equal_lights(lights,count)) {
            ++hits_;
            return false;
        }
        // Preserve previous cache after an invalid input throws. Allocation
        // failures must not create a valid-looking partial light grid.
        auto built=build_tile_lights(width,height,inverse,eye,lights,count);
        std::vector<NorthStarRenderLight> copied;
        if(count)copied.assign(lights,lights+count);
        grid_=std::move(built);
        lights_=std::move(copied);
        inverse_=inverse;eye_=eye;width_=width;height_=height;
        valid_=true;
        ++misses_;
        return true;
    }
    void invalidate() noexcept {valid_=false;}
    TileLightGrid const& grid() const noexcept {return grid_;}
    std::uint64_t hits() const noexcept {return hits_;}
    std::uint64_t misses() const noexcept {return misses_;}
private:
    bool equal_lights(NorthStarRenderLight const* lights,std::uint32_t count) const {
        if(!count)return true;
        if(!lights)return false;
        for(std::uint32_t i=0;i<count;++i) {
            auto const& a=lights_[i];
            auto const& b=lights[i];
            for(int j=0;j<4;++j)
                if(a.position_range[j]!=b.position_range[j] ||
                   a.color_intensity[j]!=b.color_intensity[j] ||
                   a.direction_cone[j]!=b.direction_cone[j])
                    return false;
        }
        return true;
    }
    bool valid_=false;
    std::uint32_t width_=0,height_=0;
    std::array<float,16> inverse_{};
    std::array<float,3> eye_{};
    std::vector<NorthStarRenderLight> lights_;
    TileLightGrid grid_;
    std::uint64_t hits_=0,misses_=0;
};
} // namespace northstarcpp::render
