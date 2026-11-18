// Native Qt Asset Browser prefab QA: discovery, Scene V3 parsing,
// schematic preview and transactional placement using the original sample.
#include "qt_prefab_preview.hpp"
#include "editor_prefab.hpp"
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/scene.hpp>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <iostream>

static void require(bool condition,char const* reason) {
    if(!condition)throw std::runtime_error(reason);
}
int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);
    if(argc!=2)return 2;
    using namespace northstarcpp;
    using namespace northstarcpp::editor;
    namespace fs=std::filesystem;
    try {
        auto project=fs::u8path(argv[1]);
        auto shared=project.parent_path().parent_path()/"engine/northstarcpp/resources/sharedAssets";
        AssetCatalog catalog;
        catalog.scan(shared);
        auto const* record=catalog.find("prefabs/weapon/pistol.nprefab");
        require(record && record->kind==AssetKind::prefab &&
                !record->directory,"Engine weapon prefab missing or misclassified");
        require(record->prefab_valid && record->prefab_actor_count==1,
                "Prefab card did not decode original Scene V3 object count");
        require(catalog.find("prefabs")!=nullptr,
                "Project prefab virtual folder not indexed");
        require(catalog.resolve(record->virtual_path)==
                fs::weakly_canonical(shared/"prefabs/weapon/pistol.nprefab"),
                "Native prefab resolved outside sharedAssets/prefabs");
        auto prefab=prefab_preview::load(shared,
                                        record->asset_reference());
        require(prefab.version==3 && prefab.objects.size()==1 &&
                prefab.objects[0].name=="Pistol" &&
                prefab.objects[0].node.asset==
                    "models/weapon/pistol/pistol.ydd",
                "Original native pistol prefab geometry was not decoded");
        require(prefab.objects[0].mass>0.f &&
                prefab.objects[0].node.collision,
                "Original prefab physics contract was lost");
        auto preview=prefab_preview::thumbnail(prefab,320);
        require(!preview.isNull() &&
                preview.width()==320 && preview.height()==320 &&
                preview.pixel(160,160)!=preview.pixel(0,0),
                "Prefab composition thumbnail was not generated");
        auto const summary=prefab_preview::details(prefab);
        require(summary.contains("Actors: 1") &&
                summary.contains("Colliders: 1"),
                "Prefab details omit parsed graph counts");
        Scene target;
        target.version=3;
        target.name="Prefab Placement";
        target.layers.push_back({"Default",true});
        auto const root=editor_tools::instantiate_prefab(
            target,prefab,{20.f,3.f,40.f});
        require(target.objects.size()==1 && root==0 &&
                target.objects[0].position.x==20.f &&
                target.objects[0].position.y==3.f &&
                target.objects[0].position.z==40.f &&
                target.objects[0].node.asset==prefab.objects[0].node.asset &&
                target.objects[0].mass==prefab.objects[0].mass,
                "Prefab instantiation lost world anchor or original asset contract");
        // Also support ordinary Content subfolders, not only the sibling
        // project/prefabs convention. A corrupt prefab must stay indexed.
        QTemporaryDir temporary;
        require(temporary.isValid(),"Cannot create isolated prefab test Content");
        auto sandbox=fs::u8path(temporary.path().toUtf8().toStdString());
        fs::create_directories(sandbox/"assets"/"actors");
        fs::copy_file(shared/"prefabs/weapon/pistol.nprefab",
            sandbox/"assets"/"actors"/"pistol.nprefab");
        std::ofstream(sandbox/"assets"/"actors"/"broken.nprefab")<<"invalid";
        AssetCatalog nested;
        nested.scan(sandbox/"assets");
        auto const* embedded=nested.find("actors/pistol.nprefab");
        auto const* invalid=nested.find("actors/broken.nprefab");
        require(embedded && embedded->kind==AssetKind::prefab &&
                !embedded->external_prefab && embedded->prefab_valid &&
                embedded->prefab_actor_count==1,
                "Content-folder prefab card is incorrectly classified");
        require(invalid && invalid->kind==AssetKind::prefab &&
                !invalid->prefab_valid,
                "Invalid Content prefab should be quarantined as metadata");
        require(nested.resolve("actors/pistol.nprefab")==
                fs::weakly_canonical(sandbox/"assets"/"actors"/"pistol.nprefab"),
                "Content prefab path resolved into sibling project folder");
        require(prefab_preview::load(sandbox/"assets",
                 "actors/pistol.nprefab").objects.size()==1,
                "Content subfolder prefab cannot be decoded");
        bool rejected=false;
        try {(void)prefab_preview::load(shared,
                  "prefabs/../outside.nprefab");}
        catch(std::exception const&) {rejected=true;}
        require(rejected,"Prefab directory traversal accepted");
        std::cout<<"PASS: original pistol.nprefab catalog, image, "
                    "metadata, placement and traversal defense\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Prefab Browser QA failed: "<<e.what()<<"\n";
        return 1;
    }
}
