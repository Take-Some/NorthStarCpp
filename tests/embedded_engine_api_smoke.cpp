// The installed Engine API is a compiled resource, never a VFS override.
#include <northstarcpp/embedded_script_api.hpp>
#include <northstarcpp/editor_asset_catalog.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) {
    if(argc!=3)return 2;
    namespace fs=std::filesystem;
    try {
        auto const shared=fs::u8path(argv[1]);
        auto const source=fs::u8path(argv[2]);
        using namespace northstarcpp;
        if(embedded_api::count()==0 ||
           !embedded_api::find("scripts/northstar/bootstrap.js") ||
           !embedded_api::find("scripts/ui/UI.js") ||
           embedded_api::find("../scripts/northstar/bootstrap.js") ||
           embedded_api::find("scripts/northstar/bootstrap.js.bak"))return 3;
        editor::AssetCatalog catalog;
        catalog.scan(shared,100000,true);
        auto const* root=catalog.find("scripts");
        if(!root || !root->directory || !root->embedded_engine_api)return 4;
        for(std::size_t i=0;i<embedded_api::count();++i) {
            auto const& embedded=embedded_api::entries()[i];
            if(embedded.source.empty() || !embedded_api::find(embedded.virtual_path))
                return 5;
            std::string const key(embedded.virtual_path);
            auto const* record=catalog.find(key);
            if(!record || record->directory || !record->embedded_engine_api ||
               record->size!=embedded.source.size() ||
               record->asset_reference()!="engine-api://"+key)return 6;
            bool rejected=false;
            try { (void)catalog.backing_file(key); }
            catch(std::invalid_argument const&) {rejected=true;}
            if(!rejected)return 7;
            auto const physical=source/fs::u8path(key.substr(8));
            std::ifstream file(physical,std::ios::binary);
            if(!file)return 8;
            std::string const bytes((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
            if(bytes!=embedded.source)return 9;
        }
        if(catalog.find("scripts/northstar/bootstrap.js.bak-20261116-041221824"))
            return 10; // no on-disk backups should be shown in the engine UI
        std::cout<<"Embedded Engine API: PASS ("<<embedded_api::count()
                 <<" read-only resources)\n";
        return 0;
    }catch(std::exception const& error) {
        std::cerr<<error.what()<<'\n';
        return 11;
    }
}
