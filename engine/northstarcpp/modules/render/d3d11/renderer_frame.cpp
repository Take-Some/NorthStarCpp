// D3D11 frame methods; independently compiled provider implementation.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d11::detail {
void RendererState::draw(NorthStarRenderFrame const& frame){
        RECT client{};
        GetClientRect(window, &client);
        if (client.right <= 0 || client.bottom <= 0)
            return;
        resize(client.right, client.bottom);

        vertices.clear();
        for (int n = -10; !frame.first_person && !frame.camera_override && n <= 10; ++n) {
            auto const i = static_cast<float>(n);
            float const tone = n == 0 ? 0.38f : 0.23f;
            vertices.push_back({i, -0.01f, -10.f, tone, tone, tone});
            vertices.push_back({i, -0.01f, 10.f, tone, tone, tone});
            vertices.push_back({-10.f, -0.01f, i, tone, tone, tone});
            vertices.push_back({10.f, -0.01f, i, tone, tone, tone});
        }
        if (frame.count > northstarcpp::max_scene_objects || (frame.count && !frame.cubes))
            throw std::runtime_error("Invalid render cube buffer");
        using namespace northstarcpp::math;
        float3 const focus(frame.camera_x, frame.camera_y, frame.camera_z);
        auto const direction = float3(std::sin(frame.camera_yaw) * std::cos(frame.camera_pitch),
            std::sin(frame.camera_pitch), std::cos(frame.camera_yaw) * std::cos(frame.camera_pitch));
        if (frame.camera_override &&
            (!std::isfinite(frame.camera_yaw) ||
             !std::isfinite(frame.camera_pitch) ||
             !std::isfinite(frame.camera_roll) ||
             !std::isfinite(frame.camera_fov_y_degrees) ||
             !std::isfinite(frame.camera_near_clip) ||
             !std::isfinite(frame.camera_far_clip) ||
             frame.camera_fov_y_degrees < 25.f ||
             frame.camera_fov_y_degrees > 120.f ||
             frame.camera_near_clip < 0.01f ||
             frame.camera_far_clip <= frame.camera_near_clip + 1.f))
            throw std::runtime_error("Invalid CameraDirector render frame");
        auto const right = float3(std::cos(frame.camera_yaw), 0.f,
                                  -std::sin(frame.camera_yaw));
        auto const base_up = direction ^ right;
        auto const up = frame.camera_override
            ? base_up * std::cos(frame.camera_roll) +
              right * std::sin(frame.camera_roll)
            : float3(0.f, 1.f, 0.f);
        auto const view = frame.camera_override || frame.first_person
            ? create_camera_at(focus, focus + direction, up)
            : create_camera_at(focus + float3(8.f, 7.f, -10.f), focus,
                               {0.f, 1.f, 0.f});
        constexpr float rad_per_degree = 3.14159265358979323846f / 180.f;
        auto const fov = frame.camera_override
            ? frame.camera_fov_y_degrees * rad_per_degree : pi_d4;
        auto const projection = create_projection(fov,
            static_cast<float>(height) / width,
            frame.camera_override ? frame.camera_near_clip : 0.1f,
            frame.camera_override ? frame.camera_far_clip :
                (std::isfinite(frame.camera_far_clip) && frame.camera_far_clip>250.f &&
                 frame.camera_far_clip<=20000.f ? frame.camera_far_clip : 250.f));
        auto const view_projection = mul4x4(view, projection);
        auto const eye=frame.camera_override||frame.first_person?
            focus:focus+float3(8.f,7.f,-10.f);
        struct alignas(16) MatCamera {
            decltype(view_projection) matrix;
            float eye[4];
        } const mat_camera{view_projection,{eye.x,eye.y,eye.z,1.f}};
        static_assert(sizeof(MatCamera)==80);
        context->UpdateSubresource(authored_camera.get(),0,nullptr,
            &mat_camera,0,0);
        frustum const visible_region(view_projection, clip_depth::zero_to_one);
        for (std::uint32_t i = 0; i < frame.count; ++i) {
            auto const& cube = frame.cubes[i];
            auto const* axes = cube.rotation;
            float const half = cube.size * 0.5f;
            float4x4 transform;
            transform.i = float4(axes[0] * half, axes[1] * half, axes[2] * half, 0.f);
            transform.j = float4(axes[3] * half, axes[4] * half, axes[5] * half, 0.f);
            transform.k = float4(axes[6] * half, axes[7] * half, axes[8] * half, 0.f);
            transform.c = float4(cube.x, cube.y + half, cube.z, 1.f);
            aabb bounds;
            bounds.min = {-1.f, -1.f, -1.f};
            bounds.max = {1.f, 1.f, 1.f};
            bounds.modify(transform);
            // Leave a margin for CPU and GPU rounding at the clip planes.
            bounds += 0.05f;
            if (visible_region.test_inexact(bounds) == intersection_outside)
                continue;
            auto const first=vertices.size();
            add_cube(vertices, transform, static_cast<std::int32_t>(i) == frame.selected ? 0xffd474u : cube.rgb);
            if(i<surface_ids.size() && surface_ids[i] &&
               surface_views[(surface_ids[i]-1)*3].get()) {
                auto const category=float(surface_ids[i]);
                for(std::size_t v=first;v<vertices.size();++v)
                    vertices[v].surface=category;
            }
        }
        if (mesh_triangles.size()>max_vertex_capacity-vertices.size())
            throw std::runtime_error("Mesh geometry exceeds transient GPU budget");
        vertices.insert(vertices.end(),mesh_triangles.begin(),mesh_triangles.end());
        context->UpdateSubresource(camera.get(), 0, nullptr, &view_projection, 0, 0);
        ensure_vertex_capacity(vertices.size());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
            "Map viewport vertices");
        std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(Vertex));
        context->Unmap(vertex_buffer.get(), 0);

        auto* target = render_target.get();
        context->OMSetRenderTargets(1, &target, depth.get());
        float const clear[]{0.055f, 0.075f, 0.105f, 1.f};
        context->ClearRenderTargetView(target, clear);
        context->ClearDepthStencilView(depth.get(), D3D11_CLEAR_DEPTH, 1.f, 0);
        D3D11_VIEWPORT const viewport{0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
        context->RSSetViewports(1, &viewport);
        context->RSSetState(rasterizer.get());
        // Background pass runs before world geometry, using the actual YDD mesh,
        // native YMAT material tint and YTD texture. Sky never writes depth.
        if (sky_index_count)
            draw_sky(direction,up,projection);
        context->UpdateSubresource(camera.get(),0,nullptr,&view_projection,0,0);
        draw_editor_grid(frame);

        UINT const stride = sizeof(Vertex), offset = 0;
        auto* buffer = vertex_buffer.get();
        auto* constants = camera.get();
        context->IASetInputLayout(layout.get());
        context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
        context->VSSetShader(vertex_shader.get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &constants);
        context->PSSetShader(pixel_shader.get(), nullptr, 0);
        // t0 remains reserved for the independent sky shader. Binding
        // all nine terrain material maps allows the shared world pixel
        // shader to select sand/rock/forest without per-object draw calls.
        ID3D11ShaderResourceView* world_views[9]{};
        for(unsigned i=0;i<9;++i)world_views[i]=surface_views[i].get();
        context->PSSetShaderResources(1,9,world_views);
        auto* sampler=sky_sampler.get();
        context->PSSetSamplers(0,1,&sampler);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        if (!frame.first_person && !frame.camera_override) {
            context->Draw(grid_vertices, 0);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(static_cast<UINT>(vertices.size()) - grid_vertices, grid_vertices);
        } else {
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(static_cast<UINT>(vertices.size()), 0);
        }
        draw_authored_materials();
        draw_skinned_materials();
        draw_character();
        if (frame.first_person && frame.player_max_health != 0u) {
            context->IASetInputLayout(layout.get());
            context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
            context->VSSetShader(vertex_shader.get(),nullptr,0);
            context->PSSetShader(pixel_shader.get(),nullptr,0);
            draw_fps_overlay(frame);
        }

        if (frame.verify_pixels)
            verify_frame(frame.verify_pixels);
        check(swap_chain->Present(1, 0), "Present viewport frame");
    }

}
