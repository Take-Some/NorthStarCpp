// Native XML YMAT and VFS raster acceptance; legacy JSON material rejected.
// The build never modifies user's source projects or shared assets.
#include <northstarcpp/assets.hpp>
#include <northstarcpp/material.hpp>
#include <northstarcpp/material_ymat_authoring.hpp>
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
        auto const encoded=northstarcpp::shared_assets::encode_editor_ymat(
            R"({"schema":1,"name":"Test","shader":"standard_lit",
                "base_color":"#804020","metallic":0.2,"roughness":0.75,
                "base_color_texture":"","normal_texture":"",
                "roughness_texture":"","metallic_texture":"",
                "emissive_texture":""})");
        {
            std::ofstream out(directory/"assets"/"Materials"/"test.ymat",
                              std::ios::binary);
            out.write(reinterpret_cast<char const*>(encoded.data()),encoded.size());
        }
        northstarcpp::AssetStore assets;
        assets.mount_project_assets(directory,shared);
        auto loaded=northstarcpp::load_material_definition(
            assets,"Materials/test.ymat");
        // BMP/JPEG/PNG decoding remains available as a separate image
        // service, not as a JSON material texture binding.
        auto raster=northstarcpp::load_native_texture_image(
            assets,"Textures/palette.bmp");
        require(raster.width==2 && raster.height==2 && raster.rgba8.size()==16,
                "WIC raster VFS decode failed");
        require(std::abs(loaded.roughness-.75f)<.00001f &&
                std::abs(loaded.metallic-.2f)<.00001f,"Native XML material factors incorrect");
        require(loaded.base_color[0]>.18f && loaded.base_color[0]<.24f,
                "Native XML YMAT color conversion incorrect");
        std::cout<<"XML YMAT + WIC raster VFS: PASS\n";
        // Native engine material reads original YMAT via sharedAssets; do
        // not rewrite the source to a JSON proxy.
        auto original=northstarcpp::load_material_definition(
            assets,"materials/skydome.ymat");
        require(original.base_color[0]>=0 && original.base_color[0]<=1,
                "Original YMAT base color invalid");
        std::cout<<"Original YMAT + YTD native material: PASS\n";
        // Character and weapon YMAT must use canonical XML NEF8 kind4.
        auto const abby=northstarcpp::shared_assets::load_materials(
            assets,"characters/abby/materials/abby.ymat");
        require(abby.size()==35 && abby.front().name=="m00" &&
                abby.front().shader=="pbr.default" &&
                !abby.front().textures.empty(),
                "Character XML YMAT failed native material parsing");
        auto const pistol=northstarcpp::shared_assets::load_materials(
            assets,"materials/weapon_pistol.ymat");
        require(pistol.size()==4 && pistol.front().name=="m00" &&
                pistol.front().textures.front().slot=="base_color",
                "Weapon XML YMAT failed native material parsing");
        auto const pistol_entry=assets.load_material(
            "materials/weapon_pistol.ymat@m01");
        require(pistol_entry.name=="m01" && pistol_entry.shader=="pbr.default",
                "Weapon XML YMAT @entry selection failed");
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

        // Both pistol and rifle case prefabs reference the same native YDD.
        // Its source material URI MUST be the supported YMAT entry, not an
        // obsolete .nemat or a non-mounted "shared/materials" prefix.
        auto const casings=northstarcpp::shared_assets::load_drawables(
            assets,"models/weapon/pistol/pistol_shell.ydd");
        require(casings.size()==1 && !casings.front().meshes.empty(),
                "Physical shell YDD has no render sections");
        constexpr char brass_ref[]="materials/weapon_shell.ymat@brass";
        for(auto const& mesh:casings.front().meshes) {
            require(mesh.material==brass_ref,
                    "Pistol/rifle shell embedded material reference is not canonical YMAT");
        }
        auto const brass=assets.load_material(brass_ref);
        require(brass.name=="brass" && brass.shader=="pbr.default",
                "Missing original native YMAT brass entry");
        auto const brass_gpu=northstarcpp::load_material_definition(
            assets,brass_ref);
        require(brass_gpu.textures[0].width>0 &&
                brass_gpu.textures[0].height>0 &&
                brass_gpu.textures[0].rgba8.size()==
                    std::size_t(brass_gpu.textures[0].width)*
                    brass_gpu.textures[0].height*4,
                "Physical brass shells do not have a valid YTD albedo");
        require(brass_gpu.metallic>.8f && brass_gpu.roughness>.2f &&
                brass_gpu.roughness<.5f,
                "Brass material PBR metallic/roughness values were lost");
        std::cout<<"Pistol/rifle shell native YDD -> YMAT@brass -> YTD: PASS\n";

        // This QA is opportunistic: full-resolution Naughty Dog source
        // textures are private, ignored by Git and absent from clean CI.
        // If installed locally, exercise exactly the production VFS/BCn
        // material path, including the new masked base-alpha adapter.
        bool has_private_ellie=false;
        try {
            has_private_ellie=
                assets.size("characters/ellie/textures/ellie.ytd")>
                    64ull*1024*1024;
        }catch(std::exception const&) {}
        if(has_private_ellie) {
            northstarcpp::MaterialLoadSession ellie_session(assets);
            auto const head=ellie_session.load(
                "characters/ellie/materials/ellie.ymat@"
                "mat_6a62136608a09d90_ellie-head");
            require(head.blend_mode==1 &&
                    head.textures[0].width>=256 &&
                    head.textures[0].height>=256 &&
                    head.textures[0].rgba8.size()==
                        std::size_t(head.textures[0].width)*
                        head.textures[0].height*4u,
                "Original Ellie hair material has no full-res color or masked alpha");
            auto const eyes=ellie_session.load(
                "characters/ellie/materials/ellie.ymat@"
                "mat_06bc3e1c7790b27c_ellie-head");
            require(!eyes.textures[0].rgba8.empty() &&
                    eyes.textures[0].width>=64 &&
                    eyes.roughness<0.2f,
                "Original Ellie eye material has no sclera and glassy PBR fallback");
            std::cout<<"Private Ellie fullres/alpha/eyes VFS runtime: PASS\n";
            auto debug_mean=[](northstarcpp::MaterialImage const& tex) {
                if(tex.rgba8.empty())return 0.;
                std::uint64_t sum=0;
                for(std::size_t i=0;i<tex.rgba8.size();i+=4)
                    sum+=std::uint64_t(tex.rgba8[i])+
                         tex.rgba8[i+1]+tex.rgba8[i+2];
                return double(sum)/(tex.rgba8.size()/4)/3.;
            };
            auto const face=ellie_session.load(
                "characters/ellie/materials/ellie.ymat@"
                "mat_34b53614ceaace8d_ellie-head");
            std::cout<<"Ellie source BCn albedo averages [0..255]: "
                     <<"head="<<debug_mean(face.textures[0])
                     <<" eyes="<<debug_mean(eyes.textures[0])
                     <<" hair="<<debug_mean(head.textures[0])
                     <<static_cast<char>(10);

            // Regression: the canonical outfit must resolve every actual
            // original YMAT slot through ONE shared YTD cache. A previous
            // 15-part partial import left the hair/jacket/gear out entirely.
            auto const ellie=northstarcpp::shared_assets::load_drawables(
                assets,"characters/ellie/models/ellie.ydd");
            require(ellie.size()==1 && ellie.front().meshes.size()==38,
                    "Ellie canonical YDD is incomplete");
            std::size_t with_albedo=0,with_mask=0;
            std::size_t triangles=0;
            northstarcpp::MaterialLoadSession all_ellie_materials(assets);
            for(auto const& part:ellie.front().meshes) {
                require(!part.material.empty() &&
                    part.material.find("characters/ellie/materials/ellie.ymat@")==0,
                    "Ellie part did not receive original YMAT binding");
                auto const value=all_ellie_materials.load(part.material);
                triangles+=part.indices.size()/3;
                if(!value.textures[0].rgba8.empty()) {
                    ++with_albedo;
                    require(value.textures[0].width>0 &&
                        value.textures[0].rgba8.size()==
                        std::size_t(value.textures[0].width)*
                        value.textures[0].height*4,
                        "Ellie linked texture is malformed");
                }
                if(value.blend_mode==1)++with_mask;
            }
            require(with_albedo>=31 && triangles==147367,
                "Ellie canonical outfit is missing original textured materials");
            std::cout<<"Ellie complete outfit/material links: "
                     <<ellie.front().meshes.size()<<" meshes, "
                     <<triangles<<" triangles, "
                     <<with_albedo<<" textured, "<<with_mask
                     <<" masked: PASS\n";

        }
        // Private Isaac game assets are ignored by Git. Exercise the *real*
        // materials, fullres YTD decoder and source drawable bindings only
        // when the original local source content is present.
        bool has_private_isaac=false;
        try {
            has_private_isaac=assets.size("characters/isaac/textures/isaac.ytd")>
                10ull*1024*1024;
        }catch(std::exception const&) {}
        if(has_private_isaac) {
            auto const source=northstarcpp::shared_assets::load_drawables(
                assets,"characters/isaac/models/isaac.ydd");
            require(source.size()==1 && source.front().meshes.size()==22,
                    "Native Isaac body/head drawable missing original mesh parts");
            northstarcpp::MaterialLoadSession isaac_session(assets);
            std::size_t textured=0,masked=0,triangles=0;
            for(auto const& part:source.front().meshes) {
                require(part.material.find("characters/isaac/materials/isaac.ymat@")==0,
                        "Isaac mesh lost its native material binding");
                auto const material=isaac_session.load(part.material);
                triangles+=part.indices.size()/3;
                if(!material.textures[0].rgba8.empty()) {
                    ++textured;
                    auto const& tex=material.textures[0];
                    require(tex.width>0 && tex.height>0 &&
                            tex.rgba8.size()==std::size_t(tex.width)*tex.height*4,
                            "Isaac fullres albedo is malformed");
                }
                if(material.blend_mode==1)++masked;
            }
            auto const eye=isaac_session.load(
                "characters/isaac/materials/isaac.ymat@"
                "mat_7d3a58427bb3dbb5_isaac-head_00083870");
            require(!eye.textures[0].rgba8.empty() &&
                    eye.textures[0].width>=256 &&
                    eye.roughness<0.2f,
                    "Isaac eyes have no original sclera/iris PBR fallback");
            require(textured>=16 && masked>=5 && triangles==110029,
                    "Isaac original fullres materials were not applied to all meshes");
            std::cout<<"Isaac fullres material/skinning-preview VFS: "
                     <<source.front().meshes.size()<<" meshes, "
                     <<textured<<" textured, "<<masked<<" masked, "
                     <<triangles<<" triangles: PASS"<<static_cast<char>(10);
        }
        bool rejected=false;
        try {
            (void)northstarcpp::load_material_definition(assets,
                "../Materials/test.ymat");
        }catch(std::exception const&){rejected=true;}
        require(rejected,"VFS material path traversal accepted");
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Material loader regression: "<<e.what()<<"\n";
        return 1;
    }
}
