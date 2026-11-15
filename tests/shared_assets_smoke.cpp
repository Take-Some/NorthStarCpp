// NorthStarCpp
// Author: Kayla Verner
#include <northstarcpp/assets.hpp>
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/native_model.hpp>
#include <northstarcpp/material.hpp>
#include <northstarcpp/skeleton.hpp>


#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, char const* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    try {
        auto const root = std::filesystem::u8path(argv[1]);
        northstarcpp::AssetManager manager;
        manager.mount_shared_assets(root);
        // The browser and streaming API must see every embedded ListFile
        // entry, never silently substitute the first texture/drawable.
        auto const texture_entries=northstarcpp::shared_assets::list_dictionary_entries(
            manager,"textures/skydome.ytd");
        auto const drawable_entries=northstarcpp::shared_assets::list_dictionary_entries(
            manager,"models/skydome.ydd");
        // Project's original Pistol YDD, not the temporary hand-built OBJ.
        auto const pistol_path=std::string("models/weapon/pistol/pistol.ydd");
        auto const pistol_entries=northstarcpp::shared_assets::list_dictionary_entries(
            manager,pistol_path);
        require(!pistol_entries.empty(),"Pistol YDD has no drawable entries");
        auto const pistol_drawables=northstarcpp::shared_assets::load_drawables(
            manager,pistol_path);
        require(pistol_drawables.size()==pistol_entries.size(),
            "Pistol YDD drawable count does not match dictionary");
        auto const pistol_mesh=northstarcpp::load_scene_mesh(manager,pistol_path);
        require(pistol_mesh.indices.size()==21902u*3u &&
                pistol_mesh.vertices.size()>10000,
            "Runtime mesh dispatch failed to recover original pistol geometry");
        require(pistol_mesh.sections.size()==8u,
            "Native YDD mesh sections were flattened and lost their YMAT links");
        std::size_t section_indices=0;
        std::size_t textured_sections=0;
        for(auto const& section:pistol_mesh.sections) {
            require(section.first_index==section_indices && section.index_count%3==0,
                "Native model section index range is not contiguous");
            section_indices+=section.index_count;
            if(!section.material_reference.empty()) {
                require(section.material_reference.find(".ymat@")!=std::string::npos,
                    "Native material is not a YMAT dictionary reference");
                auto material=northstarcpp::load_material_definition(
                    manager,section.material_reference);
                require(!material.textures[0].rgba8.empty(),
                    "Native Pistol YMAT was not resolved to YTD base color");
                ++textured_sections;
            }
        }
        require(section_indices==pistol_mesh.indices.size() && textured_sections>=4,
            "Original pistol has missing material sections");
        auto bundled=northstarcpp::load_native_model_resource(manager,pistol_path);
        require(bundled.mesh.sections.size()==8 &&
                bundled.mesh.indices.size()==pistol_mesh.indices.size() &&
                bundled.materials.size()==4u &&
                bundled.skeleton.has_value() &&
                bundled.skeleton->joints.size()==62u &&
                bundled.skeleton_reference==
                    "models/weapon/pistol/pistol.skeleton.ymt",
            "Pistol native dependency bundle is incomplete");
        for(auto const& entry:bundled.materials)
            require(!entry.second.textures[0].rgba8.empty(),
                "Native bundle did not preserve decoded YTD albedo");
        require(bundled.has_skin() && !bundled.vertex_skin.empty(),
            "Original Pistol YDD skin weights are missing from native model");
        northstarcpp::SkeletonPose rest_pose(*bundled.skeleton);
        auto rest_model=northstarcpp::skin_native_model_cpu(
            bundled,rest_pose.palette());
        require(rest_model.vertices.size()==bundled.mesh.vertices.size() &&
                rest_model.sections.size()==bundled.mesh.sections.size() &&
                rest_model.indices.size()==bundled.mesh.indices.size(),
            "CPU skinning lost native material geometry ranges");
        float bind_error=0.f;
        for(std::size_t i=0;i<rest_model.vertices.size();++i) {
            auto const& a=rest_model.vertices[i].position;
            auto const& b=bundled.mesh.vertices[i].position;
            bind_error=std::max(bind_error,std::max({
                std::abs(a.x-b.x),std::abs(a.y-b.y),std::abs(a.z-b.z)}));
        }
        require(bind_error<0.001f,
            "Native Pistol 8-weight skinning does not reproduce YMT bind pose");
        std::cout<<"Pistol native 8-influence skin: "<<bundled.vertex_skin.size()
                 <<" vertices; bind error "<<bind_error<<"\n";
        auto pistol_clips=northstarcpp::load_skeletal_clips(manager,
            "animations/weapon/pistol/military-pistol.ycd",
            {"idle","p-fire","p-reload"},*bundled.skeleton);
        require(pistol_clips.size()==3u,
            "Original Pistol YCD animation clips are unavailable");
        float animated_delta=0.f;
        for(auto const& clip:pistol_clips) {
            northstarcpp::SkeletonPose animated(*bundled.skeleton);
            animated.sample(*bundled.skeleton,clip,
                0.5f*float(clip.frames-1)/clip.sample_rate,false);
            auto deformed=northstarcpp::skin_native_model_cpu(
                bundled,animated.palette());
            require(deformed.indices==bundled.mesh.indices &&
                    deformed.sections.size()==bundled.mesh.sections.size(),
                "Native animation broke material section or triangle topology");
            for(std::size_t i=0;i<deformed.vertices.size();++i) {
                auto const& a=deformed.vertices[i].position;
                auto const& b=bundled.mesh.vertices[i].position;
                animated_delta=std::max(animated_delta,std::max({
                    std::abs(a.x-b.x),std::abs(a.y-b.y),std::abs(a.z-b.z)}));
            }
        }
        require(animated_delta>0.00001f,
            "Original Pistol animation did not deform any skinned vertex");
        std::cout<<"Pistol YCD three clips: max vertex delta "
                 <<animated_delta<<"\n";


        auto sky_model=northstarcpp::load_native_model_resource(
            manager,"models/skydome.ydd@skydome_high");
        require(sky_model.mesh.sections.size()==1u &&
                !sky_model.skeleton.has_value(),
            "Static YDD without a YMT companion should be allowed");

        auto const skeleton=northstarcpp::load_skeleton(manager,
            "models/weapon/pistol/pistol.skeleton.ymt");
        require(!skeleton.joints.empty(),
            "Pistol skeleton YMT did not load into the common skeleton runtime");
        std::cout << "Native Pistol: " << textured_sections
                  << " YMAT/YTD textured sections; "
                  << skeleton.joints.size() << " YMT joints\n";

        auto const selected_pistol=northstarcpp::load_scene_mesh(
            manager,pistol_path+"@pistol");
        require(selected_pistol.indices.size()==pistol_mesh.indices.size(),
            "YDD dictionary selector changed triangle count");

        for(auto const& entry:pistol_drawables) {
            std::size_t triangles=0;
            for(auto const& part:entry.meshes)
                triangles+=part.indices.size()/3;
            std::cout<<"Pistol original YDD drawable: "<<entry.name
                     <<" mesh parts="<<entry.meshes.size()
                     <<" triangles="<<triangles<<"\n";
            require(triangles>0,"Original Pistol YDD drawable has no geometry");
        }

        require(texture_entries.size()==5 && drawable_entries.size()==1,
            "NEF8 ListFile entries were truncated during enumeration");
        require(drawable_entries[0].name=="skydome_high",
            "Drawable selector name differs from ListFile entry");
        for(auto const& entry:texture_entries) {
            auto loaded=manager.load_texture("textures/skydome.ytd@"+entry.name);
            require(loaded.name==entry.name && !loaded.data.empty(),
                "NEF8 dictionary selection returned the wrong texture");
            require(loaded.data.size()==entry.size,
                "NEF8 texture entry's byte length does not match its payload");
            if(entry.name=="moon_new")
                require(loaded.format_id==103 && loaded.srgb,
                    "Legacy DXT5 moon must use BC3, not the wrong BC7 tag");
        }
        std::string const sky_ref = "models/skydome.ydd@skydome_high";
        auto first = manager.request_drawable_resources(sky_ref, northstarcpp::AssetPriority::background);
        auto duplicate = manager.request_drawable_resources(sky_ref, northstarcpp::AssetPriority::critical);
        auto const sky_resource = first.get();
        require(first.ready() && sky_resource == duplicate.get(), "Identical streaming requests did not share their result");
        auto const& sky = *sky_resource;
        require(sky.drawable.meshes.size() == 1 && sky.materials.size() == 1 && sky.textures.size() == 3,
            "Incomplete skydome resource graph");
        require(sky.materials.front().reference == "materials/skydome.ymat@sky_cloud_clear",
            "Skydome should reference a native YMAT material");
        auto const& mesh = sky.drawable.meshes.front();
        require(mesh.vertices.size() == 469 && mesh.indices.size() == 2784,
            "Incorrect skydome mesh buffers");
        require(sky.materials.front().asset.name == "sky_cloud_clear" &&
            sky.materials.front().asset.shader == "pbr.unlit.sky" && sky.materials.front().asset.two_sided,
            "Incorrect skydome material");
        auto moon_request = manager.request_texture("textures/skydome.ytd@moon_new");
        auto moon_resource = moon_request.get();
        auto const& moon = *moon_resource;
        require(moon.srgb && moon.mips.size() == 8 && moon.mips.front().width == 128 &&
            moon.mips.back().width == 1 && moon.data.size() == 21872,
            "Incorrect native texture payload");
        auto const leaf = manager.load_material("materials/fps/game_ready_world.ymat@tree_leaf");
        require(leaf.alpha_cutoff > 0.44f && leaf.alpha_cutoff < 0.46f &&
            !leaf.parameters.empty(), "Incorrect FPS material");
        // world_surfaces_runtime.ytd is a normalized copy of the supplied
        // world_surfaces.ytd; the original malformed indexes are untouched.
        if(std::filesystem::is_regular_file(
                root/"textures"/"world_surfaces_runtime.ytd")) {
            auto const entries=northstarcpp::shared_assets::list_dictionary_entries(
                manager,"textures/world_surfaces_runtime.ytd");
            require(entries.size()==9,"World surface dictionary lost entries");
            for(auto const& entry:entries) {
                auto tex=manager.load_texture(
                    "textures/world_surfaces_runtime.ytd@"+entry.name);
                require(tex.data.size()==entry.size && !tex.mips.empty(),
                    "World surface texture mip bounds invalid");
                if(entry.name.find("_normal")!=std::string::npos ||
                   entry.name.find("_roughness")!=std::string::npos)
                    require(tex.format_id==105 && !tex.srgb,
                        "World normals/roughness must remain BC5 linear data");
                else require(tex.format_id==102 && tex.srgb,
                    "World base color must be BC1 sRGB");
            }
        }
        // Regression: the Editor's native YDD path must resolve the same
        // relocated character YMAT/YTD references as the character runtime.
        if(std::filesystem::is_regular_file(root/
           "characters/abby/models/abby.ydd")) {
            auto model=northstarcpp::load_native_model_resource(
                manager,"characters/abby/models/abby.ydd");
            require(!model.mesh.indices.empty() && !model.mesh.sections.empty(),
                "Abby native YDD has no renderable geometry");
            std::size_t resolved=0;
            for(auto const& section:model.mesh.sections) {
                if(section.material_reference.empty())continue;
                require(section.material_reference.rfind(
                    "characters/abby/materials/abby.ymat@",0)==0,
                    "Editor native YDD bypassed canonical character YMAT namespace");
                if(model.materials.count(section.material_reference))++resolved;
            }
            require(resolved>0,
                "Editor native YDD failed to decode any character material");
            std::cout<<"Abby editor model: "<<model.mesh.sections.size()
                <<" mesh parts, "<<resolved<<" material sections\n";
        }
        auto const info = northstarcpp::shared_assets::inspect_directory(root);
        require(info.materials >= 7 && info.drawables >= 1 && info.textures >= 5 &&
            info.scripts >= 7 && info.shaders >= 4 &&
            info.resolved_materials >= 1 && info.resolved_textures >= 3,
            "Shared asset inventory is incomplete");
        try {
            manager.request_texture("textures/skydome.ytd@missing").get();
            throw std::logic_error("Missing texture selector was accepted");
        } catch (std::runtime_error const& error) {
            require(std::string(error.what()).find("Asset entry not found") != std::string::npos,
                "Wrong missing-selector error");
        }

        manager.set_streaming_budget(0);
        moon_request = {};
        require(manager.streaming_stats().resident == 2, "A live decoded handle must pin its resource");
        moon_resource.reset();
        require(manager.streaming_stats().resident == 1, "An unpinned texture was not evicted");

        auto zones = northstarcpp::parse_asset_zones(R"({
            "version":1,"zones":[
                {"min":[0,0,0],"max":[10,10,10],"drawables":["models/skydome.ydd@skydome_high"]},
                {"min":[5,0,0],"max":[20,10,10],"drawables":["models/skydome.ydd@skydome_high"]}
            ]})");
        northstarcpp::AssetZones streamer(manager, std::move(zones), 10.f);
        streamer.update({-5.f, 0.f, 0.f});
        require(streamer.find(sky_ref).get() == sky_resource, "Zone prefetch did not reuse loaded resources");
        streamer.update({12.f, 0.f, 0.f});
        require(streamer.find(sky_ref).get() == sky_resource, "Overlapping zones duplicated the resource");
        streamer.update({100.f, 0.f, 0.f});
        require(!streamer.find(sky_ref), "Exited zone did not release its request");

        manager.mount_directory("test-overlay", root);
        require(first.stale() && sky_resource->drawable.name == "skydome_high",
            "Mount changes must invalidate cache without breaking existing handles");
        auto refreshed = manager.request_drawable_resources(sky_ref);
        require(!refreshed.stale() && refreshed.get() != sky_resource,
            "A new VFS mount reused a stale resource");
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
