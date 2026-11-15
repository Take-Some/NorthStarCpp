// Native VFS material acceptance: JSON, raster image and original NEF8/YMAT.
// The build never modifies user's source projects or shared assets.
#include <northstarcpp/assets.hpp>
#include <northstarcpp/material.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
namespace fs=std::filesystem;
void require(bool ok,char const* message) {
    if(!ok)throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto const shared=fs::u8path(argv[1]);
    auto const directory=fs::temp_directory_path()/
        ("northstar-material-loader-"+std::to_string(
#ifdef _WIN32
          GetCurrentProcessId()
#else
          42
#endif
        ));
    struct Cleanup {
        fs::path path;
        ~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}
    } cleanup{directory};
    try {
        fs::create_directories(directory/"assets"/"Materials");
        fs::create_directories(directory/"assets"/"Textures");
        // Exact 2x2 24bpp BMP, row pitch 8; WIC converts it into RGBA8.
        std::array<unsigned char,70> bmp{};
        bmp[0]='B';bmp[1]='M';bmp[2]=70;
        bmp[10]=54;bmp[14]=40;bmp[18]=2;bmp[22]=2;
        bmp[26]=1;bmp[28]=24;bmp[34]=16;
        // bottom row BGR: red, green
        bmp[54]=0;bmp[55]=0;bmp[56]=255;
        bmp[57]=0;bmp[58]=255;bmp[59]=0;
        // top row: blue, white
        bmp[62]=255;bmp[63]=0;bmp[64]=0;
        bmp[65]=255;bmp[66]=255;bmp[67]=255;
        {
            std::ofstream out(directory/"assets"/"Textures"/"palette.bmp",
                              std::ios::binary);
            out.write(reinterpret_cast<char const*>(bmp.data()),bmp.size());
        }
        {
            std::ofstream out(directory/"assets"/"Materials"/"test.material.json");
            out<<R"({"schema":1,"name":"Test","shader":"standard_lit",
                "base_color":"#804020","metallic":0.2,"roughness":0.75,
                "base_color_texture":"Textures/palette.bmp",
                "normal_texture":"","roughness_texture":"",
                "metallic_texture":"","emissive_texture":""})";
        }
        northstarcpp::AssetStore assets;
        assets.mount_project_assets(directory,shared);
        auto loaded=northstarcpp::load_material_definition(
            assets,"Materials/test.material.json");
        require(loaded.textures[0].width==2 && loaded.textures[0].height==2 &&
                loaded.textures[0].rgba8.size()==16,"PNG/BMP material VFS decode failed");
        require(std::abs(loaded.roughness-.75f)<.00001f &&
                std::abs(loaded.metallic-.2f)<.00001f,"Material factors incorrect");
        require(loaded.base_color[0]>.18f && loaded.base_color[0]<.24f,
                "Hex sRGB to linear failed");
        std::cout<<"JSON + WIC raster + VFS material: PASS\n";
        // Native engine material reads original YMAT via sharedAssets; do
        // not rewrite the source to a JSON proxy.
        auto original=northstarcpp::load_material_definition(
            assets,"materials/skydome.ymat");
        require(original.base_color[0]>=0 && original.base_color[0]<=1,
                "Original YMAT base color invalid");
        std::cout<<"Original YMAT + YTD native material: PASS\n";
        // The engine also ships real NEF8 kind 30 JSON YMAT libraries for
        // skeletal characters and weapons. They must load via the same
        // material parser that powers GPU material assignment and previews.
        auto const abby=northstarcpp::shared_assets::load_materials(
            assets,"characters/abby/materials/abby.ymat");
        require(abby.size()==35 && abby.front().name=="m00" &&
                abby.front().shader=="pbr.default" &&
                !abby.front().textures.empty(),
                "Character JSON YMAT failed native material parsing");
        auto const pistol=northstarcpp::shared_assets::load_materials(
            assets,"materials/weapon_pistol.ymat");
        require(pistol.size()==4 && pistol.front().name=="m00" &&
                pistol.front().textures.front().slot=="base_color",
                "Weapon JSON YMAT failed native material parsing");
        auto const pistol_entry=assets.load_material(
            "materials/weapon_pistol.ymat@m01");
        require(pistol_entry.name=="m01" && pistol_entry.shader=="pbr.default",
                "Weapon JSON YMAT @entry selection failed");
        auto const entries=northstarcpp::shared_assets::list_dictionary_entries(
            assets,"characters/abby/materials/abby.ymat");
        require(entries.size()==abby.size() && entries.front().name=="m00",
                "Character JSON YMAT cannot be enumerated by Content Browser");
        auto source=assets.read("materials/weapon_pistol.ymat");
        auto const imported=northstarcpp::shared_assets::load_materials_from_bytes(
            source,"materials/weapon_pistol.ymat");
        require(imported.size()==4 && imported[1].name=="m01",
                "Source-bytes weapon YMAT preview cannot decode kind 30");
        require(pistol[1].parameters.size()>=2 &&
                pistol[1].blend=="opaque" && !pistol[1].two_sided,
                "Weapon JSON YMAT PBR parameters were lost");
        // Refs are retained verbatim. Unavailable imported texture
        // dictionaries must not become invented or silently rewritten.
        require(pistol.front().textures.front().reference==
                "textures/weapon/pistol.ytd@m00_base",
                "JSON YMAT imported texture reference was rewritten");
        std::cout<<"Character + weapon NEF8 kind 30 YMAT: PASS\n";
        bool rejected=false;
        try {
            (void)northstarcpp::load_material_definition(assets,
                "../Materials/test.material.json");
        }catch(std::exception const&){rejected=true;}
        require(rejected,"VFS material path traversal accepted");
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Material loader regression: "<<e.what()<<"\n";
        return 1;
    }
}
