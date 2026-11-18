#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <algorithm>
#include <chrono>
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
        check(kind_from_extension(fs::path("MaterialFunctions/Color.material-function.json"))==
              AssetKind::material_function,
              "Material Function must be a first-class asset kind");
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
        fs::create_directories(root/"Assets"/"characters"/"abby");
        fs::copy_file(source/"characters/abby/abby.character.json",
            root/"Assets/characters/abby/abby.character.json");
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
        fs::create_directories(root/"Assets"/"NativeRig");
        fs::copy_file(source/"models/weapon/pistol/common-pistol.ymt",
            root/"Assets/NativeRig/common-pistol.ymt",
            fs::copy_options::overwrite_existing);
        fs::copy_file(source/"animations/weapon/pistol/military-pistol.ycd",
            root/"Assets/NativeRig/military-pistol.ycd",
            fs::copy_options::overwrite_existing);
        std::ofstream(root/"Assets/NativeRig/broken.ymt",
                      std::ios::binary).write("INVALID_NEf8",12);
        check(kind_from_extension(fs::path("common-pistol.YMT"))==
              AssetKind::skeleton,"YMT must classify as a skeleton");
        check(kind_from_extension(fs::path("military-pistol.YCD"))==
              AssetKind::animation,"YCD must classify as animation");
        check(kind_from_extension(fs::path("combat.nagraph.xml"))==
              AssetKind::animation,"XML Animation Graph must classify as animation");
        catalog.scan(root/"Assets");
        auto const* character=catalog.find(
            "characters/abby/abby.character.json");
        check(character && character->kind==AssetKind::character &&
              !character->directory &&
              catalog.asset_reference(character->virtual_path)==
                  "characters/abby/abby.character.json",
              "Namespaced Character manifest missing or misclassified");
        check(catalog.find("characters/abby")!=nullptr,
              "Character namespace folder not visible");
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
        check(ytd->virtual_child_count==ytd_children &&
              ydd->virtual_child_count==ydd_children &&
              ymat->virtual_child_count==ymat_children,
              "NEF8 child counts differ from actual dictionary entries");
        check(ytd_children==5,"All five skydome YTD entries must be visible");
        check(ydd_children==1,"Skydome YDD drawable must be visible");
        check(ymat_children==3,"All three skydome YMAT materials must be visible");
        auto const* rig=catalog.find("NativeRig/common-pistol.ymt");
        auto const* animation=catalog.find("NativeRig/military-pistol.ycd");
        check(rig && rig->directory && rig->is_list_file(),
              "Native YMT is not a browsable joint hierarchy");
        check(animation && animation->directory && animation->is_list_file(),
              "Native YCD is not a browsable clip dictionary");
        auto const* broken=catalog.find("NativeRig/broken.ymt");
        check(broken && !broken->directory && broken->kind==AssetKind::skeleton,
              "Damaged YMT should stay visible as an invalid file");
        std::size_t joints=0,clips=0;
        for(auto const& record:catalog.entries()) {
            if(record.is_virtual_entry() &&
               record.container_path=="NativeRig/common-pistol.ymt") {
                ++joints;
                check(record.kind==AssetKind::skeleton &&
                      catalog.asset_reference(record.virtual_path)==
                      record.container_path+"@"+record.entry_name,
                      "YMT joint reference identity was lost");
            }
            if(record.is_virtual_entry() &&
               record.container_path=="NativeRig/military-pistol.ycd") {
                ++clips;
                check(record.kind==AssetKind::animation &&
                      catalog.asset_reference(record.virtual_path)==
                      record.container_path+"@"+record.entry_name,
                      "YCD clip reference identity was lost");
            }
        }
        check(rig->virtual_child_count==joints &&
              animation->virtual_child_count==clips,
              "YMT/YCD child counts differ from decoded entries");
        check(joints>0 && clips>0,"Native YMT/YCD entries were not indexed");
        check(catalog.entries().size()==23+4+joints+clips,
              "Existing catalog entries or new rig entries were omitted");
        // A second scan must reuse decoded metadata for all valid NEF8
        // sources while preserving the exact virtual hierarchy.
        auto const cold_misses=catalog.dictionary_cache_misses();
        catalog.scan(root/"Assets");
        check(catalog.dictionary_cache_hits()>=5,
              "Warm refresh unnecessarily decoded NEF8 dictionaries");
        check(catalog.dictionary_cache_misses()==0,
              "Warm refresh unexpectedly missed a stable dictionary");
        check(catalog.entries().size()==23+4+joints+clips,
              "Warm catalog refresh changed the virtual hierarchy");
        std::cout<<"NEF8 warm scan: "<<catalog.dictionary_cache_hits()
                 <<" reused, cold decoded "<<cold_misses<<"\n";
        // Updating a compressed file invalidates its cached dictionary.
        auto const rig_path=root/"Assets/NativeRig/common-pistol.ymt";
        auto const previous_time=fs::last_write_time(rig_path);
        {
            std::fstream damaged(rig_path,std::ios::binary|
                                        std::ios::in|std::ios::out);
            damaged.write("FAIL",4);
        }
        fs::last_write_time(rig_path,previous_time+std::chrono::seconds(2));
        catalog.scan(root/"Assets");
        auto const* damaged=catalog.find("NativeRig/common-pistol.ymt");
        check(damaged && !damaged->directory,
              "Damaged YMT is still accepted from a stale cache entry");
        check(catalog.dictionary_cache_hits()>=4,
              "Unmodified dictionaries should remain reusable");
        // The base fixture is restored so any following runs are repeatable.
        fs::copy_file(source/"models/weapon/pistol/common-pistol.ymt",
            rig_path,fs::copy_options::overwrite_existing);
        fs::last_write_time(rig_path,previous_time+std::chrono::seconds(4));
        catalog.scan(root/"Assets");
        auto const* recovered=catalog.find("NativeRig/common-pistol.ymt");
        check(recovered && recovered->directory &&
              recovered->virtual_child_count==joints,
              "Repaired NEF8 dictionary failed to invalidate/reindex");

        std::cout<<"YMT joints="<<joints<<" YCD clips="<<clips<<"\n";
        // Game scripts are first-class physical assets inside assets/scripts.
        // A sibling project/scripts directory must not be indexed as an
        // implicit source or override the canonical VFS tree.
        fs::create_directories(root/"Assets"/"scripts"/"player");
        std::ofstream(root/"Assets"/"scripts"/"main.js")<<"// main";
        std::ofstream(root/"Assets"/"scripts"/"player"/"controller.ts")<<"export {};";
        fs::create_directories(root/"scripts");
        std::ofstream(root/"scripts"/"legacy.js")<<"// legacy";
        catalog.scan(root/"Assets");
        auto const* script_dir=catalog.find("scripts");
        auto const* entry=catalog.find("scripts/main.js");
        auto const* nested=catalog.find("scripts/player/controller.ts");
        check(script_dir && script_dir->directory,
              "Project script assets root missing");
        check(entry && entry->kind==AssetKind::script,
              "Project main.js missing from asset catalog");
        check(nested && nested->kind==AssetKind::script,
              "Nested TypeScript asset missing");
        check(catalog.find("scripts/legacy.js")==nullptr,
              "Legacy sibling scripts shadowed canonical assets");
        check(catalog.backing_file("scripts/main.js")==
              (root/"Assets"/"scripts"/"main.js").lexically_normal(),
              "Script backing file must resolve under assets");
        check(catalog.resolve("scripts/player")==
              (root/"Assets"/"scripts"/"player").lexically_normal(),
              "Nested script asset folder cannot be resolved");
        check(catalog.asset_reference("scripts/main.js")=="scripts/main.js",
              "Project VFS script reference unexpectedly changed");
        std::cout<<"Cross-platform asset catalog: PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
