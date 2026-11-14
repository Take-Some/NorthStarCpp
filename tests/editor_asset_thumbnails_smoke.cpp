// Decoded asset thumbnail regression.
#include "editor_asset_thumbnails.hpp"
#include <northstarcpp/shared_assets.hpp>
#include <cstdint>
#include <vector>
#include <northstarcpp/assets.hpp>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <string>
static void write_ppm(std::filesystem::path file,northstarcpp::editor_tools::Thumbnail const& t) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file,std::ios::binary);
    out<<"P6\n96 96\n255\n";
    for(auto const bgra:t.bgra) {
        char const bytes[]{char((bgra>>16)&255),char((bgra>>8)&255),char(bgra&255)};
        out.write(bytes,3);
    }
}
int main(int argc,char** argv) {
    using namespace northstarcpp;
    using namespace northstarcpp::editor_tools;
    shared_assets::Texture texture{};
    texture.format_id=1;texture.name="test";
    texture.mips.push_back({2,2,0,16});
    texture.data={255,0,0,255, 255,0,0,255,
                  255,0,0,255, 255,0,0,255};
    auto image=thumbnail_texture(texture);
    if(!image.valid() || image.bgra[48*96+48]!=0xffff0000u)return 1;
    texture.data.resize(4);
    if(thumbnail_texture(texture).valid())return 2;
    texture.data.assign(16,0);
    texture.format_id=101;
    texture.mips[0]={4,4,0,8};
    texture.data.resize(8);
    // BC1 solid red 565.
    texture.data={0,0xf8,0,0,0,0,0,0};
    auto compressed=thumbnail_texture(texture);
    if(!compressed.valid()||compressed.bgra[48*96+48]!=0xffff0000u)return 3;
    shared_assets::Material material{};
    material.name="steel";
    shared_assets::MaterialParameter param{};
    param.name="base_color";param.values={.9f,.3f,.2f};
    material.parameters.push_back(param);
    if(!thumbnail_material(material).valid())return 4;
    shared_assets::Drawable drawable{};
    shared_assets::MeshPart mesh{};
    mesh.vertices={
        {{-1.f,0.f,0.f},{0.f,1.f,0.f},{0.f,0.f}},
        {{1.f,0.f,0.f},{0.f,1.f,0.f},{1.f,0.f}},
        {{0.f,1.f,0.f},{0.f,1.f,0.f},{.5f,1.f}}};
    mesh.indices={0,1,2};
    drawable.meshes.push_back(mesh);
    auto preview=thumbnail_drawable(drawable);
    if(!preview.valid())return 5;
    std::size_t colored=0;
    for(auto const p:preview.bgra) if(p==0xff82cee4u) ++colored;
    if(colored==0)return 6;
    if(argc>=3) {
        // Real VFS/codec integration: no direct file parsing in the editor.
        AssetManager assets;
        assets.mount_shared_assets(std::filesystem::u8path(argv[1]));
        auto textures=shared_assets::load_textures(assets,"textures/skydome.ytd");
        auto drawables=shared_assets::load_drawables(assets,"models/skydome.ydd");
        auto materials=shared_assets::load_materials(assets,"materials/skydome.ymat");
        if(textures.empty()||drawables.empty()||materials.empty())return 7;
        auto const tex=thumbnail_texture(textures.front());
        auto const mesh=thumbnail_drawable(drawables.front());
        auto const mat=thumbnail_material(materials.front());
        if(!tex.valid()||!mesh.valid()||!mat.valid())return 8;
        std::unordered_set<std::uint32_t> colors(tex.bgra.begin(),tex.bgra.end());
        if(colors.size()<4)return 9; // real texture is not a flat placeholder
        auto output=std::filesystem::u8path(argv[2]);
        write_ppm(output/"skydome-ytd.ppm",tex);
        write_ppm(output/"skydome-ydd.ppm",mesh);
        write_ppm(output/"skydome-ymat.ppm",mat);
    }
    return 0;
}
