#include <algorithm>
// Decoded asset thumbnail regression.
#include "editor_asset_thumbnails.hpp"
#include <northstarcpp/shared_assets.hpp>
#include <cstdint>
#include <vector>
#include <northstarcpp/assets.hpp>
#include <fstream>
#include <iostream>
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
    // BC4 grayscale endpoints: the 8-byte block maps every pixel to 128.
    texture.format_id=102;
    texture.data={128,128,0,0,0,0,0,0};
    auto bc4=thumbnail_texture(texture);
    if(!bc4.valid()||bc4.bgra[48*96+48]!=0xff808080u)return 17;
    // BC3 solid red with opaque interpolated alpha.
    texture.format_id=103;
    texture.mips.front().size=16;
    texture.data={255,0,0,0,0,0,0,0,0,0xf8,0,0,0,0,0,0};
    auto bc3=thumbnail_texture(texture);
    if(!bc3.valid()||bc3.bgra[48*96+48]!=0xffff0000u)return 18;
    shared_assets::Material material{};
    material.name="steel";
    shared_assets::MaterialParameter param{};
    param.name="base_color";param.values={.9f,.3f,.2f};
    material.parameters.push_back(param);
    if(!thumbnail_material(material).valid())return 4;
    // Same encoded RGB bytes produce different *linear* lighting results
    // depending on NETD sRGB metadata. Never treat data textures as sRGB.
    auto pbr=material;
    pbr.parameters.clear();
    pbr.textures.push_back({"base_color","textures/sample.ytd@gray"});
    shared_assets::Texture gray{};
    gray.name="gray";
    gray.format_id=1;
    gray.srgb=false;
    gray.mips={{2,2,0,16}};
    gray.data={128,128,128,255,128,128,128,255,
               128,128,128,255,128,128,128,255};
    auto linearSphere=preview_material(
        pbr,{{"textures/sample.ytd@gray",gray}},128);
    gray.srgb=true;
    auto srgbSphere=preview_material(
        pbr,{{"textures/sample.ytd@gray",gray}},128);
    if(!linearSphere.valid() || !srgbSphere.valid() ||
       linearSphere.argb==srgbSphere.argb)return 34;
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
        if(textures.size()!=5 || drawables.size()!=1 || materials.size()!=3)
            return 12;
        std::vector<shared_assets::ResolvedAsset<shared_assets::Texture>> bindings;
        bindings.reserve(textures.size());
        for(auto const& tex:textures)
            bindings.push_back({"textures/skydome.ytd@"+tex.name,tex});
        std::vector<std::uint32_t> middle_colors;
        for(auto const& material:materials) {
            auto sphere=preview_material(material,bindings,256);
            auto plain=preview_material(material,{},256);
            auto orbit=preview_material(material,bindings,256,.85f,.15f);
            if(!sphere.valid() || !plain.valid() || !orbit.valid())
                return 25;
            // The surface (center pixel) must be affected by the shader
            // parameters or a genuine bound texture, not a generic icon.
            middle_colors.push_back(sphere.argb[128*256+128]);
            if(!material.textures.empty() &&
               sphere.argb==plain.argb)return 26;
            if(!material.textures.empty() &&
               orbit.argb==sphere.argb)return 27;
            auto small=thumbnail_material(material);
            if(!small.valid())return 28;
        }
        if(middle_colors[0]==middle_colors[1] &&
           middle_colors[1]==middle_colors[2])return 29;
        // All dictionary records must have a real decoded preview:
        // RGBA8, BC1, BC4 and BC7, not just a first-entry fallback.
        std::unordered_set<std::uint16_t> formats;
        for(auto const& texture : textures) {
            formats.insert(texture.format_id);
            auto thumbnail=thumbnail_texture(texture);
            if(!thumbnail.valid()) {
                std::cerr<<"No thumbnail for "<<texture.name
                         <<" (NETD format "<<texture.format_id<<")\n";
                return 13;
            }
        }
        if(formats.count(1)==0 || formats.count(101)==0 ||
           formats.count(102)==0 || formats.count(103)==0)return 14;
        // Input NETD declares moon_new as BC7, but its payload is BC3/DXT5.
        // An actual decoded grey moon is the regression oracle here, NOT a
        // purple/checkerboard result from interpreting BC3 as BC7.
        auto moon=std::find_if(textures.begin(),textures.end(),
            [](auto const& item){return item.name=="moon_new";});
        if(moon==textures.end() || moon->format_id!=103)return 30;
        auto moonImage=preview_texture(*moon,128);
        if(!moonImage.valid() || moonImage.width!=128 ||
           moonImage.height!=128)return 31;
        auto const center=moonImage.argb[64*128+64];
        int const r=int((center>>16)&255),g=int((center>>8)&255),
                  b=int(center&255);
        if((center>>24)<230 || r<80 || r>180 ||
           std::abs(r-g)>16 || std::abs(r-b)>16)return 32;
        // Legitimate BC7 remains supported; never globally remap 104.
        shared_assets::Texture nativeBC7{};
        nativeBC7.name="valid_bc7";
        nativeBC7.format_id=104;
        nativeBC7.mips={{4,4,0,16}};
        nativeBC7.data.assign(16,0);
        nativeBC7.data[0]=0x40; // valid BC7 mode 6
        if(!preview_texture(nativeBC7,96).valid())return 33;
        for(auto const& item:drawables)
            if(!thumbnail_drawable(item).valid())return 15;
        // Modal texture display renders from decoded source pixels rather
        // than scaling up a 96x96 thumbnail or confusing BC7 channels.
        for(auto const& asset:textures) {
            auto high=preview_texture(asset,1024);
            // A full-resolution preview must preserve small native mips.
            // For example, dither is 64x64; forcing a 96px minimum would
            // upscale its source and incorrectly fail a valid preview.
            auto const& native=asset.mips.front();
            if(!high.valid() ||
               high.width < std::min(96, static_cast<int>(native.width)) ||
               high.height < std::min(96, static_cast<int>(native.height)) ||
               high.width>static_cast<int>(native.width) ||
               high.height>static_cast<int>(native.height)) {
                std::cerr<<"Full preview mismatch: "<<asset.name<<" (native "
                         <<native.width<<"x"<<native.height
                         <<", preview "<<high.width<<"x"<<high.height<<")\n";
                return 19;
            }
            if(high.width>1024||high.height>1024)return 20;
            if(high.argb.size()!=std::size_t(high.width)*high.height)return 21;
        }
        auto model=preview_drawable(drawables.front(),640,640,.45f,.32f,1.f);
        auto rotated=preview_drawable(drawables.front(),640,640,1.55f,.17f,.85f);
        if(!model.valid() || !rotated.valid() ||
           model.argb==rotated.argb)return 22;
        auto wireframe=preview_drawable(drawables.front(),640,640,
                                        .45f,.32f,1.f,true);
        if(!wireframe.valid() || wireframe.argb==model.argb)return 23;
        // Invalid resolutions and declared mips are rejected before reads.
        if(preview_drawable(drawables.front(),10000,10000).valid() ||
           preview_texture(textures.front(),32).valid() ||
           preview_texture(textures.front(),1024,999).valid())return 24;
        // Corrupt compressed mip metadata must not read outside the buffer.
        auto invalid=textures.back();
        invalid.mips.front().size=7;
        if(thumbnail_texture(invalid).valid())return 16;
        if(textures.empty()||drawables.empty()||materials.empty())return 7;
        auto const tex=thumbnail_texture(textures.front());
        // An explicit dictionary@entry reference must preview THAT texture,
        // rather than unconditionally rendering the dictionary's first item.
        // The second entry uses supported BC1 compression (NETD format 101).
        // BC4 and BC7 are decoded independently via the pinned bcdec library.
        auto const selected=assets.load_texture("textures/skydome.ytd@dither");
        if(selected.name!="dither" || selected.name==textures.front().name)return 10;
        auto const selected_preview=thumbnail_texture(selected);
        if(!selected_preview.valid() || selected_preview.bgra==tex.bgra)return 11;
        auto const mesh=thumbnail_drawable(drawables.front());
        auto const mat=thumbnail_material(materials.front());
        if(!tex.valid()||!mesh.valid()||!mat.valid())return 8;
        std::unordered_set<std::uint32_t> colors(tex.bgra.begin(),tex.bgra.end());
        if(colors.size()<4)return 9; // real texture is not a flat placeholder
        auto output=std::filesystem::u8path(argv[2]);
        for(auto const& asset : textures)
            write_ppm(output/("nef8-texture-"+asset.name+".ppm"),
                      thumbnail_texture(asset));
        for(auto const& asset : drawables)
            write_ppm(output/("nef8-model-"+asset.name+".ppm"),
                      thumbnail_drawable(asset));
        write_ppm(output/"skydome-ytd.ppm",tex);
        write_ppm(output/"skydome-ydd.ppm",mesh);
        // Full-size orbit render is available through the reusable editor API.
        auto const output3d=output/"skydome-modal-orbit.ppm";
        std::ofstream rendered(output3d,std::ios::binary);
        rendered<<"P6\n"<<model.width<<" "<<model.height<<"\n255\n";
        for(auto const argb:model.argb) {
            char rgb[]{char((argb>>16)&255),char((argb>>8)&255),char(argb&255)};
            rendered.write(rgb,3);
        }
        write_ppm(output/"skydome-ymat.ppm",mat);
    }
    return 0;
}
