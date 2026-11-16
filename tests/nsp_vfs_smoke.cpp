// NorthStar Pack v1 roundtrip through the real replaceable engine.assets ABI.
#include <northstarcpp/assets_plugin.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=std::filesystem;
static void check(bool value,char const* message) {
    if(!value)throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    try {
        auto const library=fs::absolute(fs::u8path(argv[1]));
        auto const pack=fs::absolute(fs::u8path(argv[2])).u8string();
        auto const source=fs::absolute(fs::u8path(argv[3]));
        HMODULE dll=LoadLibraryW(library.c_str());
        check(dll!=nullptr,"Cannot load assets provider");
        auto query=reinterpret_cast<NorthStarModuleQuery>(
            GetProcAddress(dll,"northstar_module_query"));
        check(query!=nullptr,"Missing module query");
        auto* main=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_ASSETS,NORTHSTARCPP_ASSETS_ABI);
        auto* archive=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_ASSETS,NORTHSTARCPP_ASSETS_ARCHIVE_ABI);
        auto* range=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_ASSETS,NORTHSTARCPP_ASSETS_RANGE_ABI);
        check(main && archive && range,"Missing compatible assets ABI");
        auto* api=static_cast<NorthStarAssetsApi const*>(main->api);
        auto* archives=static_cast<NorthStarAssetsArchiveApi const*>(archive->api);
        auto* ranges=static_cast<NorthStarAssetsRangeApi const*>(range->api);
        void* context=api->create();
        check(context!=nullptr,"Cannot create VFS context");
        check(archives->mount_archive(context,"",pack.c_str(),pack.c_str(),-100),
              "Cannot mount NSP");
        const char* name="prefabs/rifle_mini14.nprefab";
        fs::path expected_file=source/"prefabs"/"rifle_mini14.nprefab";
        std::ifstream input(expected_file,std::ios::binary);
        check(static_cast<bool>(input),"Missing smoke source asset");
        std::vector<std::uint8_t> expected{
            std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        std::uint64_t length=0,received=0;
        check(api->file_size(context,name,&length),"VFS cannot resolve NSP entry");
        check(length==expected.size(),"NSP size mismatch");
        std::vector<std::uint8_t> bytes(length);
        check(api->read(context,name,bytes.data(),bytes.size(),&received),
              "NSP read failed");
        check(received==length && bytes==expected,"NSP roundtrip mismatch");
        std::vector<std::uint8_t> range_data(std::min<std::size_t>(11,bytes.size()));
        check(ranges->read_range(context,name,2,range_data.data(),range_data.size(),&received),
              "NSP range read failed");
        check(received==range_data.size() &&
              std::equal(range_data.begin(),range_data.end(),bytes.begin()+2),
              "NSP range bytes mismatch");
        // Mount a project-like override and check the original low-priority
        // package remains visible for assets not provided by the project.
        auto override_root=fs::temp_directory_path()/
            ("northstar_nsp_overlay_smoke_"+std::to_string(GetCurrentProcessId()));
        fs::create_directories(override_root/"prefabs");
        {std::ofstream out(override_root/"prefabs"/"rifle_mini14.nprefab",std::ios::binary);
         out<<"project-override";}
        auto override_u8=override_root.u8string();
        check(api->mount_directory(context,"",override_u8.c_str(),100),
              "Cannot mount override directory");
        check(api->file_size(context,name,&length) && length==16,
              "Project priority did not override NSP");
        check(api->file_size(context,"models/skydome.ydd",&length) && length>0,
              "NSP fallback is unavailable");
        check(api->unmount_directory(context,"",override_u8.c_str()),
              "Cannot unmount override");
        std::error_code error;
        fs::remove_all(override_root,error);
        api->destroy(context);
        FreeLibrary(dll);
        std::cout<<"NSP ABI mount, read, range, overlay: PASS\n";
        return 0;
    }catch(std::exception const& e){
        std::cerr<<e.what()<<"\n";
        return 1;
    }
}
