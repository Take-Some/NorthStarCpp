// Real NEF8/NEF16 YPT particle dictionaries and Content Browser integration.
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using namespace northstarcpp::editor;
static void check(bool ok,char const* error) {
    if(!ok)throw std::runtime_error(error);
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    try {
        auto const engine=fs::path(argv[1]);
        auto const fixture=fs::path(argv[2]);
        check(fs::is_regular_file(engine/"particles/core.ypt") &&
              fs::is_regular_file(engine/"particles/core_snow.ypt"),
              "Expected two native shared YPT particle dictionaries");
        check(kind_from_extension("core.YPT")==AssetKind::particle &&
              std::string(kind_name(AssetKind::particle))=="Particle",
              "YPT extension classification is not case insensitive");
        AssetCatalog catalog;
        catalog.scan(engine);
        for(auto const& filename:{"core.ypt","core_snow.ypt"}) {
            auto const path=std::string("particles/")+filename;
            auto const* parent=catalog.find(path);
            check(parent && parent->is_list_file() && parent->directory &&
                  parent->virtual_child_count==
                      (std::string(filename)=="core.ypt"?48u:84u),
                  "Valid YPT must be a virtual particle dictionary");
            std::vector<std::uint8_t> source;
            {
                std::ifstream stream(engine/fs::u8path(path),std::ios::binary);
                source.assign(std::istreambuf_iterator<char>(stream),
                              std::istreambuf_iterator<char>());
            }
            auto list=northstarcpp::shared_assets::list_dictionary_entries(source,path);
            check(list.size()==parent->virtual_child_count &&
                  !list.front().name.empty(),
                  "YPT effect, emitter and particle decoding failed");
            auto const virtual_path=path+"/"+list.front().name;
            auto const* child=catalog.find(virtual_path);
            check(child && child->kind==AssetKind::particle &&
                  child->is_virtual_entry() &&
                  child->container_path==path &&
                  child->entry_name==list.front().name,
                  "YPT virtual particle child missing");
            check(catalog.asset_reference(virtual_path)==
                  path+"@"+list.front().name,
                  "YPT particle identity must use path@entry");
            check(catalog.backing_file(virtual_path)==
                  (engine/fs::u8path(path)).lexically_normal(),
                  "YPT child must resolve to the real dictionary file");
            bool blocked=false;
            try{(void)catalog.resolve(virtual_path);}
            catch(std::invalid_argument const&){blocked=true;}
            check(blocked,"Virtual particle must not be writable as a physical file");
        }
        // WolvenKit CR2W JSON -> native NEF8/NEF16 YPT conversion must be
        // accepted by the SAME production decoder and virtual asset browser.
        auto const converted="particles/weapon/pistol_lexington.ypt";
        check(fs::is_regular_file(engine/converted),
              "Converted weapon YPT is missing from shared Assets");
        std::vector<std::uint8_t> weapon_source;
        {
            std::ifstream input(engine/converted,std::ios::binary);
            weapon_source.assign(std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>());
        }
        auto const converted_list=
            northstarcpp::shared_assets::list_dictionary_entries(
                weapon_source,converted);
        check(converted_list.size()==24u,
              "Converted YPT must contain 4 effects, 10 emitters, 10 particles");
        auto const raw_json=
            northstarcpp::shared_assets::decode_particle_runtime_json(
                weapon_source);
        check(raw_json.find("muzzle_round_smoke")!=std::string::npos &&
              raw_json.find("shell_reference_editor_only")!=std::string::npos,
              "Converted REDengine parameters were not preserved");
        auto const* shot=catalog.find(
            "particles/weapon/pistol_lexington.ypt/effect.pistol_shot");
        check(shot && shot->is_virtual_entry() &&
              catalog.asset_reference(
                  "particles/weapon/pistol_lexington.ypt/effect.pistol_shot")==
                  "particles/weapon/pistol_lexington.ypt@effect.pistol_shot",
              "Converted weapon shot is not available in the Asset Browser");
        // Each translated system is also a standalone native dictionary.
        for(auto const& item:std::array<std::pair<char const*,std::size_t>,3>{{
                {"particles/weapon/pistol_muzzle.ypt",11u},
                {"particles/weapon/pistol_ejection.ypt",9u},
                {"particles/weapon/pistol_shell_reference.ypt",3u}}}) {
            std::vector<std::uint8_t> payload;
            std::ifstream stream(engine/item.first,std::ios::binary);
            check(bool(stream),"Converted YPT component is missing");
            payload.assign(std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>());
            auto children=northstarcpp::shared_assets::list_dictionary_entries(
                payload,item.first);
            check(children.size()==item.second,
                  "Converted YPT component entry count mismatch");
            auto const* catalog_entry=catalog.find(item.first);
            check(catalog_entry && catalog_entry->is_list_file() &&
                  catalog_entry->virtual_child_count==children.size(),
                  "Converted standalone YPT component is not browsable");
        }
        catalog.scan(engine);
        check(catalog.dictionary_cache_hits()>=2,
              "Warm particle dictionary catalog scan missed DDC");
        fs::create_directories(fixture);
        fs::copy_file(engine/"particles/core.ypt",fixture/"bad.ypt",
            fs::copy_options::overwrite_existing);
        // Damage only this disposable fixture. An invalid YPT must remain
        // visible as a file rather than disappear or crash the browser.
        {
            std::fstream bad(fixture/"bad.ypt",std::ios::binary|
                             std::ios::in|std::ios::out);
            bad.write("FAIL",4);
        }
        AssetCatalog broken;
        broken.scan(fixture);
        auto const* failed=broken.find("bad.ypt");
        check(failed && !failed->directory &&
              failed->kind==AssetKind::particle,
              "Corrupt YPT should remain visible without virtual entries");
        std::cout<<"Particle YPT: core/core_snow indexed, icons/types enabled, "
                    "warm cached, broken file isolated\n";
        return 0;
    }catch(std::exception const& ex) {
        std::cerr<<"YPT catalog smoke: "<<ex.what()<<"\n";
        return 1;
    }
}
