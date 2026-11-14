#pragma once

#include <array>

namespace northstarcpp::ui {

struct QuadCorner {
    float x, y, u, v;
};

// Shared corner order used by ui_image. Inputs retain their coordinate system.
inline std::array<QuadCorner, 4> quad_corners(
    float x0, float y0, float x1, float y1,
    float u0 = 0.f, float v0 = 0.f, float u1 = 1.f, float v1 = 1.f)
{
    return {{{x0, y1, u0, v1},
             {x0, y0, u0, v0},
             {x1, y1, u1, v1},
             {x1, y0, u1, v0}}};
}

// Emit the original two triangles as a list for the current HUD pipelines.
inline constexpr std::array<unsigned, 6> quad_triangle_corners{1, 3, 2, 1, 2, 0};

} // namespace northstarcpp::ui
