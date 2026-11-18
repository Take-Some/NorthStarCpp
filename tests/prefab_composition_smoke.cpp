#include "editor_prefab_composition.hpp"
#include "editor_tools.hpp"
#include "editor_asset_thumbnails.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/native_model.hpp>
#include <northstarcpp/scene.hpp>
#include <cmath>
#include <chrono>
#include <array>
#include <iostream>
#include <stdexcept>
#include <filesystem>
using namespace northstarcpp;
namespace pp=northstarcpp::editor::prefab_preview;
static void require(bool ok,char const* message) {
    if(!ok)throw std::runtime_error(message);
}
static bool close(float a,float b) {return std::abs(a-b)<.0001f;}
int main(int argc,char** argv) {
    if(argc<2 || argc>3 ||
       (argc==3 && std::string(argv[2])!="--mt28-only"))return 2;
    try {
        Scene source;
        source.version=3;
        source.name="Three Actor Studio";
        source.layers={{"Default",true}};
        SceneObject root;
        root.name="Root";
        root.node.id=1;
        root.node.asset.clear();
        source.objects.push_back(root);
        SceneObject model;
        model.name="Rotated Model";
        model.node.id=2;
        model.node.parent=1;
        model.node.asset="models/test.ydd";
        model.position={3.f,5.f,7.f};
        model.node.rotation={0.f,0.f,90.f};
        model.node.scale={2.f,1.f,1.f};
        source.objects.push_back(model);
        model.name="Second Model";
        model.node.id=3;
        model.position={-4.f,0.f,0.f};
        model.node.rotation={0.f,0.f,0.f};
        source.objects.push_back(model);
        shared_assets::Drawable triangle;
        triangle.name="Native triangle";
        shared_assets::MeshPart part;
        part.name="Test material section";
        part.material="materials/test.ymat@blue";
        part.vertices={
            {{{1.f,0.f,0.f}},{{1.f,0.f,0.f}},{{0.f,0.f}}},
            {{{0.f,1.f,0.f}},{{1.f,0.f,0.f}},{{1.f,0.f}}},
            {{{0.f,0.f,0.f}},{{1.f,0.f,0.f}},{{0.f,1.f}}}
        };
        part.indices={0,1,2};
        triangle.meshes.push_back(part);
        int loaded=0,materials=0;
        auto result=pp::compose(source,[&](std::string const& name) {
            require(name=="models/test.ydd","Wrong VFS model reference");
            ++loaded;
            return std::vector<shared_assets::Drawable>{triangle};
        },[&](std::string const& name) {
            require(name=="materials/test.ymat@blue",
                    "Wrong native material reference");
            ++materials;
            MaterialDefinition def;
            def.base_color={.7f,.5f,.3f,1.f};
            def.textures[0].width=2;
            def.textures[0].height=2;
            def.textures[0].rgba8={
                255,0,0,255,0,255,0,255,
                0,0,255,255,255,255,255,255
            };
            return def;
        });
        require(result.visible_objects==3 && result.modeled_objects==2 &&
                result.proxy_objects==1 && result.missing_assets==0 &&
                result.drawable.meshes.size()==3 &&
                result.materials.size()==3 && result.triangles==14,
                "Prefab did not compose all visible mesh sections");
        require(loaded==1 && materials==1,
                "Repeating the same native YDD/YMAT was decoded multiple times");
        auto const& transformed=result.drawable.meshes[1].vertices[0];
        require(close(transformed.position[0],3.f) &&
                close(transformed.position[1],7.f) &&
                close(transformed.position[2],7.f) &&
                close(transformed.normal[0],0.f) &&
                close(transformed.normal[1],1.f) &&
                close(transformed.uv[0],0.f),
                "Prefab transform or inverse-transpose normal was not applied");
        require(result.materials[1].textures[0].width==2 &&
                result.materials[2].textures[0].width==2,
                "Native texture binding was lost across prefab instances");
        auto const dark=editor_tools::preview_drawable(result.drawable,
            240,240,.62f,.27f,1.f,false,&result.materials,0.f);
        auto const lit=editor_tools::preview_drawable(result.drawable,
            240,240,.62f,.27f,1.f,false,&result.materials,2.5f);
        require(dark.valid() && lit.valid() &&
                dark.argb!=lit.argb,
                "Native prefab studio key light does not affect scene pixels");
        source.objects[2].node.visible=false;
        auto hidden=pp::compose(source,[&](std::string const&) {
            return std::vector<shared_assets::Drawable>{triangle};
        },[&](std::string const&) {return MaterialDefinition{};});
        require(hidden.visible_objects==2 && hidden.triangles==13,
                "Hidden prefab actor leaked into shared Z-buffer");
        // Scene V3 ECS-authored material overrides take precedence over
        // the original YDD mesh section material, exactly like Player.
        auto overridden=source;
        overridden.objects[2].node.visible=true;
        editor_tools::assign_actor_material(
            overridden,2,"materials/override.material.json");
        auto assigned=pp::compose(overridden,
            [&](std::string const&) {
                return std::vector<shared_assets::Drawable>{triangle};
            },[&](std::string const& name) {
                MaterialDefinition material;
                material.base_color=name=="materials/override.material.json"?
                    std::array<float,4>{.2f,.8f,.3f,1.f}:
                    std::array<float,4>{.7f,.5f,.3f,1.f};
                return material;
            });
        require(assigned.drawable.meshes[2].material==
                "materials/override.material.json" &&
                close(assigned.materials[2].base_color[1],.8f),
                "Authored per-entity material assignment ignored in composite");
        auto const before=serialize_scene_v3(source);
        auto missing=source;
        missing.objects[1].node.asset="models/missing.ydd";
        auto fallback=pp::compose(missing,
            [&](std::string const&) -> std::vector<shared_assets::Drawable> {
                throw std::runtime_error("Model unavailable");
            },[&](std::string const&){return MaterialDefinition{};});
        require(fallback.missing_assets==1 &&
                fallback.proxy_objects==2 &&
                !fallback.warnings.empty() &&
                !fallback.drawable.meshes.empty(),
                "Missing native resource did not preserve composed proxy/diagnostic");
        require(serialize_scene_v3(source)==before,
                "Preview builder mutated the original Scene V3 document");
        // Regression: prefab previews must not reject or silently truncate
        // meshes at the old 120K triangle threshold. Render the *last* face
        // after 120001 degenerate faces to exercise the software rasterizer.
        Scene dense_scene;
        dense_scene.version=3;
        dense_scene.name="Unlimited Geometry";
        dense_scene.layers={{"Default",true}};
        SceneObject dense_actor;
        dense_actor.name="Dense";
        dense_actor.node.id=1;
        dense_actor.node.asset="models/dense.ydd";
        dense_scene.objects.push_back(dense_actor);
        shared_assets::MeshPart dense_part=part;
        dense_part.material.clear();
        dense_part.indices.assign(120001u*3u,0u);
        dense_part.indices.insert(dense_part.indices.end(),{0u,1u,2u});
        shared_assets::Drawable dense_model;
        dense_model.meshes.push_back(dense_part);
        auto dense=pp::compose(dense_scene,
            [&](std::string const&) {
                return std::vector<shared_assets::Drawable>{dense_model};
            },[&](std::string const&){return MaterialDefinition{};});
        require(dense.triangles==120002u && dense.drawable.meshes.size()==1,
                "Prefab composition still imposes a triangle budget");
        auto dense_image=editor_tools::preview_drawable(dense.drawable,
            160,160,.62f,.27f,1.f,false,&dense.materials);
        auto blank_model=dense.drawable;
        blank_model.meshes.front().indices.resize(120001u*3u);
        auto blank_image=editor_tools::preview_drawable(blank_model,
            160,160,.62f,.27f,1.f,false,&dense.materials);
        require(dense_image.valid() && blank_image.valid() &&
                dense_image.argb!=blank_image.argb,
                "Prefab software preview truncates faces beyond 120K");
        // Independently verify the former per-prefab mesh and vertex caps.
        shared_assets::Drawable many_parts;
        shared_assets::MeshPart tiny_part=part;
        tiny_part.material.clear();
        many_parts.meshes.assign(4097,tiny_part);
        auto many=pp::compose(dense_scene,
            [&](std::string const&) {
                return std::vector<shared_assets::Drawable>{many_parts};
            },[&](std::string const&){return MaterialDefinition{};});
        require(many.drawable.meshes.size()==4097 &&
                many.materials.size()==4097,
                "Prefab composition still imposes a mesh-section budget");
        shared_assets::Drawable many_vertices;
        auto big_part=part;
        big_part.material.clear();
        big_part.vertices.resize(600001);
        many_vertices.meshes.push_back(std::move(big_part));
        auto many_vertex_preview=pp::compose(dense_scene,
            [&](std::string const&) {
                return std::vector<shared_assets::Drawable>{many_vertices};
            },[&](std::string const&){return MaterialDefinition{};});
        require(many_vertex_preview.drawable.meshes.front().vertices.size()==600001,
                "Prefab composition still imposes a vertex budget");
        std::cout<<"UNBOUNDED_PREFAB triangles="<<dense.triangles
                 <<" mesh_sections="<<many.drawable.meshes.size()
                 <<" vertices="<<many_vertex_preview.drawable.meshes.front().vertices.size()
                 <<"\n";
        // Real original YDD material sections must all survive composition.
        namespace fs=std::filesystem;
        fs::path repo=fs::absolute(argv[1]);
        // Regression for imported MT28 prefab entities: a legacy export
        // inserted an extra rotation slot before scale and printed the
        // color in decimal even though Scene V3 reads it as hexadecimal.
        // Both project copies must remain strict Scene V3 without a
        // special-case lenient parser that could misread other prefabs.
        for(auto const& project:{"NorthStarFirstPersonPlatform","KaylaNightCity"}) {
            auto const project_dir=repo/"Projects"/project;
            auto const bus=load_scene(project_dir/"prefabs/mahir_mt28.nprefab");
            require(bus.version==3 && bus.objects.size()==65,
                    "MT28 prefab failed Scene V3 parsing or lost entities");
            require(bus.objects[0].node.id==1 &&
                    bus.objects[0].node.parent==0,
                    "MT28 prefab root is invalid");
            for(std::size_t index=0;index<bus.objects.size();++index) {
                auto const& object=bus.objects[index];
                require(// Entity ID 4 belonged to a deprecated full-body proxy.
                // Preserve all remaining stable IDs and parent links.
                object.node.id==index+1+(index>=3?1:0) &&
                        object.node.parent==(index==0?0u:1u) &&
                        object.color==0x444444u &&
                        std::isfinite(object.node.rotation.x) &&
                        std::isfinite(object.node.rotation.y) &&
                        std::isfinite(object.node.rotation.z) &&
                        std::abs(object.node.rotation.x)<=180.f &&
                        std::abs(object.node.rotation.y)<=180.f &&
                        std::abs(object.node.rotation.z)<=180.f &&
                        close(object.node.scale.x,1.f) &&
                        close(object.node.scale.y,1.f) &&
                        close(object.node.scale.z,1.f) &&
                        close(object.node.lod_near,0.f) &&
                        close(object.node.lod_far,1000000.f) &&
                        (object.node.visible ||
                         (object.node.id==2 && object.name=="coach_fx_cutout")) &&
                        !object.node.collision,
                        "MT28 prefab entity fields are shifted or invalid");
                require(fs::is_regular_file(project_dir/"assets"/
                    fs::u8path(object.node.asset)),
                    "MT28 prefab references a missing native vehicle mesh");
            }
            // FX cutout is an opaque technical mesh with broad horizontal
            // faces, not a visible body surface. Keep its stable entity ID,
            // but exclude it from actual rendering.
            require(bus.objects[1].node.id==2 &&
                    bus.objects[1].name=="coach_fx_cutout" &&
                    !bus.objects[1].node.visible,
                    "MT28 helper cutout is incorrectly visible");
            // The low-detail authored coach proxy is a *second complete
            // body*, not an extra detail mesh. Drawing it over the native
            // shell produces the doubled roof/side seen in Prefab Preview.
            std::size_t original_shells=0;
            for(auto const& object:bus.objects) {
                require(object.node.asset.find(
                    "mahir_mt28_basic_coach_01.ydr")==std::string::npos,
                    "MT28 prefab draws the legacy body proxy over the native shell");
                if(object.node.asset.find(
                    "v_mahir_mt28_coach__ext01_body_01.ydr")!=std::string::npos)
                    ++original_shells;
            }
            require(original_shells==1,
                    "MT28 prefab must contain exactly one native main body");
            auto const roundtrip=parse_scene_v3(serialize_scene_v3(bus));
            require(roundtrip.objects.size()==65,
                    "MT28 prefab lost entities on Scene V3 serialization");
            require(close(bus.objects.front().position.y,1.04f),
                    "MT28 body lost original vehicle root height");
            std::size_t wheel_parts=0;
            for(auto const& object:bus.objects) {
                auto const& id=object.node.asset;
                if(id.find("_tire_")==std::string::npos &&
                   id.find("_wheel_")==std::string::npos)continue;
                ++wheel_parts;
                require(std::abs(object.position.x)>=.99f &&
                        close(object.position.y,.54f) &&
                        std::abs(object.position.z)>=2.39f,
                        "MT28 tyre/rim is not attached to an original axle");
            }
            require(wheel_parts==16,
                    "MT28 eight-wheel rig must bind 16 tyre/rim objects");
            std::cout<<"MT28_PREFAB project="<<project
                     <<" entities="<<bus.objects.size()
                     <<" axle_visuals="<<wheel_parts
                     <<" valid_mesh_paths=65 roundtrip=PASS\n";
            if(std::string(project)=="KaylaNightCity") {
                AssetManager mt28_native_assets;
                mt28_native_assets.mount_project_assets(project_dir);
                for(auto const& filename:{
                    "v_mahir_mt28_coach__ext01_chassis_01.ydr",
                    "v_mahir_mt28_coach__ext01_doors.ydr"}) {
                    auto const virtualRef=std::string("models/mahir_mt28/")+filename;
                    auto bytes=mt28_native_assets.read(virtualRef);
                    require(bytes.size()>4 && std::equal(
                        bytes.begin(),bytes.begin()+4,std::string("NEF8").begin()),
                        "MT28 engine.runtime VFS does not return original NEF8 model");
                    auto native=load_native_model_resource(
                        mt28_native_assets,virtualRef,true);
                    require(!native.mesh.vertices.empty() &&
                            !native.mesh.indices.empty(),
                        "MT28 runtime native model decoder cannot load original model");
                    std::cout<<"MT28_RUNTIME_ASSET "<<filename
                             <<" bytes="<<bytes.size()
                             <<" triangles="<<native.mesh.indices.size()/3<<"\n";
                }
                AssetManager mt28_assets;
                mt28_assets.mount_content_assets(project_dir/"assets");
                auto const composed=pp::load_composition(bus,mt28_assets);
                std::array<std::size_t,3> blend_histogram{};
                std::size_t with_custom_shader=0,with_graph=0;
                std::array<std::size_t,3> rigid_opaque_candidates{};
                std::array<std::size_t,3> base_alpha_opaque{};

                for(auto const& mat:composed.materials) {
                    if(mat.blend_mode<3) {
                        ++blend_histogram[mat.blend_mode];
                        bool const baseOpaque=mat.base_color[3]>=.999f &&
                                              mat.opacity>=.999f;
                        if(baseOpaque)++base_alpha_opaque[mat.blend_mode];
                        bool alphaOpaque=true;
                        auto const& tex=mat.textures[0].rgba8;
                        for(std::size_t k=3;k<tex.size();k+=4)
                            if(tex[k]!=255) {alphaOpaque=false;break;}
                        if(baseOpaque && alphaOpaque)
                            ++rigid_opaque_candidates[mat.blend_mode];
                    }
                    if(!mat.graph_shader_defines.empty())++with_graph;
                    if(!mat.fragment_shader_asset.empty() ||
                       !mat.vertex_shader_asset.empty() ||
                       !mat.gbuffer_shader_asset.empty() ||
                       !mat.fragment_hlsl_asset.empty() ||
                       !mat.vertex_hlsl_asset.empty() ||
                       !mat.gbuffer_hlsl_asset.empty())++with_custom_shader;
                }
                std::cout<<"MT28_GPU_ELIGIBILITY opaque="<<blend_histogram[0]
                         <<" masked="<<blend_histogram[1]
                         <<" translucent="<<blend_histogram[2]
                         <<" graph="<<with_graph
                         <<" shaders="<<with_custom_shader
                         <<" rgbaOpaque="<<rigid_opaque_candidates[0]<<","<<rigid_opaque_candidates[1]<<","<<rigid_opaque_candidates[2]
                         <<" baseOpaque="<<base_alpha_opaque[0]<<","<<base_alpha_opaque[1]<<","<<base_alpha_opaque[2]<<"\n";
                std::cout<<"MT28_PREVIEW modeled="<<composed.modeled_objects
                         <<" missing="<<composed.missing_assets
                         <<" proxy="<<composed.proxy_objects
                         <<" triangles="<<composed.triangles
                         <<" warnings="<<composed.warnings.size()<<"\n";
                for(auto const& part:composed.drawable.meshes) {
                    auto const& name=part.name;
                    if(name.find("wheel")==std::string::npos &&
                       name.find("tire")==std::string::npos &&
                       name.find("chassis")==std::string::npos &&
                       name.find("interior")==std::string::npos &&
                       name.find("seats")==std::string::npos &&
                       name.find("body_01")==std::string::npos)continue;
                    std::array<float,3> mn{1.e9f,1.e9f,1.e9f},mx{-1.e9f,-1.e9f,-1.e9f};
                    for(auto const& v:part.vertices)
                        for(unsigned axis=0;axis<3;++axis) {
                            mn[axis]=std::min(mn[axis],v.position[axis]);
                            mx[axis]=std::max(mx[axis],v.position[axis]);
                        }
                    std::cout<<"MT28_PART "<<name
                        <<" bounds_x="<<mn[0]<<","<<mx[0]
                        <<" y="<<mn[1]<<","<<mx[1]
                        <<" z="<<mn[2]<<","<<mx[2]<<"\n";
                }
                for(std::size_t w=0;w<std::min<std::size_t>(composed.warnings.size(),10);++w)
                    std::cout<<"MT28_WARNING "<<composed.warnings[w]<<"\n";
                require(composed.modeled_objects==64 &&
                        composed.missing_assets==0 &&
                        composed.proxy_objects==0 &&
                        composed.triangles>0 &&
                        composed.warnings.empty(),
                        "MT28 prefab preview did not load all models and materials");
                std::size_t transparent_sections=0;
                for(auto const& mat:composed.materials)
                    if(mat.blend_mode==2 && mat.base_color[3]<.5f)
                        ++transparent_sections;
                require(transparent_sections>=10,
                        "MT28 glass alpha was lost during native YMAT loading");
                // Regression: transparency must change actual raster pixels,
                // not merely load the alpha flag from XML YMAT.
                shared_assets::Drawable alpha_fixture;
                shared_assets::MeshPart background_part=part;
                background_part.material.clear();
                background_part.vertices={
                    {{{-1.f,-1.f,0.f}},{{0.f,0.f,1.f}},{{0.f,0.f}}},
                    {{{ 1.f,-1.f,0.f}},{{0.f,0.f,1.f}},{{1.f,0.f}}},
                    {{{ 0.f, 1.f,0.f}},{{0.f,0.f,1.f}},{{.5f,1.f}}}};
                background_part.indices={0,1,2};
                auto glass_part=background_part;
                glass_part.name="Glass";
                for(auto& v:glass_part.vertices)v.position[2]=.15f;
                alpha_fixture.meshes={background_part,glass_part};
                MaterialDefinition solid_back,glass;
                solid_back.base_color={.8f,.2f,.1f,1.f};
                glass.base_color={.1f,.4f,.9f,.22f};
                glass.blend_mode=2;
                std::vector<MaterialDefinition> glass_mat{solid_back,glass};
                auto const with_glass=editor_tools::preview_drawable(
                    alpha_fixture,128,128,0.f,0.f,1.f,false,&glass_mat);
                glass_mat[1].blend_mode=0;
                auto const opaque_glass=editor_tools::preview_drawable(
                    alpha_fixture,128,128,0.f,0.f,1.f,false,&glass_mat);
                require(with_glass.valid() && opaque_glass.valid() &&
                        with_glass.argb!=opaque_glass.argb,
                        "3D preview glass does not blend over opaque geometry");
                auto const render_begin=std::chrono::steady_clock::now();
                auto const bus_image=editor_tools::preview_drawable(
                    composed.drawable,192,192,.62f,.27f,1.f,false,
                    &composed.materials);
                auto const render_end=std::chrono::steady_clock::now();
                auto const frame_ms=std::chrono::duration<double,std::milli>(
                    render_end-render_begin).count();
                auto const interactive_begin=std::chrono::steady_clock::now();
                auto const interactive_image=editor_tools::preview_drawable(
                    composed.drawable,192,192,.62f,.27f,1.f,false,
                    &composed.materials,1.f,12);
                auto const interactive_end=std::chrono::steady_clock::now();
                auto const interactive_ms=std::chrono::duration<double,std::milli>(
                    interactive_end-interactive_begin).count();
                require(interactive_image.valid(),
                    "MT28 interactive full-mesh LOD failed");
                std::cout<<"MT28_PREVIEW_FRAME_MS full="<<frame_ms
                         <<" interactive="<<interactive_ms
                         <<" transparent_sections="<<transparent_sections<<"\n";
                require(bus_image.valid(),"MT28 prefab raster preview is invalid");
                std::size_t visible_pixels=0;
                for(auto const pixel:bus_image.argb)
                    if(pixel!=0xff1d2531u)++visible_pixels;
                require(visible_pixels>0,"MT28 prefab preview rendered no pixels");
                std::cout<<"MT28_PREVIEW_RASTER dimensions="<<bus_image.width
                         <<"x"<<bus_image.height
                         <<" pixels="<<bus_image.argb.size()<<"\n";
            }
        }
        // Scope a regression run to the MT28 scene/mesh/material pipeline
        // without requiring unrelated shared-asset fixtures.
        if(argc>2 && std::string(argv[2])=="--mt28-only") {
            std::cout<<"PASS: MT28 two-copy scene and original-body-only preview QA\n";
            return 0;
        }
        auto prefab=load_scene(repo/"engine/northstarcpp/resources/sharedAssets"/
            "prefabs/weapon/pistol.nprefab");
        auto instance=prefab.objects.front();
        instance.node.id+=1;
        instance.node.parent=prefab.objects.front().node.id;
        instance.position.x+=2.5f;
        prefab.objects.push_back(instance);
        validate_scene(prefab);
        AssetManager assets;
        assets.mount_content_assets(repo/"Projects/NorthStarFirstPersonPlatform"/"assets");
        auto real=pp::load_composition(prefab,assets);
        require(real.visible_objects==2 && real.modeled_objects==2 &&
                real.missing_assets==0 && real.triangles>30000 &&
                real.drawable.meshes.size()==real.materials.size(),
                "Two original pistol YDD meshes were not composed entirely");
        std::size_t native_textures=0;
        for(auto const& material:real.materials)
            if(material.textures[0].width>0 &&
               !material.textures[0].rgba8.empty())
                ++native_textures;
        require(native_textures==real.drawable.meshes.size(),
                "Native YMAT/YTD material textures did not resolve per section");
        std::cout<<"REAL_ASSET triangle_count="<<real.triangles
                 <<" mesh_sections="<<real.drawable.meshes.size()
                 <<" textured_sections="<<native_textures
                 <<" warnings="<<real.warnings.size()<<"\n";
        auto scene_image=editor_tools::preview_drawable(real.drawable,
            300,300,.35f,.3f,1.f,false,&real.materials);
        require(scene_image.valid(),
                "Original two-pistol native preview could not be rasterized");
        std::cout<<"PASS: native multi-YDD Prefab scene transforms, "
                  "shared depth buffer, materials, light and visibility\n";
        // Regression: Abby's attached pistol prefab and model are both
        // loaded from shared engine content, not the project.
        auto const abby_root=repo/"Projects/AbbyPistolDemo";
        require(!fs::is_regular_file(
                    abby_root/"assets/models/weapon/pistol/pistol.ydd"),
                "QA requires a shared pistol, not project-local duplicate");
        auto const abby_pistol=load_scene(repo/"engine/northstarcpp/resources/sharedAssets/prefabs/weapon/pistol.nprefab");
        AssetManager abby_assets;
        abby_assets.mount_content_assets(abby_root/"assets");
        auto const abby_composition=pp::load_composition(abby_pistol,abby_assets);
        require(abby_composition.visible_objects==1 &&
                abby_composition.modeled_objects==1 &&
                abby_composition.proxy_objects==0 &&
                abby_composition.missing_assets==0 &&
                abby_composition.triangles>15000 &&
                abby_composition.warnings.empty(),
                "Abby Pistol preview lost its shared engine model");
        std::cout<<"ABBY_PISTOL triangles="<<abby_composition.triangles
                 <<" sections="<<abby_composition.drawable.meshes.size()
                 <<" missing="<<abby_composition.missing_assets<<"\n";
        // The real Abby prefab was previously rejected at 120K triangles.
        // Exercise its project-independent sharedAssets VFS path.
        auto const shared_root=repo/"engine/northstarcpp/resources/sharedAssets";
        auto const abby_prefab=load_scene(shared_root/"prefabs/characters/abby.nprefab");
        AssetManager full_abby_assets;
        full_abby_assets.mount_content_assets(shared_root);
        auto const full_abby=pp::load_composition(abby_prefab,full_abby_assets);
        require(full_abby.visible_objects==1 &&
                full_abby.modeled_objects==1 &&
                full_abby.proxy_objects==0 &&
                full_abby.missing_assets==0 &&
                full_abby.triangles>120000 &&
                !full_abby.drawable.meshes.empty(),
                "Abby prefab was truncated or reverted to a proxy");
        auto abby_image=editor_tools::preview_drawable(
            full_abby.drawable,256,256,.62f,.27f,1.f,
            false,&full_abby.materials);
        require(abby_image.valid(),
                "Full-resolution Abby prefab preview failed to render");
        std::size_t resolved_abby_textures=0;
        for(std::size_t i=0;i<full_abby.drawable.meshes.size();++i) {
            auto const& section=full_abby.drawable.meshes[i];
            require(section.material.rfind(
                "characters/abby/materials/abby.ymat@",0)==0,
                "Abby preview retained obsolete YDD material namespace");
            if(full_abby.materials[i].textures[0].width>0 &&
               !full_abby.materials[i].textures[0].rgba8.empty())
                ++resolved_abby_textures;
        }
        require(full_abby.warnings.empty() &&
                resolved_abby_textures==full_abby.drawable.meshes.size(),
                "Abby prefab did not resolve every original YMAT/YTD material");
        std::cout<<"ABBY_MATERIALS resolved_textures="
                 <<resolved_abby_textures<<"\n";
        if(!full_abby.warnings.empty())
            std::cout<<"ABBY_PREFAB first_warning="<<full_abby.warnings.front()<<"\n";
        std::cout<<"ABBY_PREFAB triangles="<<full_abby.triangles
                 <<" sections="<<full_abby.drawable.meshes.size()
                 <<" warnings="<<full_abby.warnings.size()<<"\n";
        // Shared asset browser roots must mount exactly once; otherwise
        // the native preview drops to its schematic cube fallback.
        auto rifle=load_scene(repo/"engine/northstarcpp/resources/sharedAssets"/"prefabs"/
            "weapon/rifle_mini14.nprefab");
        require(rifle.version==3 && rifle.objects.size()==1 &&
                rifle.objects.front().node.asset==
                    "models/weapon/rifle_mini14/rifle_mini14.ydd",
                "Mini-14 prefab failed to resolve native Scene V3 model");
        AssetManager rifle_assets;
        // Validate the distributable NSP independently of editable source.
        rifle_assets.mount_runtime_archives();
        auto rifle_composition=pp::load_composition(rifle,rifle_assets);
        require(rifle_composition.visible_objects==1 &&
                rifle_composition.modeled_objects==1 &&
                rifle_composition.proxy_objects==0 &&
                rifle_composition.missing_assets==0 &&
                rifle_composition.triangles>0 &&
                !rifle_composition.drawable.meshes.empty() &&
                rifle_composition.drawable.meshes.size()==
                    rifle_composition.materials.size(),
                "Mini-14 shared prefab 3D model did not compose");
        std::size_t rifle_textured=0;
        for(auto const& material:rifle_composition.materials)
            if(material.textures[0].width>0 &&
               !material.textures[0].rgba8.empty())++rifle_textured;
        if(!rifle_composition.warnings.empty() ||
           rifle_textured!=rifle_composition.materials.size()) {
            std::cerr<<"MINI14_DIAGNOSTIC textures="<<rifle_textured
                     <<" materials="<<rifle_composition.materials.size()
                     <<" warnings="<<rifle_composition.warnings.size()<<"\n";
            for(auto const& warning:rifle_composition.warnings)
                std::cerr<<"MINI14_WARN "<<warning<<"\n";
            for(std::size_t i=0;i<rifle_composition.materials.size();++i)
                std::cerr<<"MINI14_SECTION "<<i
                         <<" diffuse_texture_width="<<
                         rifle_composition.materials[i].textures[0].width<<"\n";
        }
        require(rifle_composition.warnings.empty() &&
                rifle_textured==rifle_composition.materials.size(),
                "Mini-14 material texture sections failed to resolve");
        auto rifle_image=editor_tools::preview_drawable(
            rifle_composition.drawable,320,320,.62f,.27f,1.f,false,
            &rifle_composition.materials);
        require(rifle_image.valid(),
                "Mini-14 native preview could not be rasterized");
        for(auto const& issue:rifle_composition.warnings)
            std::cout<<"MINI14_WARN "<<issue<<"\n";
        std::cout<<"MINI14 triangle_count="<<rifle_composition.triangles
                 <<" mesh_sections="<<rifle_composition.drawable.meshes.size()
                 <<" warnings="<<rifle_composition.warnings.size()<<"\n";
        // The authoring YTD, not just a previously packed NSP, must decode.
        // Mini-14 contains padded BC1/BC4 tail mips and an exact mip alias.
        AssetManager authoring_assets;
        authoring_assets.mount_content_assets(
            repo/"engine/northstarcpp/resources/sharedAssets");
        auto const authoring=pp::load_composition(rifle,authoring_assets);
        require(authoring.modeled_objects==1 &&
                authoring.missing_assets==0 &&
                authoring.triangles==rifle_composition.triangles,
                "Editable shared source mount lost the Mini-14 model");
        require(authoring.warnings.empty(),
                "Editable Mini-14 material bindings failed to resolve");
        std::size_t textured_sections=0;
        for(auto const& material:authoring.materials)
            if(material.textures[0].width>0 &&
               !material.textures[0].rgba8.empty() &&
               material.textures[1].width>0 &&
               !material.textures[1].rgba8.empty())
                ++textured_sections;
        require(textured_sections==authoring.materials.size(),
                "Mini-14 authoring material textures were not decoded");
        auto const textures=shared_assets::load_textures(
            authoring_assets,"textures/weapon/rifle_mini14.ytd");
        require(textures.size()==6,
                "Mini-14 YTD texture dictionary is incomplete");
        constexpr std::array<std::uint16_t,6> expected_formats{
            101,105,102,101,101,102}; // base/normal/roughness * 2
        for(std::size_t i=0;i<textures.size();++i) {
            auto const& texture=textures[i];
            require(texture.mips.size()==11,
                    "Mini-14 tail mip chain was truncated");
            require(texture.format_id==expected_formats[i],
                    "Mini-14 texture codec mismatches its material slot");
            auto const thumbnail=editor_tools::thumbnail_texture(texture);
            auto const preview=editor_tools::preview_texture(texture,128);
            require(thumbnail.valid() && preview.valid(),
                    "Mini-14 native texture preview cannot decode BCn format");
            if(i==2 || i==5) {
                for(auto pixel:preview.argb)
                    require(((pixel>>16)&255)==((pixel>>8)&255) &&
                            ((pixel>>8)&255)==(pixel&255),
                            "Mini-14 roughness must decode as single-channel BC4");
            }
        }
        std::cout<<"MINI14_FORMAT_QA textures="<<textures.size()
                 <<" decoded_previews=6 roughness_grayscale=2\n";
        return 0;
    }catch(std::exception const& error) {
        std::cerr<<"Prefab composition QA: "<<error.what()<<"\n";
        return 1;
    }
}
