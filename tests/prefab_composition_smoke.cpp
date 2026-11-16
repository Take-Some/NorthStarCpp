#include "editor_prefab_composition.hpp"
#include "editor_tools.hpp"
#include "editor_asset_thumbnails.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/scene.hpp>
#include <cmath>
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
    if(argc!=2)return 2;
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
        // Real original YDD material sections must all survive composition.
        namespace fs=std::filesystem;
        fs::path repo=fs::absolute(argv[1]);
        auto prefab=load_scene(repo/"Projects/NorthStarFirstPersonPlatform"/
            "prefabs/pistol.nprefab");
        auto instance=prefab.objects.front();
        instance.node.id+=1;
        instance.node.parent=prefab.objects.front().node.id;
        instance.position.x+=2.5f;
        prefab.objects.push_back(instance);
        validate_scene(prefab);
        AssetManager assets;
        pp::mount_preview_assets(assets,
            repo/"Projects/NorthStarFirstPersonPlatform"/"assets");
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
        // Shared asset browser roots must mount exactly once; otherwise
        // the native preview drops to its schematic cube fallback.
        auto rifle=load_scene(repo/"sharedAssets"/"prefabs"/
            "rifle_mini14.nprefab");
        require(rifle.version==3 && rifle.objects.size()==1 &&
                rifle.objects.front().node.asset==
                    "models/weapon/rifle_mini14/rifle_mini14.ydd",
                "Mini-14 prefab failed to resolve native Scene V3 model");
        AssetManager rifle_assets;
        pp::mount_preview_assets(rifle_assets,repo/"sharedAssets");
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
        return 0;
    }catch(std::exception const& error) {
        std::cerr<<"Prefab composition QA: "<<error.what()<<"\n";
        return 1;
    }
}
