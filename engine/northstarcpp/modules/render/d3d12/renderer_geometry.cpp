// Native CPU geometry implementation for d3d12.
#include "renderer_geometry.hpp"
#include <northstarcpp/geometry.hpp>
#include <ui_quad.h>
#include <algorithm>
#include <cmath>
namespace northstarcpp::render::d3d12::geometry {
void make_hud(NorthStarRenderFrame const& frame,unsigned width,unsigned height,
              std::vector<Vertex>& hud_triangles,std::vector<Vertex>& hud_lines) {
        float const hx = 2.f / static_cast<float>(width);
        float const hy = 2.f / static_cast<float>(height);
        hud_lines.reserve(12);
        hud_triangles.reserve(270);
        auto rect = [&](float x0, float y0, float x1, float y1,
                        float r, float g, float b) {
            auto const corners = northstarcpp::ui::quad_corners(x0, y0, x1, y1);
            for (auto index : northstarcpp::ui::quad_triangle_corners) {
                auto const& corner = corners[index];
                hud_triangles.push_back({corner.x, corner.y, 0.f, r, g, b});
            }
        };
        auto segment = [&](float x0, float y0, float x1, float y1) {
            constexpr float r = 0.38f, g = 0.94f, b = 0.88f;
            hud_lines.push_back({x0, y0, 0.f, r, g, b});
            hud_lines.push_back({x1, y1, 0.f, r, g, b});
        };
        // Center reticle with a clear space around the point of aim.
        segment(-16 * hx, 0.f, -5 * hx, 0.f);
        segment(5 * hx, 0.f, 16 * hx, 0.f);
        segment(0.f, -16 * hy, 0.f, -5 * hy);
        segment(0.f, 5 * hy, 0.f, 16 * hy);
        // Health bar and ammo marks; colors are GPU-side, no GDI over swap chain.
        rect(-0.94f,0.83f,-0.36f,0.89f,0.07f,0.13f,0.18f);
        float const health = frame.player_max_health ?
            std::clamp(float(frame.player_health)/frame.player_max_health,0.f,1.f) : 0.f;
        rect(-0.93f,0.84f,-0.93f+0.56f*health,0.88f,
             health < 0.26f ? 0.95f : 0.14f,
             health < 0.26f ? 0.22f : 0.87f,
             health < 0.26f ? 0.21f : 0.58f);
        rect(0.30f,-0.89f,0.94f,-0.82f,0.07f,0.13f,0.18f);
        auto const capacity = std::min(frame.magazine_capacity, 32u);
        for (uint32_t i=0; i<capacity; ++i) {
            float const start = 0.315f + i*(0.60f/static_cast<float>(capacity));
            bool const filled = i<frame.ammunition;
            rect(start,-0.878f,start+0.60f/capacity*0.7f,-0.832f,
                 filled ? 0.96f : 0.18f, filled ? 0.71f : 0.24f, filled ? 0.36f : 0.29f);
        }
        // The initial weapon is a stylized low-poly HUD model, not a 3D rig.
        // It visibly lifts on each shot and returns with the recoil spring.
        float const kick = std::clamp(frame.weapon_recoil,0.f,1.6f)*0.075f;
        rect(0.38f,-1.f+kick,0.76f,-0.59f+kick,0.09f,0.17f,0.22f);
        rect(0.40f,-0.72f+kick,0.79f,-0.64f+kick,0.17f,0.31f,0.36f);
        rect(0.49f,-0.65f+kick,0.71f,-0.49f+kick,0.23f,0.41f,0.43f);
        rect(0.54f,-0.51f+kick,0.66f,-0.39f+kick,0.11f,0.23f,0.27f);
        rect(0.55f,-0.40f+kick,0.65f,-0.37f+kick,0.29f,0.69f,0.68f);
        // Muted menu backdrop allows native Win32 buttons to remain accessible.
        if (frame.overlay_paused)
            rect(-0.56f,-0.56f,0.56f,0.56f,0.035f,0.075f,0.105f);
}

void cube(std::vector<Vertex>& vertices,const NorthStarRenderCube& c,bool selected) {
    auto const mesh=northstarcpp::geometry::primitive(northstarcpp::geometry::SolidPrimitive::cube);
    constexpr float shade[]={.74f,1.f,.60f,.82f,.95f,.68f};
    auto rgb=selected?0xffd474u:c.rgb;
    float r=float((rgb>>16)&255)/255.f,g=float((rgb>>8)&255)/255.f,b=float(rgb&255)/255.f;
    float h=c.size*.5f;
    for(size_t t=0;t<mesh.index_count;t+=3) {
        auto a=mesh.positions+3*mesh.indices[t],p=mesh.positions+3*mesh.indices[t+1],
            q=mesh.positions+3*mesh.indices[t+2];
        unsigned face=a[1]==p[1]&&a[1]==q[1]?(a[1]>0?1:0):
            a[2]==p[2]&&a[2]==q[2]?(a[2]>0?4:2):(a[0]>0?3:5);
        for(int j=0;j<3;++j) {
            auto v=mesh.positions+3*mesh.indices[t+j];
            float x=v[0]*h,y=v[1]*h,z=v[2]*h;
            vertices.push_back({c.x+c.rotation[0]*x+c.rotation[1]*y+c.rotation[2]*z,
                c.y+h+c.rotation[3]*x+c.rotation[4]*y+c.rotation[5]*z,
                c.z+c.rotation[6]*x+c.rotation[7]*y+c.rotation[8]*z,
                r*shade[face],g*shade[face],b*shade[face]});
        }
    }
}
} // namespace northstarcpp::render::d3d12::geometry
