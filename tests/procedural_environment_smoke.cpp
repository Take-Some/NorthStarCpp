#include <northstarcpp/procedural_environment.hpp>
#include <northstarcpp/scene_world.hpp>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
static void check(bool value,char const* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        northstarcpp::CloudNoiseSettings settings{2026u,128u,128u};
        auto pixels=northstarcpp::make_perlin_cloud_rgba(settings);
        check(pixels.size()==128u*128u*4u,"Cloud payload size is invalid");
        check(pixels==northstarcpp::make_perlin_cloud_rgba(settings),
              "Identical seed must reproduce cloud pixels");
        std::uint8_t lo=255,hi=0;
        for (std::size_t i=0;i<pixels.size();i+=4) {
            check(pixels[i+3]==255,"RGBA8 alpha is not fully opaque");
            lo=std::min(lo,pixels[i]);hi=std::max(hi,pixels[i]);
        }
        check(hi-lo>40,"Cloud noise texture is constant");
        settings.seed++;
        check(pixels!=northstarcpp::make_perlin_cloud_rgba(settings),
              "Cloud seed does not affect output");
        northstarcpp::PerlinWorldSettings world;
        auto generated=northstarcpp::make_perlin_world(world);
        northstarcpp::validate_scene(generated);
        check(generated.version==3 && !generated.cells.empty(),
              "Generated world lacks v3 streaming cells");
        check(generated.objects.size()>world.columns*world.rows,
              "Generated world lacks layered terrain");
        check(generated.objects.size()<=northstarcpp::max_scene_objects,
              "Generated world exceeds object cap");
        unsigned players=0,solid=0;
        for (auto const& o: generated.objects) {
            if (o.player) ++players;
            else if(o.mass==0 && o.node.collision && !o.node.cell.empty()) ++solid;
        }
        check(players==1 && solid>world.columns*world.rows,
              "Generated world lacks collision or unique player spawn");
        auto serialized=northstarcpp::serialize_scene_v3(generated);
        auto decoded=northstarcpp::parse_scene_v3(serialized);
        check(northstarcpp::serialize_scene_v3(decoded)==serialized,
              "Generated world cannot roundtrip through scene V3");
        northstarcpp::SceneWorld streaming(decoded);
        streaming.update(decoded.objects.back().position);
        check(streaming.loaded_cell_count()>0,"Procedural streaming cells are not loadable");
        check(serialized==northstarcpp::serialize_scene_v3(northstarcpp::make_perlin_world(world)),
              "Generated world is not deterministic");
        world.seed++;
        check(serialized!=northstarcpp::serialize_scene_v3(northstarcpp::make_perlin_world(world)),
              "World seed has no effect");
        bool rejected=false;
        try { world.columns=65; (void)northstarcpp::make_perlin_world(world); }
        catch(std::invalid_argument const&) {rejected=true;}
        check(rejected,"Unsafe world dimensions were accepted");
        std::cout<<"Procedural cloud RGBA + scene V3 + physics flags + streaming: PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
