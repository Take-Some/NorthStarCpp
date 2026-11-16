// Validate the real KaylaNightCity texture dictionary with the same
// AssetCatalog metadata and native codec used by NorthStar Content Browser.
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        const auto assets=fs::path(argv[1]);
        const auto file=assets/"textures"/"menu.ytd";
        if(!fs::is_regular_file(file))
            throw std::runtime_error("menu.ytd is missing from project assets");
        northstarcpp::editor::AssetCatalog catalog;
        catalog.scan(assets);
        auto const* dictionary=catalog.find("textures/menu.ytd");
        if(!dictionary || !dictionary->directory || !dictionary->is_list_file() ||
           dictionary->virtual_child_count!=2)
            throw std::runtime_error("Content Browser cannot expand menu.ytd as a valid YTD");
        auto const* item=catalog.find("textures/menu.ytd/bg");
        if(!item || !item->is_virtual_entry() ||
           item->kind!=northstarcpp::editor::AssetKind::texture ||
           item->entry_name!="bg" ||
           catalog.asset_reference(item->virtual_path)!="textures/menu.ytd@bg" ||
           catalog.backing_file(item->virtual_path)!=file.lexically_normal())
            throw std::runtime_error("Content Browser did not index menu.ytd/bg");
        auto const* logo=catalog.find("textures/menu.ytd/logo");
        if(!logo || !logo->is_virtual_entry() ||
           logo->kind!=northstarcpp::editor::AssetKind::texture ||
           logo->entry_name!="logo" ||
           catalog.asset_reference(logo->virtual_path)!="textures/menu.ytd@logo")
            throw std::runtime_error("Content Browser did not index menu.ytd/logo");
        std::ifstream stream(file,std::ios::binary);
        if(!stream)throw std::runtime_error("Cannot read menu.ytd");
        std::vector<std::uint8_t> raw(
            (std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
        const auto textures=northstarcpp::shared_assets::load_textures_from_bytes(
            raw,"textures/menu.ytd");
        if(textures.size()!=2 || textures.front().name!="bg" ||
           textures.front().format_id!=1 || textures.front().mips.size()!=1 ||
           textures.front().mips[0].width!=1672 ||
           textures.front().mips[0].height!=941 ||
           textures.front().mips[0].size!=1672u*941u*4u ||
           textures.front().data.size()!=1672u*941u*4u)
            throw std::runtime_error("Native YTD decoder rejected or altered BG pixels");
        auto const& logoTexture=textures.at(1);
        if(logoTexture.name!="logo" || logoTexture.format_id!=1 ||
           logoTexture.mips.size()!=1 || logoTexture.mips.front().width!=1672 ||
           logoTexture.mips.front().height!=941 ||
           logoTexture.data.size()!=1672u*941u*4u)
            throw std::runtime_error("Native YTD logo entry invalid");
        bool hasTransparency=false;
        for(std::size_t i=3;i<logoTexture.data.size();i+=4)
            if(logoTexture.data[i]<255) {hasTransparency=true;break;}
        if(!hasTransparency)
            throw std::runtime_error("Logo alpha channel lost during YTD conversion");
        std::cout<<"Content Browser: textures/menu.ytd/bg -> textures/menu.ytd@bg PASS\n";
        std::cout<<"Content Browser: textures/menu.ytd/logo -> textures/menu.ytd@logo PASS\n";
        std::cout<<"Native YTD RGBA8: 1672 x 941 PASS\n";
        return 0;
    } catch(std::exception const& ex) {
        std::cerr<<"menu.ytd smoke: "<<ex.what()<<'\n';
        return 1;
    }
}
