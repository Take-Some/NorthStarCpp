// P0 regression: overlapping catalog refreshes must not parse optional
// NEF8 JSON metadata in parallel and never poison the shared decode cache.
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
namespace fs=std::filesystem;
using northstarcpp::editor::AssetCatalog;
static void check(bool condition,char const* message) {
    if(!condition)throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    try {
        auto engine=fs::path(argv[1]);
        auto project=fs::path(argv[2]);
        auto fixture=fs::path(argv[3]);
        check(fs::is_directory(engine),"Engine Shared Assets root missing");
        check(fs::is_directory(project),"Project Asset root missing");
        fs::create_directories(fixture);
        auto const source=engine/"models/skydome.ydd";
        auto const dest=fixture/"test_dictionary.ydd";
        fs::copy_file(source,dest,fs::copy_options::overwrite_existing);
        // NEF8 v1 JSON metadata is not required for children enumeration.
        // Break only that optional field; leave authenticated body unchanged.
        {
            std::fstream stream(dest,std::ios::in|std::ios::out|std::ios::binary);
            check(bool(stream),"Cannot mutate isolated fixture");
            stream.seekg(48);
            char offsetBytes[8]{};
            stream.read(offsetBytes,8);
            std::uint64_t offset=0;
            for(int i=0;i<8;++i)
                offset|=std::uint64_t(static_cast<unsigned char>(offsetBytes[i]))<<(8*i);
            check(offset>=128 && offset<1024*1024,
                "Unexpected test dictionary metadata offset");
            stream.seekp(static_cast<std::streamoff>(offset));
            stream.put('!'); // deliberately invalid JSON object
        }
        std::vector<std::uint8_t> bytes;
        {
            std::ifstream f(dest,std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(f),
                         std::istreambuf_iterator<char>());
        }
        bool rejects=false;
        try {
            (void)northstarcpp::shared_assets::list_dictionary_entries(bytes,"test_dictionary.ydd");
        }catch(std::exception const&) { rejects=true; }
        // Validated content still yields its child dictionary even though
        // the optional metadata JSON is malformed. This is intentional;
        // the full material/model importer must still validate its metadata.
        check(!rejects,"Catalog dictionary should ignore optional JSON metadata");
        std::atomic<int> successes{0};
        std::vector<std::future<void>> jobs;
        const int workers=std::getenv("NORTHSTAR_CATALOG_STRESS_WORKERS") ?
            std::max(1,std::atoi(std::getenv("NORTHSTAR_CATALOG_STRESS_WORKERS"))):4;
        constexpr int rounds=6;
        for(int i=0;i<workers;++i) {
            jobs.emplace_back(std::async(std::launch::async,[&,i] {
                for(int n=0;n<rounds;++n) {
                    AssetCatalog engineCatalog;
                    engineCatalog.scan(engine,100000,true);
                    check(!engineCatalog.entries().empty(),
                          "Engine catalog lost resources");
                    AssetCatalog projectCatalog;
                    projectCatalog.scan(project,100000);
                    check(!projectCatalog.entries().empty(),
                          "Project catalog lost resources");
                    AssetCatalog isolated;
                    isolated.scan(fixture,1000);
                    auto* ydd=isolated.find("test_dictionary.ydd");
                    check(ydd && ydd->directory && ydd->virtual_child_count>0,
                          "Malformed optional metadata dropped valid dictionary");
                    ++successes;
                }
            }));
        }
        for(auto& job:jobs)job.get();
        check(successes==workers*rounds,"Catalog refresh tasks lost");
        std::cout<<"NEF8 metadata-free concurrent dictionary indexing: PASS ("
                 <<successes<<" engine/project/fixture scan rounds)\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"NEF8 concurrent index test: "<<e.what()<<"\n";
        return 1;
    }
}
