#include "../engine/northstarcpp/modules/editor/asset_dependency_registry.hpp"
#include <northstarcpp/scene.hpp>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
using northstarcpp::editor::AssetDependencyRegistry;
using northstarcpp::editor::AssetCatalog;
void require(bool value,char const* message) {
    if(!value)throw std::runtime_error(message);
}
void fileWrite(QString const& name,QByteArray const& bytes) {
    QFile out(name);
    require(out.open(QIODevice::WriteOnly),"Fixture creation failed");
    require(out.write(bytes)==bytes.size(),"Fixture write failed");
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir temp;
        require(temp.isValid(),"Temporary project unavailable");
        QString const root=temp.path()+"/assets";
        require(QDir().mkpath(root+"/models"),"Models folder missing");
        require(QDir().mkpath(root+"/configs"),"Configs folder missing");
        fileWrite(root+"/models/original.ydr","example source mesh");
        fileWrite(root+"/configs/character.json",
            R"({"model":"models/original.ydr","description":"test"})");
        fileWrite(root+"/configs/player.js",
            R"(const characterModel = "models/original.ydr";)");
        auto scene=northstarcpp::make_startup_scene();
        scene.version=3;
        require(!scene.objects.empty(),"Startup fixture missing objects");
        scene.objects.front().node.asset="models/original.ydr";
        northstarcpp::save_scene(scene,
            fs::u8path((temp.path()+"/main.nscn").toUtf8().toStdString()));
        AssetCatalog catalog;
        catalog.scan(fs::u8path(root.toUtf8().toStdString()));
        AssetDependencyRegistry index;
        QString error;
        require(index.rebuild(catalog,error),"Registry build failed");
        auto inbound=index.referencersOf("models/original.ydr");
        require(inbound.contains("configs/character.json") &&
                inbound.contains("configs/player.js") &&
                inbound.contains("@project/main.nscn"),
                "Asset reverse dependency edges missing");
        require(index.dependenciesOf("configs/player.js").contains(
                    "models/original.ydr"),"Asset forward edge missing");
        require(index.edgeCount()>=3 && index.revision()==1,
                "Registry generation/edge metadata invalid");
        auto invalid=index.moveAssets(catalog,{
            {"models/original.ydr","../unsafe.ydr"}});
        require(!invalid.ok,"Traversal destination allowed");
        auto moved=index.moveAssets(catalog,{
            {"models/original.ydr","models/renamed.ydr"}});
        require(moved.ok && moved.assetsMoved==1 &&
                moved.referencesUpdated==3,
                ("Move failed: "+moved.message).toStdString().c_str());
        require(QFile::exists(root+"/models/renamed.ydr") &&
                !QFile::exists(root+"/models/original.ydr"),
                "Asset rename not committed");
        QFile json(root+"/configs/character.json");
        require(json.open(QIODevice::ReadOnly),"Updated JSON missing");
        require(json.readAll().contains("models/renamed.ydr"),
                "JSON relinking failed");
        QFile script(root+"/configs/player.js");
        require(script.open(QIODevice::ReadOnly),"Updated JS missing");
        require(script.readAll().contains("models/renamed.ydr"),
                "Script literal relinking failed");
        auto after=northstarcpp::load_scene(
            fs::u8path((temp.path()+"/main.nscn").toUtf8().toStdString()));
        require(after.objects.front().node.asset=="models/renamed.ydr",
                "Native binary scene relinking failed");
        auto stale=index.moveAssets(catalog,{
            {"models/original.ydr","models/third.ydr"}});
        require(!stale.ok,"Stale asset catalog move accepted");
        AssetCatalog refreshed;
        refreshed.scan(fs::u8path(root.toUtf8().toStdString()));
        QString status;
        require(index.rebuild(refreshed,status),"Rescan failed");
        require(index.referencersOf("models/renamed.ydr").size()==3,
                "Graph did not follow move");
        std::cout<<"PASS: dependency index, reverse graph, binary scene, atomic move\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"FAIL asset registry smoke: "<<e.what()<<'\n';
        return 1;
    }
}
