#include <northstarcpp/editor_asset_catalog.hpp>
#include "../engine/northstarcpp/modules/editor/asset_browser_query.hpp"
#include "../engine/northstarcpp/modules/editor/asset_browser_collections.hpp"
#include "../engine/northstarcpp/modules/editor/asset_team_collections.hpp"
#include <QDir>
#include "../engine/northstarcpp/modules/editor/asset_browser_git_ops.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QSettings>
#include <iostream>
#include <stdexcept>

using namespace northstarcpp::editor;
void require(bool okay,char const* message) {
    if(!okay)throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir temp;
        require(temp.isValid(),"Could not create temporary asset root");
        QFile wood(temp.path()+"/wood_001.ybn");
        QFile metal(temp.path()+"/steel_002.ydr");
        require(wood.open(QIODevice::WriteOnly),"Missing YBN fixture");
        require(wood.write(QByteArray(1024,'w'))==1024,"YBN fixture write");
        wood.close();
        require(metal.open(QIODevice::WriteOnly),"Missing YDR fixture");
        require(metal.write(QByteArray(16,'s'))==16,"YDR fixture write");
        metal.close();
        AssetCatalog catalog;
        catalog.scan(temp.path().toStdString());
        auto const* ybn=catalog.find("wood_001.ybn");
        auto const* ydr=catalog.find("steel_002.ydr");
        require(ybn && ydr,"Registry did not index physical assets");
        require(ybn->modified_epoch_ms>0 && ydr->modified_epoch_ms>0,
                "Indexed modification metadata missing");
        require(ybn->size==1024 && ydr->size==16,"Metadata size mismatch");
        auto test=[&](QString const& expression,bool a,bool b) {
            AssetQuery query(expression);
            if(!query.valid())
                throw std::runtime_error(
                    ("Query refused: "+query.error()).toStdString());
            require(query.matches(*ybn)==a &&
                    query.matches(*ydr)==b,
                    "Expression evaluated incorrectly");
        };
        test("type:Collision",true,false);
        test("name:steel OR ext:ybn",true,true);
        test("is:asset AND (ext:ybn OR size<20)",true,true);
        test("size>100 AND !name:steel",true,false);
        test("name:\"wood_001\" AND ext:ybn",true,false);
        test("NOT (type:mesh OR size<20)",true,false);
        test("is:folder",false,false);
        test("is:virtual",false,false);
        test("is:asset AND size:>=1KB",true,false);
        require(!AssetQuery("name:\"unfinished").valid(),"Invalid quote accepted");
        require(!AssetQuery("(kind:Mesh").valid(),"Missing bracket accepted");
        require(!AssetQuery("invalid-field:value").valid(),"Unknown field accepted");
        auto named=asset_collections::load(temp.path());
        require(named.isEmpty(),"Unexpected existing fixture collections");
        named.insert("Level Art",{});
        require(asset_collections::add(named,"Level Art","wood_001.ybn"),
                "Could not add collection asset");
        require(!asset_collections::add(named,"Level Art","../escape"),
                "Unsafe collection reference accepted");
        asset_collections::save(temp.path(),named);
        auto loaded=asset_collections::load(temp.path());
        require(loaded.contains("Level Art") &&
                loaded["Level Art"]==QStringList{"wood_001.ybn"},
                "Collection persistence failed");
        require(!asset_collections::validName("../outside"),
                "Unsafe collection name accepted");
        // Team collections are project files, not private machine settings.
        QString teamAssets=temp.path()+"/assets";
        require(QDir().mkpath(teamAssets),"Cannot make team project assets root");
        asset_collections::Items team;
        team.insert("Shared Level Art",QStringList{"models/pillar.ydr"});
        QString teamError;
        require(asset_team_collections::save(teamAssets,team,teamError),
                "Team collection atomic save failed");
        asset_collections::Items shared;
        require(asset_team_collections::load(teamAssets,shared,teamError) &&
                shared.value("Shared Level Art").contains("models/pillar.ydr"),
                "Team collection reload failed");
        require(QFile::exists(asset_team_collections::filename(teamAssets)),
                "Team collections JSON missing from project");
        // Git operates only on a disposable repository, with bounded
        // arguments and no shell interpolation or real project changes.
        QTemporaryDir repo;
        require(repo.isValid(),"Git fixture directory missing");
        auto initial=git_ops::run(repo.path(),{"init","-q"});
        require(initial.success,"Git initialization failed");
        auto baseline=git_ops::run(repo.path(),{
            "-c","user.name=NorthStar QA","-c","user.email=qa@northstar.invalid",
            "commit","-q","--allow-empty","-m","baseline"});
        require(baseline.success,"Git baseline creation failed");
        QString const unusual="asset with spaces;literal.ydr";
        QFile tracked(repo.path()+"/"+unusual);
        require(tracked.open(QIODevice::WriteOnly),"Git fixture asset not writable");
        tracked.write("content");tracked.close();
        auto pending=git_ops::run(repo.path(),{
            "status","--porcelain=v1","--",unusual});
        require(pending.success && pending.output.contains("??"),
                "Git did not detect untracked asset");
        auto staged=git_ops::run(repo.path(),{"add","--",unusual});
        require(staged.success,"Git stage failed");
        auto diff=git_ops::run(repo.path(),{"diff","--cached","--",unusual});
        require(diff.success && diff.output.contains("content"),
                "Git staged diff unavailable");
        auto reset=git_ops::run(repo.path(),{"restore","--staged","--",unusual});
        require(reset.success,"Git unstage failed");
        auto after=git_ops::run(repo.path(),{
            "status","--porcelain=v1","--",unusual});
        require(after.success && after.output.contains("??"),
                "Git unstage did not restore untracked status");
        require(!git_ops::run(repo.path()+"/absent",{"status"}).success,
                "Git accepted nonexistent root");
        // Test cleanup: no persistent QA settings left in user profile.
        QSettings settings("Kayla's Systems","NorthStarCpp");
        settings.remove(asset_collections::storageKey(temp.path()));
        std::cout<<"PASS: index metadata, bounded advanced queries, collections\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Advanced Asset Browser QA failed: "<<e.what()<<'\n';
        return 1;
    }
}
