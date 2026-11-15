// Stress/regression: indexed asset access must preserve exact record
// identity while avoiding repeated full-vector scans.
#include <northstarcpp/editor_asset_catalog.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using northstarcpp::editor::AssetCatalog;
using northstarcpp::editor::AssetRecord;
static void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        auto const dir=fs::path(argv[1]);
        fs::create_directories(dir);
        constexpr int items=2048;
        std::vector<std::string> names;
        names.reserve(items);
        for(int i=0;i<items;++i) {
            auto name=std::string("Asset_")+std::to_string(i)+".ysc";
            names.push_back(name);
            auto file=dir/fs::u8path(name);
            if(!fs::exists(file)) {
                std::ofstream output(file,std::ios::binary);
                output.put('A');
                require(bool(output),"Cannot create catalog fixture");
            }
        }
        AssetCatalog catalog;
        catalog.scan(dir,items+10);
        require(catalog.entries().size()==items,
                "Catalog omitted fixture files");
        auto reference_find=[&](std::string const& name)->AssetRecord const* {
            auto const& records=catalog.entries();
            auto const found=std::find_if(records.begin(),records.end(),
                [&](AssetRecord const& record){
                    return record.virtual_path==name;
                });
            return found==records.end()?nullptr:&*found;
        };
        for(auto const& name:names)
            require(catalog.find(name)==reference_find(name),
                    "Indexed lookup differs from previous linear search");
        require(!catalog.find("Asset_not_found.ysc"),
                "Indexed lookup invented nonexistent asset");
        constexpr int repetitions=20000;
        auto run=[&](bool indexed) {
            volatile std::size_t checksum=0;
            auto const started=std::chrono::steady_clock::now();
            for(int n=0;n<repetitions;++n) {
                auto const& name=names[(n*37)%items];
                auto const* record=indexed?catalog.find(name):reference_find(name);
                require(record!=nullptr,"Repeated lookup lost asset");
                checksum=checksum+record->virtual_path.size();
            }
            require(checksum>0,"Benchmark was eliminated");
            return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now()-started).count();
        };
        const auto linear_us=run(false);
        const auto indexed_us=run(true);
        std::cout<<"AssetCatalog lookup: "<<items<<" records, "
                 <<repetitions<<" lookups, linear "<<linear_us
                 <<" us, indexed "<<indexed_us<<" us\n";
        require(indexed_us*2<linear_us,
                "Indexed lookup did not reduce full scan cost");
        auto moved=std::move(catalog);
        for(auto const& name:names)
            require(moved.find(name)!=nullptr,"Move invalidated index");
        AssetCatalog copied=moved;
        for(auto const& name:names)
            require(copied.find(name)!=nullptr,"Copy invalidated index");
        copied.scan(dir,17);
        require(copied.truncated() && copied.entries().size()==17,
                "Truncated scan did not rebuild catalog");
        for(auto const& record:copied.entries())
            require(copied.find(record.virtual_path)==&record,
                    "Truncated catalog contains stale index");
        std::cout<<"Catalog index / move / copy / truncation: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
