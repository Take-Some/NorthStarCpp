#include <northstarcpp/editor_asset_catalog.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace northstarcpp::editor;
namespace fs=std::filesystem;
void check(bool okay,char const* message) {
    if(!okay)throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    auto root=fs::path(argv[1]);
    try {
        if(fs::exists(root))fs::remove_all(root); // isolated CTest fixture only
        fs::create_directories(root/"Assets"/"Meshes");
        fs::create_directories(root/"Assets"/"Textures");
        std::ofstream(root/"Assets"/"Meshes"/"npc.ydd").put('M');
        std::ofstream(root/"Assets"/"Textures"/"albedo.ytd").put('T');
        std::ofstream(root/"Assets"/"logic.ysc").put('S');
        AssetCatalog catalog;
        catalog.scan(root/"Assets");
        check(catalog.entries().size()==5,"Missing catalog assets");
        auto const& records=catalog.entries();
        check(std::count_if(records.begin(),records.end(),
            [](auto const& r){return r.kind==AssetKind::mesh;})==1,
            "Mesh classification failed");
        check(std::count_if(records.begin(),records.end(),
            [](auto const& r){return r.kind==AssetKind::texture;})==1,
            "Texture classification failed");
        check(catalog.resolve("Meshes/npc.ydd")==
              (root/"Assets"/"Meshes"/"npc.ydd").lexically_normal(),
              "Virtual path resolution failed");
        for(auto value:{"../outside","C:/evil","/absolute",
                        "Textures/../../secret","Textures\\evil"}) {
            check(!safe_virtual_path(value),"Unsafe relative path accepted");
            bool rejected=false;
            try{(void)catalog.resolve(value);}
            catch(std::invalid_argument const&){rejected=true;}
            check(rejected,"Unsafe catalog resolution accepted");
        }
        catalog.scan(root/"Assets",2);
        check(catalog.truncated() && catalog.entries().size()==2,
              "Bounded traversal failed");
        auto source=fs::path(argv[2]);
        fs::create_directories(root/"Assets"/"RealTextures");
        fs::create_directories(root/"Assets"/"RealModels");
        fs::create_directories(root/"Assets"/"RealMaterials");
        fs::copy_file(source/"materials/skydome.ymat",
            root/"Assets/RealMaterials/skydome.ymat",
            fs::copy_options::overwrite_existing);
        fs::copy_file(source/"textures/skydome.ytd",
            root/"Assets/RealTextures/skydome.ytd",
            fs::copy_options::overwrite_existing);
        fs::copy_file(source/"models/skydome.ydd",
            root/"Assets/RealModels/skydome.ydd",
            fs::copy_options::overwrite_existing);
        catalog.scan(root/"Assets");
        auto const* ytd=catalog.find("RealTextures/skydome.ytd");
        auto const* ydd=catalog.find("RealModels/skydome.ydd");
        auto const* ymat=catalog.find("RealMaterials/skydome.ymat");
        check(ytd && ytd->directory && ytd->is_list_file(),
              "Valid YTD must be a virtual directory");
        check(ydd && ydd->directory && ydd->is_list_file(),
              "Valid YDD must be a virtual directory");
        check(ymat && ymat->directory && ymat->is_list_file(),
              "Valid YMAT must be a virtual directory");
        std::size_t ytd_children=0,ydd_children=0,ymat_children=0;
        for(auto const& record:catalog.entries()) {
            if(!record.is_virtual_entry())continue;
            if(record.container_path=="RealTextures/skydome.ytd") {
                ++ytd_children;
                check(record.kind==AssetKind::texture,
                      "YTD virtual child kind should be texture");
                check(catalog.asset_reference(record.virtual_path)==
                      record.container_path+"@"+record.entry_name,
                      "YTD child reference must use dictionary@entry");
            }
            if(record.container_path=="RealModels/skydome.ydd") {
                ++ydd_children;
                check(record.kind==AssetKind::mesh,
                      "YDD virtual child kind should be mesh");
            }
            if(record.container_path=="RealMaterials/skydome.ymat") {
                ++ymat_children;
                check(record.kind==AssetKind::material,
                      "YMAT virtual child must be material");
                check(catalog.asset_reference(record.virtual_path)==
                      record.container_path+"@"+record.entry_name,
                      "YMAT material identity must be preserved");
            }
            bool rejected=false;
            try {(void)catalog.resolve(record.virtual_path);}
            catch(std::invalid_argument const&){rejected=true;}
            check(rejected,"Virtual list child resolved to filesystem path");
            check(fs::is_regular_file(catalog.backing_file(record.virtual_path)),
                  "Virtual child backing file not found");
        }
        check(ytd_children==5,"All five skydome YTD entries must be visible");
        check(ydd_children==1,"Skydome YDD drawable must be visible");
        check(ymat_children==3,"All three skydome YMAT materials must be visible");
        check(catalog.entries().size()==20,
              "Catalog must include all YDD, YTD and YMAT virtual folders and children");
        std::cout<<"Cross-platform asset catalog: PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
