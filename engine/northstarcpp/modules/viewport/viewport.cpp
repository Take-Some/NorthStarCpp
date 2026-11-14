// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include "viewport.hpp"
#include <northstarcpp/scene_world.hpp>

#include <northstarcpp/physics.hpp>
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/procedural_environment.hpp>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/assets.hpp>
#include "module_loader.hpp"

#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

namespace northstarcpp {

struct Viewport::State {
    State(HWND window, bool software)
    {
        renderer = api.create(window, software ? 1u : 0u);
        if (!renderer)
            throw module.error("Cannot create viewport");
        triangle_api = module.query_extension<NorthStarRenderTrianglesApi>(
            NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI);
        if (!triangle_api) {
            auto const query = module.symbol<NorthStarRenderGetTrianglesApi>(
                "northstar_render_get_triangles_api");
            auto const* candidate = query ? query(NORTHSTARCPP_RENDER_TRIANGLES_ABI) : nullptr;
            if (candidate && valid_api(*candidate))
                triangle_api = candidate;
        }
        try {
            cubes.reserve(max_scene_objects);
        } catch (...) {
            api.destroy(renderer);
            throw;
        }
    }

    ~State() { api.destroy(renderer); }

    Module module{NORTHSTARCPP_CAPABILITY_RENDER};
    NorthStarRenderApi const& api = *module.query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI);
    std::vector<NorthStarRenderCube> cubes;
    std::unique_ptr<AssetStore> project_assets;
    std::filesystem::path source_project_root;
    std::unordered_map<std::string,MeshAsset> cached_meshes;
    std::vector<NorthStarRenderTriangleVertex> mesh_vertices;
    NorthStarRenderTrianglesApi const* triangle_api = nullptr;
    void* renderer = nullptr;
};

Viewport::Viewport(HWND window, bool software) : state(std::make_unique<State>(window, software)) {}
Viewport::~Viewport() = default;

void Viewport::set_asset_sources(std::filesystem::path const& root,
                                 std::filesystem::path const& archive,
                                 std::filesystem::path const& archive_data)
{
    auto assets=std::make_unique<AssetStore>();
    if (!archive.empty())
        assets->mount_archive("",archive,archive_data.empty()?archive:archive_data);
    else
        assets->mount_directory("",root);
    state->project_assets=std::move(assets);
    state->source_project_root=archive.empty() ? std::filesystem::absolute(root) :
        std::filesystem::path{};
    state->cached_meshes.clear();
    state->mesh_vertices.clear();
}


void Viewport::set_sky(shared_assets::DrawableResources const& resources)
{
    if (resources.drawable.meshes.size() != 1 || resources.materials.empty())
        throw std::runtime_error("Skydome requires one authored mesh and its YMAT material");
    auto const& mesh = resources.drawable.meshes.front();
    auto const& material = resources.materials.front().asset;
    auto const binding = std::find_if(material.textures.begin(), material.textures.end(),
        [](shared_assets::TextureBinding const& tex) {return tex.slot == "base_color";});
    if (binding == material.textures.end())
        throw std::runtime_error("Skydome material has no base_color texture");
    auto const found = std::find_if(resources.textures.begin(), resources.textures.end(),
        [&](auto const& item) {return item.reference == binding->reference;});
    if (found == resources.textures.end())
        throw std::runtime_error("Skydome base_color texture is unresolved");
    auto const& texture = found->asset;
    if (texture.format_id != 1 || texture.mips.empty() ||
        texture.mips.front().size != static_cast<std::size_t>(texture.mips.front().width) *
            texture.mips.front().height * 4)
        throw std::runtime_error("Skydome requires native RGBA8 base_color mip");
    auto const& mip = texture.mips.front();
    if (mip.offset > texture.data.size() ||
        mip.size > texture.data.size() - mip.offset)
        throw std::runtime_error("Invalid skydome texture mip data");
    std::vector<NorthStarSkyVertex> verts;
    verts.reserve(mesh.vertices.size());
    for (auto const& value : mesh.vertices)
        verts.push_back({value.position[0], value.position[1], value.position[2],
            value.uv[0], value.uv[1]});
    float tint[3]{1.f,1.f,1.f};
    for (auto const& param : material.parameters)
        if (param.name == "base_color" && param.values.size() >= 3)
            for (std::size_t axis = 0; axis < 3; ++axis) tint[axis] = param.values[axis];
    // Only the specific Perlin noise texture is procedural. Other skydome
    // materials continue using their decoded native YTD base-color data.
    std::vector<std::uint8_t> procedural_clouds;
    auto const* pixels=texture.data.data() + mip.offset;
    auto width=mip.width, height=mip.height;
    if (binding->reference == "textures/skydome.ytd@baseperlinnoise3channel") {
        CloudNoiseSettings settings;
        settings.seed=2026;
        settings.width=width;
        settings.height=height;
        procedural_clouds=make_perlin_cloud_rgba(settings);
        pixels=procedural_clouds.data();
    }
    NorthStarSky const sky{verts.data(), static_cast<std::uint32_t>(verts.size()),
        mesh.indices.data(), static_cast<std::uint32_t>(mesh.indices.size()),
        pixels, width, height,
        tint[0],tint[1],tint[2]};
    if (!state->api.set_sky(state->renderer, &sky))
        throw state->module.error("Cannot initialize texture-mapped skydome");
}


