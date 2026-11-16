#include <northstarcpp/nac.hpp>
#include <northstarcpp/editor_asset_catalog.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
using namespace northstarcpp;
using namespace northstarcpp::editor;
void check(bool condition) { if(!condition)throw std::runtime_error("NAC catalog smoke failure"); }
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        const auto root=fs::u8path(argv[1]);
        fs::create_directories(root);
        nac::Track a; a.id=42; a.name="Engine"; a.channels=1;
        a.sample_rate=48000; a.samples.assign(256,2000);
        nac::Track b; b.id=91; b.name="Horn"; b.channels=2;
        b.sample_rate=22050; b.samples.assign(512,500);
        std::vector<std::uint8_t> bytes;
        std::string error;
        check(nac::encode({a,b},bytes,error));
        {
            std::ofstream out(root/"vehicle.nac",std::ios::binary);
            out.write(reinterpret_cast<char const*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
        { std::ofstream(root/"broken.nac")<<"NOT A NAC"; }
        check(kind_from_extension("VEHICLE.NAC")==AssetKind::audio);
        AssetCatalog catalog;
        catalog.scan(root);
        auto const* container=catalog.find("vehicle.nac");
        check(container && container->directory && container->is_list_file() &&
              container->virtual_child_count==2);
        for(auto id: {42u,91u}) {
            auto path="vehicle.nac/track-"+std::to_string(id);
            auto const* item=catalog.find(path);
            check(item && item->is_virtual_entry() && !item->directory &&
                  item->kind==AssetKind::audio && item->audio_track_id==id &&
                  !item->filename.empty() && item->audio_sample_rate!=0);
            check(item->asset_reference()=="vehicle.nac@track-"+std::to_string(id));
            check(catalog.backing_file(path)==root/"vehicle.nac");
            bool rejected=false;
            try{(void)catalog.resolve(path);}catch(std::invalid_argument const&){rejected=true;}
            check(rejected);
        }
        auto const* broken=catalog.find("broken.nac");
        check(broken && broken->kind==AssetKind::audio && !broken->directory);
        catalog.scan(root);
        check(catalog.find("vehicle.nac/track-42")!=nullptr);
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
