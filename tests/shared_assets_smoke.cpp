// NorthStarCpp
// Author: Kayla Verner
#include <northstarcpp/assets.hpp>

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