void Viewport::draw(Scene const& scene, int selected, bool verify_pixels, PhysicsScene const* physics,
                    FpsArena const* fps, bool paused, CameraPose const* camera_pose,
                    SceneWorld const* scene_world, bool show_fps_overlay,
                    bool show_streaming_debug, ViewportGizmo const* gizmo)
{
    auto& cubes = state->cubes;
    cubes.clear();
    auto& mesh_vertices=state->mesh_vertices;
    mesh_vertices.clear();
    auto focus = Vector3(0.f, 0.f, 0.f);
    int selected_render_index = -1;
    bool const streaming = scene_world && scene_world->update_generation() > 0 &&
        scene_world->source().objects.size() == scene.objects.size();
    if (streaming) {
        // Per-scene CPU mesh storage must obey verified cell residency.
        // The D3D11 provider uploads the currently submitted triangle list
        // each frame; it does not own persistent per-entity OBJ GPU handles.
        // Drop cached OBJ meshes not used by the current visible frame, while
        // retaining the selected item in editor authoring mode.
        std::unordered_set<std::string> active_meshes;
        auto include_mesh=[&](std::size_t i) {
            auto const& path=scene.objects[i].node.asset;
            if(path.size()>=4 && path.compare(path.size()-4,4,".obj")==0)
                active_meshes.insert(path);
        };
        for(auto const i:scene_world->visible_indices())include_mesh(i);
        if(selected>=0 && static_cast<std::size_t>(selected)<scene.objects.size())
            include_mesh(static_cast<std::size_t>(selected));
        for(auto it=state->cached_meshes.begin();it!=state->cached_meshes.end();) {
            if(!active_meshes.count(it->first))
                it=state->cached_meshes.erase(it);
            else ++it;
        }
    }
    for (std::size_t i = 0; i < scene.objects.size(); ++i) {
        // Keep the selected editor entity editable even when HLOD substitutes
        // its authored draw; the proxy is still allowed behind selection.
        if (streaming && !scene_world->object_visible(i) &&
            (selected<0 || i!=static_cast<std::size_t>(selected)))
            continue;
        auto const& object = scene.objects[i];
        if ((fps || (camera_pose && selected==-2)) && object.player) continue;
        auto const& position = object.position;
        NorthStarRenderCube cube{position.x, position.y, position.z, object.size, object.color,
            {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f}};
        if (physics) {
            auto const rotation = physics->orientation(i);
            for (unsigned axis = 0; axis < 3; ++axis)
                for (unsigned component = 0; component < 3; ++component)
                    cube.rotation[axis * 3 + component] = rotation.elements[axis][component];
        }
        if (scene.version == 3) {
            // Static authored transforms use Euler XYZ. Dynamic bodies retain
            // their provider-authoritative physics orientation.
            if (!physics || object.mass == 0.f) {
                constexpr float radians = 0.01745329251994329577f;
                float const x = object.node.rotation.x * radians;
                float const y = object.node.rotation.y * radians;
                float const z = object.node.rotation.z * radians;
                float const sx = std::sin(x), cx = std::cos(x);
                float const sy = std::sin(y), cy = std::cos(y);
                float const sz = std::sin(z), cz = std::cos(z);
                float const basis[9] = {
                    cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
                    sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
                    -sy, cy * sx, cy * cx
                };
                std::copy(std::begin(basis), std::end(basis),
                          std::begin(cube.rotation));
            }
            float const scale[3] = {object.node.scale.x, object.node.scale.y,
                                    object.node.scale.z};
            for (unsigned row = 0; row != 3; ++row)
                for (unsigned column = 0; column != 3; ++column)
                    cube.rotation[row * 3 + column] *= scale[column];
        }
        // The optional geometry ABI lets scene assets render as actual meshes.
        // Existing renderer providers keep the previous primitive-cube fallback.
        if (state->triangle_api && state->project_assets &&
            object.node.asset.rfind("assets/models/",0)==0 &&
            object.node.asset.size()>4 &&
            object.node.asset.compare(object.node.asset.size()-4,4,".obj")==0) {
            auto found=state->cached_meshes.find(object.node.asset);
            if (found==state->cached_meshes.end()) {
                // Directory-backed projects must not follow symlink/junction
                // redirections when loading authored mesh assets.
                if (!state->source_project_root.empty()) {
                    auto cursor=state->source_project_root;
                    for (auto const& part:std::filesystem::u8path(object.node.asset)) {
                        if (part=="." || part==".." || part.is_absolute())
                            throw std::invalid_argument("Unsafe mesh virtual path");
                        cursor/=part;
                        if (std::filesystem::is_symlink(cursor))
                            throw std::invalid_argument("Redirected OBJ mesh asset");
                    }
                }
                auto model=load_obj_mesh(*state->project_assets,object.node.asset);
                found=state->cached_meshes.emplace(object.node.asset,std::move(model)).first;
            }
            auto const& mesh=found->second;
            if (mesh_vertices.size()+mesh.indices.size()>300000)
                throw std::runtime_error("Scene mesh triangle budget exceeded");
            auto const color=static_cast<int>(i)==selected ? 0xffd474u : object.color;
            for (auto const vertex_index:mesh.indices) {
                auto const& local=mesh.vertices.at(vertex_index).position;
                float const x=local.x*object.size;
                float const y=local.y*object.size;
                float const z=local.z*object.size;
                mesh_vertices.push_back({
                    object.position.x+cube.rotation[0]*x+cube.rotation[1]*y+cube.rotation[2]*z,
                    object.position.y+cube.rotation[3]*x+cube.rotation[4]*y+cube.rotation[5]*z,
                    object.position.z+cube.rotation[6]*x+cube.rotation[7]*y+cube.rotation[8]*z,
                    color});
            }
            if (static_cast<int>(i)==selected || (selected==-2 && object.player))
                focus=position;
            continue;
        }
        if (static_cast<int>(i) == selected) {
            selected_render_index = static_cast<int>(cubes.size());
            focus = position;
        } else if (selected == -2 && object.player) {
            focus = position;
        }
        cubes.push_back(cube);
    }

    if (fps) focus = fps->eye(scene);
    if (camera_pose) focus = camera_pose->position;

    // Two-level runtime HLOD: one conservative AABB proxy replaces multiple
    // distant static draw calls. Original physics/ECS objects are untouched.
    if (streaming) {
        for (auto const& proxy:scene_world->hlod_proxies()) {
            if (cubes.size()>=max_scene_objects) break;
            NorthStarRenderCube lod{};
            lod.x=proxy.center.x;
            lod.y=proxy.center.y-0.5f; // provider adds size*0.5 to Y
            lod.z=proxy.center.z;
            lod.size=1.f;
            lod.rgb=proxy.color;
            lod.rotation[0]=std::max(0.04f,proxy.half_extent.x*2.f);
            lod.rotation[4]=std::max(0.04f,proxy.half_extent.y*2.f);
            lod.rotation[8]=std::max(0.04f,proxy.half_extent.z*2.f);
            cubes.push_back(lod);
        }
    }
    // In-editor streaming visualization. Render 3D cell AABB edges through
    // the same GPU provider, without exposing debug-specific ABI pointers.
    // Limit work and draw distances to protect the frame budget.
    if (streaming && show_streaming_debug && !fps) {
        constexpr std::size_t max_cells=256;
        std::size_t emitted=0;
        auto edge=[&](Vector3 a,Vector3 b,std::uint32_t color) {
            if (cubes.size()>=max_scene_objects) return;
            NorthStarRenderCube line{};
            line.x=(a.x+b.x)*0.5f;
            line.y=(a.y+b.y)*0.5f-0.5f;
            line.z=(a.z+b.z)*0.5f;
            line.size=1.f;
            line.rgb=color;
            line.rotation[0]=std::max(0.09f,std::abs(b.x-a.x));
            line.rotation[4]=std::max(0.09f,std::abs(b.y-a.y));
            line.rotation[8]=std::max(0.09f,std::abs(b.z-a.z));
            cubes.push_back(line);
        };
        for (auto const& cell:scene_world->source().cells) {
            if (emitted>=max_cells || cubes.size()+12>=max_scene_objects) break;
            auto const cx=(cell.minimum.x+cell.maximum.x)*0.5f;
            auto const cz=(cell.minimum.z+cell.maximum.z)*0.5f;
            auto const dx=cx-focus.x;
            auto const dz=cz-focus.z;
            if (dx*dx+dz*dz>600.f*600.f) continue;
            ++emitted;
            std::uint32_t color=cell.always_loaded ? 0x729bdfu :
                scene_world->cell_loaded(cell.name) ? 0x51c68fu : 0xb66d75u;
            auto const x0=cell.minimum.x,x1=cell.maximum.x;
            auto const y0=cell.minimum.y,y1=cell.maximum.y;
            auto const z0=cell.minimum.z,z1=cell.maximum.z;
            edge({x0,y0,z0},{x1,y0,z0},color);
            edge({x0,y0,z1},{x1,y0,z1},color);
            edge({x0,y1,z0},{x1,y1,z0},color);
            edge({x0,y1,z1},{x1,y1,z1},color);
            edge({x0,y0,z0},{x0,y1,z0},color);
            edge({x0,y0,z1},{x0,y1,z1},color);
            edge({x1,y0,z0},{x1,y1,z0},color);
            edge({x1,y0,z1},{x1,y1,z1},color);
            edge({x0,y0,z0},{x0,y0,z1},color);
            edge({x1,y0,z0},{x1,y0,z1},color);
            edge({x0,y1,z0},{x0,y1,z1},color);
            edge({x1,y1,z0},{x1,y1,z1},color);
        }
    }

    // Editor transform overlay: colored axis handles and rotation rings. These
    // are transient GPU draw commands, not scene/ECS objects or physics bodies.
    if (gizmo && selected >= 0 &&
        static_cast<std::size_t>(selected) < scene.objects.size() && !fps) {
        auto const center=scene.objects[static_cast<std::size_t>(selected)].position;
        auto const add=[](Vector3 a, Vector3 b)->Vector3 {
            return {a.x+b.x,a.y+b.y,a.z+b.z};
        };
        auto const mul=[](Vector3 a,float t)->Vector3 {
            return {a.x*t,a.y*t,a.z*t};
        };
        auto const cross=[](Vector3 a,Vector3 b)->Vector3 {
            return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        };
        auto const normalize=[](Vector3 v)->Vector3 {
            float const length=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
            if (length<1e-5f) return {1.f,0.f,0.f};
            return {v.x/length,v.y/length,v.z/length};
        };
        auto segment=[&](Vector3 a,Vector3 b,std::uint32_t rgb,float thickness) {
            if (cubes.size()>=max_scene_objects) return;
            auto const tangent=normalize({b.x-a.x,b.y-a.y,b.z-a.z});
            auto const side=normalize(cross(tangent,std::abs(tangent.y)>.9f
                ? Vector3{0.f,0.f,1.f} : Vector3{0.f,1.f,0.f}));
            auto const up=cross(tangent,side);
            float const length=std::sqrt((b.x-a.x)*(b.x-a.x)+
                (b.y-a.y)*(b.y-a.y)+(b.z-a.z)*(b.z-a.z));
            NorthStarRenderCube line{};
            line.x=(a.x+b.x)*.5f;
            line.y=(a.y+b.y)*.5f-.5f;
            line.z=(a.z+b.z)*.5f;
            line.size=1.f;
            line.rgb=rgb;
            line.rotation[0]=tangent.x*length; line.rotation[3]=tangent.y*length; line.rotation[6]=tangent.z*length;
            line.rotation[1]=side.x*thickness; line.rotation[4]=side.y*thickness; line.rotation[7]=side.z*thickness;
            line.rotation[2]=up.x*thickness; line.rotation[5]=up.y*thickness; line.rotation[8]=up.z*thickness;
            cubes.push_back(line);
        };
        std::uint32_t const colors[3]{0xf46b73u,0x5ce4a1u,0x64a8ffu};
        for (unsigned axis=0;axis<3;++axis) {
            auto const direction=gizmo->axes[axis];
            auto const color=gizmo->active_axis==static_cast<int>(axis) ?
                0xffd66bu : colors[axis];
            if (gizmo->mode==1) {
                auto const u=normalize(cross(direction,
                    std::abs(direction.y)>.9f ? Vector3{0.f,0.f,1.f} : Vector3{0.f,1.f,0.f}));
                auto const v=cross(direction,u);
                constexpr float tau=6.283185307179586f;
                constexpr int segments=40;
                for (int i=0;i<segments && cubes.size()<max_scene_objects;++i) {
                    float const t0=tau*float(i)/float(segments);
                    float const t1=tau*float(i+1)/float(segments);
                    auto point=[&](float angle) {
                        return add(center,add(mul(u,gizmo->length*.8333333f*std::cos(angle)),
                            mul(v,gizmo->length*.8333333f*std::sin(angle))));
                    };
                    segment(point(t0),point(t1),color,.045f);
                }
            } else {
                segment(center,add(center,mul(direction,gizmo->length)),color,
                    std::max(.04f,gizmo->length*.035f));
                if (gizmo->mode==2 && cubes.size()<max_scene_objects) {
                    auto const tip=add(center,mul(direction,gizmo->length));
                    NorthStarRenderCube handle{};
                    handle.x=tip.x; handle.y=tip.y-gizmo->length*.13f; handle.z=tip.z;
                    handle.size=std::max(.1f,gizmo->length*.26f); handle.rgb=color;
                    handle.rotation[0]=handle.rotation[4]=handle.rotation[8]=1.f;
                    cubes.push_back(handle);
                }
            }
        }
    }

    NorthStarRenderFrame const frame{cubes.data(), static_cast<uint32_t>(cubes.size()),
        fps ? -1 : selected_render_index,
        focus.x, focus.y, focus.z, verify_pixels ? 1u : 0u,
        fps ? 1u : 0u,
        camera_pose ? camera_pose->yaw : (fps ? fps->yaw() : 0.f),
        camera_pose ? camera_pose->pitch : (fps ? fps->pitch() : 0.f),
        fps ? fps->recoil_amount() : 0.f,
        fps ? static_cast<uint32_t>(fps->health()) : 0u,
        fps && show_fps_overlay ? static_cast<uint32_t>(fps->maximum_health()) : 0u,
        fps ? static_cast<uint32_t>(fps->ammunition()) : 0u,
        fps ? static_cast<uint32_t>(fps->magazine_size()) : 0u,
        paused ? 1u : 0u,
        camera_pose ? 1u : 0u,
        camera_pose ? camera_pose->roll : 0.f,
        camera_pose ? camera_pose->fov_y_degrees : 45.f,
        camera_pose ? camera_pose->near_plane : 0.1f,
        camera_pose ? camera_pose->far_plane :
            (scene.version>=3 ? 1500.f : 250.f)};
    if (state->triangle_api &&
        !state->triangle_api->set_triangles(state->renderer,
            mesh_vertices.empty()?nullptr:mesh_vertices.data(),
            static_cast<std::uint32_t>(mesh_vertices.size())))
        throw state->module.error("Cannot submit scene mesh triangles");
    if (!state->api.draw(state->renderer, &frame))
        throw state->module.error("Cannot render frame");
}

} // namespace northstarcpp
