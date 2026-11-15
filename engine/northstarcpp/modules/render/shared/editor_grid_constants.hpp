// Shared CPU packing for D3D11/D3D12 analytic grid cbuffer b0.
#pragma once
#include <northstarcpp/render_editor_grid_api.h>
#include <northstarcpp/render_plugin.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace northstarcpp::render {
struct alignas(16) EditorGridConstants {
    float eye_spacing[4];
    float camera_angles[4];
    float screen_fade[4];
    float grid_options[4];
};
static_assert(sizeof(EditorGridConstants)==64);
inline void validate_editor_grid(NorthStarRenderEditorGridSettings const* s) {
    if(!s)return;
    if(s->abi_version!=NORTHSTARCPP_RENDER_EDITOR_GRID_ABI ||
       s->reserved!=0 || s->enabled>1 ||
       !std::isfinite(s->minor_step) || s->minor_step<=0.f ||
       !std::isfinite(s->opacity) || s->opacity<0.f || s->opacity>1.f ||
       !std::isfinite(s->fade_start) || s->fade_start<0.f ||
       !std::isfinite(s->fade_end) || s->fade_end<=s->fade_start)
        throw std::invalid_argument("Invalid editor grid configuration");
}
inline EditorGridConstants grid_constants(
    NorthStarRenderFrame const& f,NorthStarRenderEditorGridSettings const& s,
    unsigned width,unsigned height) {
    constexpr float radians=.01745329251994329577f;
    return {
        {f.camera_x,f.camera_y,f.camera_z,s.minor_step},
        {f.camera_yaw,f.camera_pitch,f.camera_roll,
         f.camera_fov_y_degrees*radians},
        {float(width),float(height),s.fade_start,s.fade_end},
        {s.opacity,5.f,float(s.frame_index%1024u),0.f}
    };
}
}
