// Regress all NEF8 YMAT materials as independently editable named entries.
// Two native formats: kind 4 XML and kind 30 JSON. A save must preserve
// sibling materials, metadata, record order, custom params, and bindings.
#include "qt_material_authoring.hpp"
#include <northstarcpp/material_ymat_authoring.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/material.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
namespace ma=northstarcpp::editor::material_authoring;
namespace native=northstarcpp::shared_assets;
static void check(bool pass,char const* error) {
    if(!pass)throw std::runtime_error(error);
}
static std::vector<std::uint8_t> bytes(fs::path const& path) {
    std::ifstream source(path,std::ios::binary);
    if(!source)throw std::runtime_error("Missing YMAT test source");
    return {std::istreambuf_iterator<char>(source),{}};
}
static void copyFixture(fs::path const& from,fs::path const& to) {
    fs::create_directories(to.parent_path());
    fs::copy_file(from,to,fs::copy_options::overwrite_existing);
}
static bool equivalent(native::Material const& a,native::Material const& b) {
    if(a.name!=b.name||a.shader!=b.shader||a.blend!=b.blend||
       a.two_sided!=b.two_sided||a.alpha_cutoff!=b.alpha_cutoff||
       a.graph_source!=b.graph_source||
       a.textures.size()!=b.textures.size()||
       a.parameters.size()!=b.parameters.size())return false;
    for(std::size_t i=0;i<a.textures.size();++i)
        if(a.textures[i].slot!=b.textures[i].slot ||
           a.textures[i].reference!=b.textures[i].reference)return false;
    for(std::size_t i=0;i<a.parameters.size();++i)
        if(a.parameters[i].name!=b.parameters[i].name ||
           a.parameters[i].type!=b.parameters[i].type ||
           a.parameters[i].values!=b.parameters[i].values)return false;
    return true;
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2)return 2;
    try {
        fs::path repo=fs::u8path(argv[1]);
        QTemporaryDir temporary("northstar-ymat-edit-XXXXXX");
        check(temporary.isValid(),"Cannot create isolated YMAT test root");
        fs::path root=fs::u8path(temporary.path().toUtf8().toStdString());
        fs::path jsonSource=repo/"tests/fixtures/materials/MultiMaterialDemo.ymat";
        fs::path xmlSource=repo/"Projects/NorthStarFirstPersonPlatform/assets/materials/skydome.ymat";
        fs::path jsonTarget=root/"materials/TwoMaterials.ymat";
        fs::path xmlTarget=root/"materials/Skydome.ymat";
        copyFixture(jsonSource,jsonTarget);
        copyFixture(xmlSource,xmlTarget);

        auto const originalJson=bytes(jsonTarget);
        auto const jsonMeta=native::read_ymat_library(originalJson);
        check(jsonMeta.kind==30 && jsonMeta.count==2,
              "Expected two materials in kind-30 YMAT");
        auto beforeJson=native::load_materials_from_bytes(
            originalJson,"materials/TwoMaterials.ymat");
        check(beforeJson[0].name=="Copper Panel" && beforeJson[1].name=="Blue Ceramic",
              "Fixture order unexpectedly changed");
        auto blue=ma::load_descriptor(root,"materials/TwoMaterials.ymat@Blue Ceramic");
        check(blue.value("name").toString()=="Blue Ceramic",
              "Explicit YMAT material selection returned wrong entry");
        blue["base_color"]="#ff3355";
        blue["roughness"]=0.37;
        QJsonArray params=blue.value("parameters").toArray();
        params.append(QJsonObject{{"name","edge_intensity"},
                {"type","float"},{"value",QJsonArray{0.63}}});
        blue["parameters"]=params;
        QJsonArray textures=blue.value("textures").toArray();
        textures.append(QJsonObject{{"slot","custom_mask"},
                {"ref","textures/masks.ytd@perlin"}});
        blue["textures"]=textures;
        ma::save_descriptor(root,"materials/TwoMaterials.ymat@Blue Ceramic",blue);
        auto updatedJson=bytes(jsonTarget);
        auto afterJson=native::load_materials_from_bytes(
            updatedJson,"materials/TwoMaterials.ymat");
        check(afterJson.size()==2 && equivalent(beforeJson[0],afterJson[0]),
              "Editing second material damaged first material");
        check(!equivalent(beforeJson[1],afterJson[1]),
              "Second material did not change");
        check(afterJson[1].name=="Blue Ceramic" &&
              afterJson[1].parameters.size()>beforeJson[1].parameters.size(),
              "Custom YMAT parameters did not persist");
        check(afterJson[1].textures.size()==1 &&
              afterJson[1].textures[0].slot=="custom_mask",
              "Custom YMAT texture slot was not saved");
        auto reloadedBlue=ma::load_descriptor(root,"materials/TwoMaterials.ymat@Blue Ceramic");
        check(std::abs(reloadedBlue.value("roughness").toDouble()-.37)<.00001,
              "Edited YMAT roughness did not survive reload");
        check(native::read_ymat_library(updatedJson).kind==jsonMeta.kind &&
              native::read_ymat_library(updatedJson).count==jsonMeta.count,
              "Kind-30 NEF8 header count/kind changed");
        // Delete only custom fields from the selected material; every other
        // record and the mandatory PBR parameters must remain available.
        QJsonArray onlyStandard;
        for(auto const& raw:reloadedBlue.value("parameters").toArray())
            if(raw.toObject().value("name").toString()!="edge_intensity")
                onlyStandard.append(raw);
        reloadedBlue["parameters"]=onlyStandard;
        reloadedBlue["textures"]=QJsonArray{};
        ma::save_descriptor(root,"materials/TwoMaterials.ymat@Blue Ceramic",
                            reloadedBlue);
        auto trimmed=native::load_materials_from_bytes(
            bytes(jsonTarget),"materials/TwoMaterials.ymat");
        check(trimmed[1].textures.empty() &&
              trimmed[1].parameters.size()==beforeJson[1].parameters.size() &&
              equivalent(beforeJson[0],trimmed[0]),
              "Removing custom fields modified unrelated YMAT entries");

        auto originalXml=bytes(xmlTarget);
        auto xmlMeta=native::read_ymat_library(originalXml);
        check(xmlMeta.kind==4 && xmlMeta.count==3,
              "Expected three world materials in kind-4 YMAT");
        auto beforeXml=native::load_materials_from_bytes(
            originalXml,"materials/Skydome.ymat");
        auto first=QString::fromUtf8(beforeXml[0].name.c_str());
        auto selected=QStringLiteral("materials/Skydome.ymat@")+first;
        auto sky=ma::load_descriptor(root,selected);
        check(sky.value("shader").toString()=="pbr.unlit.sky",
              "Custom imported YMAT shader was not preserved");
        sky["base_color"]="#aabbcc";
        sky["roughness"]=0.42;
        sky["graph"]=QJsonObject{
            {"nodes",QJsonArray{
                QJsonObject{{"id","tint"},{"op","color"},
                            {"value",QJsonArray{0.2,0.4,0.6,1.0}}}
            }},
            {"outputs",QJsonObject{{"base_color","tint"}}}
        };
        auto custom=sky.value("parameters").toArray();
        for(int i=0;i<custom.size();++i) {
            auto value=custom[i].toObject();
            if(value.value("name").toString()=="emissive") {
                value["value"]=QJsonArray{0.4,0.7,0.2};
                custom[i]=value;
            }
        }
        sky["parameters"]=custom;
        ma::save_descriptor(root,selected,sky);
        auto updatedXml=bytes(xmlTarget);
        auto afterXml=native::load_materials_from_bytes(
            updatedXml,"materials/Skydome.ymat");
        check(afterXml.size()==beforeXml.size(),
              "Edited XML YMAT lost material records");
        for(std::size_t i=1;i<beforeXml.size();++i)
            check(equivalent(beforeXml[i],afterXml[i]),
                  "Nonselected world material changed after editing");
        check(afterXml[0].shader==beforeXml[0].shader &&
              afterXml[0].textures.size()==beforeXml[0].textures.size(),
              "Selected world material lost custom shader or textures");
        check(!afterXml[0].graph_source.empty() &&
              northstarcpp::compile_material_graph(afterXml[0].graph_source)
                .find("NS_GRAPH_BASE")!=std::string::npos,
              "Native kind-4 YMAT Material Graph was not persisted for GPU runtime");
        auto loadedGraph=ma::load_descriptor(root,selected);
        check(loadedGraph.value("graph").isObject(),
              "XML YMAT graph was not restored in Material Editor");
        auto post=native::read_ymat_library(updatedXml);
        check(post.kind==xmlMeta.kind && post.count==xmlMeta.count,
              "Kind-4 NEF8 header or record count changed");
        // NEF8 v1 header and non-material metadata bytes remain byte-identical
        // except for checksum and compressed-body size fields.
        auto u64=[](std::vector<std::uint8_t> const& bytes,std::size_t start) {
            std::uint64_t result=0;
            for(unsigned i=0;i<8;++i)result|=
                std::uint64_t(bytes.at(start+i))<<(8*i);
            return result;
        };
        auto begin=u64(originalXml,48);
        auto size=u64(originalXml,56);
        check(begin==u64(updatedXml,48) && size==u64(updatedXml,56) &&
              std::equal(originalXml.begin()+begin,
                         originalXml.begin()+begin+size,
                         updatedXml.begin()+begin),
              "Editing an XML YMAT damaged original NEF8 metadata");
        // Safe failure must not change bytes already on disk.
        auto invalid=sky;
        invalid["name"]="Another Material";
        bool rejected=false;
        try {ma::save_descriptor(root,selected,invalid);}
        catch(std::exception const&){rejected=true;}
        check(rejected && bytes(xmlTarget)==updatedXml,
              "Invalid YMAT entry identity overwrote source library");
        std::cout<<"NEF8 kind30/4 material entry edit, sibling preservation, "
                    "custom parameters, textures and rollback: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"YMAT editable library QA FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
