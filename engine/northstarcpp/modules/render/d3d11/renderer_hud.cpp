// D3D11 hud methods; independently compiled provider implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d11::detail {
void RendererState::draw_fps_overlay(NorthStarRenderFrame const& frame){
        // A provider-neutral FPS HUD, independent of depth and world lighting.
        // Primitive clips are stable at every viewport resolution; weapon recoil
        // shifts only the presentation model, never the hitscan ray origin.
        float const hx = 2.f / static_cast<float>(width);
        float const hy = 2.f / static_cast<float>(height);
        std::vector<Vertex> lines;
        std::vector<Vertex> triangles;
        lines.reserve(12);
        triangles.reserve(270);
        auto rect = [&](float x0, float y0, float x1, float y1,
                        float r, float g, float b) {
            auto const corners = northstarcpp::ui::quad_corners(x0, y0, x1, y1);
            for (auto index : northstarcpp::ui::quad_triangle_corners) {
                auto const& corner = corners[index];
                triangles.push_back({corner.x, corner.y, 0.f, r, g, b});
            }
        };
        auto segment = [&](float x0, float y0, float x1, float y1) {
            constexpr float r = 0.38f, g = 0.94f, b = 0.88f;
            lines.push_back({x0, y0, 0.f, r, g, b});
            lines.push_back({x1, y1, 0.f, r, g, b});
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
        using namespace northstarcpp::math;
        float4x4 identity{float4(1.f,0.f,0.f,0.f),float4(0.f,1.f,0.f,0.f),
                          float4(0.f,0.f,1.f,0.f),float4(0.f,0.f,0.f,1.f)};
        context->UpdateSubresource(camera.get(),0,nullptr,&identity,0,0);
        auto* target=render_target.get();
        context->OMSetRenderTargets(1,&target,nullptr);
        auto batch=[&](std::vector<Vertex> const& values,
                       D3D11_PRIMITIVE_TOPOLOGY topology) {
            if (values.empty()) return;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check(context->Map(vertex_buffer.get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),
                  "Map FPS HUD vertices");
            std::memcpy(mapped.pData,values.data(),values.size()*sizeof(Vertex));
            context->Unmap(vertex_buffer.get(),0);
            context->IASetPrimitiveTopology(topology);
            context->Draw(static_cast<UINT>(values.size()),0);
        };
        batch(triangles,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        batch(lines,D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }

}
