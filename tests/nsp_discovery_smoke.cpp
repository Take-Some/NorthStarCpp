// End-to-end multiple NSP mount discovery via the real AssetStore.
#include <northstarcpp/assets.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
static void verify(bool yes,char const* msg) {
    if(!yes)throw std::runtime_error(msg);
}
static std::string text_asset(northstarcpp::AssetStore const& assets,std::string const& key) {
    auto bytes=assets.read(key);
    return {bytes.begin(),bytes.end()};
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    try {
        northstarcpp::AssetStore assets;
        assets.mount_runtime_archives(fs::u8path(argv[1]));
        verify(text_asset(assets,"data/pack_order.txt")=="last",
               "Additional NSP pack precedence is incorrect");
        verify(assets.size("models/skydome.ydd")>0,
               "Engine baseline NSP is missing");
        assets.mount_directory("",fs::u8path(argv[2]),100);
        verify(text_asset(assets,"data/pack_order.txt")=="project",
               "Project override did not win");
        std::cout<<"multi-NSP discovery and project override: PASS\n";
        return 0;
    }catch(std::exception const& error) {
        std::cerr<<error.what()<<"\n";
        return 1;
    }
}
