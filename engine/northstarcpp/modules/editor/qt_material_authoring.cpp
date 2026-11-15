#include "qt_material_authoring.hpp"
#include "qt_material_instances.hpp"
#include "editor_asset_thumbnails.hpp"
#include "editor_asset_material_source.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/material.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <QImageReader>
#include <QColor>
#include <northstarcpp/editor_asset_catalog.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace northstarcpp::editor::material_authoring {
namespace fs=std::filesystem;
namespace {
std::string bytes(QString const& s) {return s.toUtf8().toStdString();}
QString string(fs::path const& p) {
    auto value=p.generic_u8string();
    return QString::fromUtf8(value.data(),static_cast<int>(value.size()));
}
bool symlink_free(fs::path const& root,fs::path const& relative) {
    std::error_code ec;
    if(fs::is_symlink(root,ec))return false;
    auto current=root;
    for(auto const& part:relative) {
        current/=part;
        ec.clear();
        auto const status=fs::symlink_status(current,ec);
        // Material creation legitimately traverses not-yet-existing
        // Materials/ directories. Reject links/errors on existing portions.
        if(ec==std::errc::no_such_file_or_directory) {
            ec.clear();
            continue;
        }
        if(ec || fs::is_symlink(status))return false;
        // Windows junctions are followed by canonical() — ensure containment.
    }
    auto const normalized=fs::weakly_canonical(current,ec);
    if(ec)return false;
    auto const origin=fs::weakly_canonical(root,ec);
    if(ec)return false;
    auto const rel=normalized.lexically_relative(origin);
    return !rel.empty() && *rel.begin()!=".." && !rel.is_absolute();
}
QStringList enumerate(fs::path const& root,bool materials) {
    QStringList records;
    std::error_code ec;
    if(!fs::is_directory(root,ec))return records;
    fs::recursive_directory_iterator it(root,
        fs::directory_options::skip_permission_denied,ec),end;
    for(;it!=end && records.size()<15000;it.increment(ec)) {
        if(ec){ec.clear();continue;}
        auto const& item=*it;
        if(item.is_symlink(ec)) {if(item.is_directory(ec))it.disable_recursion_pending();continue;}
        if(!item.is_regular_file(ec))continue;
        auto const relative=item.path().lexically_relative(root);
        auto ref=string(relative);
        if(!safe_virtual_path(bytes(ref)))continue;
        if(materials?is_material(ref):is_texture(ref))
            records.push_back(ref);
    }
    records.sort(Qt::CaseInsensitive);
    records.removeDuplicates();
    return records;
}
void require_descriptor(fs::path const& root,QJsonObject const& object) {
    if(object.value("schema").toInt(-1)!=1 ||
       object.value("name").toString().trimmed().isEmpty() ||
       !QStringList{"standard_lit","unlit"}.contains(
           object.value("shader").toString()))
        throw std::invalid_argument("Invalid editable material descriptor");
    auto const color=object.value("base_color").toString();
    static QRegularExpression const hex("^#[0-9a-fA-F]{6}$");
    if(!hex.match(color).hasMatch())
        throw std::invalid_argument("Material Base Color must be #RRGGBB");
    for(auto const field:{"metallic","roughness"}) {
        auto const item=object.value(field);
        if(!item.isDouble() || item.toDouble()<0.0 || item.toDouble()>1.0)
            throw std::invalid_argument("Material parameters must be in [0,1]");
    }
    if(object.contains("graph")) {
        if(!object.value("graph").isObject())
            throw std::invalid_argument("Material graph must be a JSON object");
        auto const json=QJsonDocument(object.value("graph").toObject())
            .toJson(QJsonDocument::Compact);
        (void)northstarcpp::compile_material_graph_with_functions(
            json.toStdString(),[&root](std::string const& reference) {
                return material_function_source(root,reference);
            });
    }
    for(auto const field:{"base_color_texture","normal_texture",
                           "roughness_texture","metallic_texture","emissive_texture"}) {
        auto const value=object.value(field);
        if(value.isUndefined())continue;
        auto ref=value.toString();
        if(!value.isString() ||
           (!ref.isEmpty() && (!safe_virtual_path(bytes(ref)) || !is_texture(ref))))
            throw std::invalid_argument("Invalid material texture slot reference");
    }
}
}
bool is_material(QString const& path) {
    auto const p=path.toLower();
    return p.endsWith(".material.json") || p.endsWith(".material-instance.json") || p.endsWith(".ymat") ||
           p.endsWith(".material") || p.endsWith(".mtl");
}
bool is_texture(QString const& path) {
    auto const suffix=QFileInfo(path).suffix().toLower();
    return QStringList{"ytd","png","jpg","jpeg","dds","tga","bmp",
                       "webp","ktx","ktx2","tif","tiff"}.contains(suffix);
}
QStringList list_materials(fs::path const& content_root) {
    return enumerate(content_root,true);
}
QStringList list_material_functions(fs::path const& root) {
    QStringList records;
    std::error_code ec;
    if(!fs::is_directory(root,ec))return records;
    fs::recursive_directory_iterator it(root,
        fs::directory_options::skip_permission_denied,ec),end;
    for(;it!=end && records.size()<1024;it.increment(ec)) {
        if(ec){ec.clear();continue;}
        if(it->is_symlink(ec)) {
            if(it->is_directory(ec))it.disable_recursion_pending();
            continue;
        }
        if(!it->is_regular_file(ec))continue;
        auto relative=string(it->path().lexically_relative(root));
        if(relative.endsWith(".material-function.json",Qt::CaseInsensitive) &&
           safe_virtual_path(bytes(relative)) &&
           it->file_size(ec)<=32768)
            records.push_back(relative);
    }
    records.sort(Qt::CaseInsensitive);
    return records;
}
QStringList list_textures(fs::path const& content_root) {
    return enumerate(content_root,false);
}
fs::path resolve_existing(fs::path const& content_root,QString const& reference) {
    auto const virtual_path=bytes(reference);
    if(!safe_virtual_path(virtual_path))
        throw std::invalid_argument("Material reference contains invalid traversal");
    auto relative=fs::u8path(virtual_path);
    if(!symlink_free(content_root,relative))
        throw std::invalid_argument("Redirected material asset path");
    auto path=content_root/relative;
    std::error_code ec;
    if(!fs::is_regular_file(path,ec) || ec)
        throw std::runtime_error("Material asset not found in project content");
    return path;
}
std::string material_function_source(
    fs::path const& root,std::string const& reference) {
    QString name=QString::fromUtf8(reference.data(),int(reference.size()));
    if(!name.endsWith(".material-function.json",Qt::CaseInsensitive))
        throw std::invalid_argument("Material Function must use .material-function.json");
    auto path=resolve_existing(root,name);
    if(fs::file_size(path)>32768)
        throw std::invalid_argument("Material Function exceeds 32 KiB");
    std::ifstream source(path,std::ios::binary);
    if(!source)throw std::runtime_error("Cannot read Material Function");
    return std::string(std::istreambuf_iterator<char>(source),{});
}
QJsonObject load_descriptor(fs::path const& root,QString const& reference) {
    if(!reference.endsWith(".material.json",Qt::CaseInsensitive))
        throw std::invalid_argument("Native compiled materials are read-only");
    QFile file(string(resolve_existing(root,reference)));
    if(!file.open(QIODevice::ReadOnly) || file.size()>512*1024)
        throw std::runtime_error("Cannot read bounded material descriptor");
    QJsonParseError error;
    auto const doc=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError || !doc.isObject())
        throw std::runtime_error("Invalid material JSON");
    auto object=doc.object();
    require_descriptor(root,object);
    return object;
}
void save_descriptor(fs::path const& root,QString const& reference,
                     QJsonObject const& descriptor) {
    if(!reference.endsWith(".material.json",Qt::CaseInsensitive))
        throw std::invalid_argument("Cannot edit compiled YMAT material directly");
    require_descriptor(root,descriptor);
    auto relative=fs::u8path(bytes(reference));
    if(!safe_virtual_path(bytes(reference)) || !symlink_free(root,relative))
        throw std::invalid_argument("Unsafe destination material reference");
    for(auto const field:{"base_color_texture","normal_texture",
                           "roughness_texture","metallic_texture","emissive_texture"}) {
        auto value=descriptor.value(field).toString();
        if(!value.isEmpty())(void)resolve_existing(root,value);
    }
    auto target=root/relative;
    std::error_code ec;
    fs::create_directories(target.parent_path(),ec);
    if(ec)throw std::runtime_error("Cannot create project Materials directory");
    QSaveFile output(string(target));
    if(!output.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot open material for atomic save");
    auto const data=QJsonDocument(descriptor).toJson(QJsonDocument::Indented);
    if(output.write(data)!=data.size() || !output.commit())
        throw std::runtime_error("Cannot commit project material descriptor");
}
QString make_material_for_texture(fs::path const& root,QString const& texture) {
    if(!is_texture(texture))
        throw std::invalid_argument("Only texture assets create materials");
    (void)resolve_existing(root,texture);
    auto stem=QFileInfo(texture).completeBaseName();
    stem.replace(QRegularExpression("[^A-Za-z0-9_-]"),"_");
    if(stem.isEmpty())stem="Texture";
    stem=stem.left(72);
    QString ref;
    for(unsigned n=0;n<10000;++n) {
        ref="Materials/"+stem+"_Material"+
            (n?QString("_%1").arg(n):QString{})+".material.json";
        if(!QFileInfo::exists(string(root/fs::u8path(bytes(ref)))))break;
        ref.clear();
    }
    if(ref.isEmpty())throw std::runtime_error("Material filename slots exhausted");
    QJsonObject descriptor{{"schema",1},{"name",QFileInfo(ref).completeBaseName()},
        {"shader","standard_lit"},{"base_color","#ffffff"},
        {"metallic",0.0},{"roughness",0.65},
        {"base_color_texture",texture},{"normal_texture",""},
        {"roughness_texture",""},{"metallic_texture",""},{"emissive_texture",""}};
    save_descriptor(root,ref,descriptor);
    return ref;
}
QImage neutral_sphere_preview(int dimension) {
    northstarcpp::shared_assets::Material material;
    material.name="Preview loading";
    material.shader="standard_lit";
    material.parameters.push_back({"base_color","color",{.42f,.46f,.52f}});
    material.parameters.push_back({"metallic","float",{0.f}});
    material.parameters.push_back({"roughness","float",{.55f}});
    auto native=northstarcpp::editor_tools::preview_material(
        material,{},dimension);
    if(!native.valid())return {};
    QImage view(reinterpret_cast<uchar const*>(native.argb.data()),
        native.width,native.height,native.width*4,QImage::Format_ARGB32);
    return view.copy();
}
QImage sphere_preview(fs::path const& root,QString const& reference,int dimension) {
    if(dimension<64 || dimension>512 || !is_material(reference))return {};
    try {
        northstarcpp::shared_assets::Material material;
        std::vector<northstarcpp::shared_assets::ResolvedAsset<
            northstarcpp::shared_assets::Texture>> textures;
        if(reference.endsWith(".material.json",Qt::CaseInsensitive) ||
           is_material_instance(reference)) {
            auto const obj=is_material_instance(reference)?
                resolve_material_instance(root,reference):
                load_descriptor(root,reference);
            material.name=bytes(obj.value("name").toString());
            material.shader=bytes(obj.value("shader").toString());
            material.blend="opaque";
            auto const base=QColor(obj.value("base_color").toString());
            material.parameters.push_back({"base_color","color",
                {float(base.redF()),float(base.greenF()),float(base.blueF())}});
            material.parameters.push_back({"metallic","float",
                {float(obj.value("metallic").toDouble())}});
            material.parameters.push_back({"roughness","float",
                {float(obj.value("roughness").toDouble())}});
            for(auto const pair:std::initializer_list<
                std::pair<char const*,char const*>>{
                {"base_color_texture","base_color"},
                {"normal_texture","normal"},
                {"roughness_texture","roughness"},
                {"metallic_texture","metallic"},
                {"emissive_texture","emissive"}}) {
                QString const virtual_path=obj.value(pair.first).toString();
                if(virtual_path.isEmpty())continue;
                material.textures.push_back({
                    pair.second,bytes(virtual_path)});
                // No texture assets are ever assigned directly to the
                // object. Read material bindings only for this preview.
                auto const physical=resolve_existing(root,virtual_path);
                std::error_code ec;
                if(fs::file_size(physical,ec)>32*1024*1024 || ec)continue;
                northstarcpp::shared_assets::Texture texture;
                if(virtual_path.endsWith(".ytd",Qt::CaseInsensitive)) {
                    try {
                        northstarcpp::AssetManager assets;
                        assets.mount_directory("",root,0);
                        auto list=northstarcpp::shared_assets::load_textures(
                            assets,bytes(virtual_path));
                        if(!list.empty())texture=std::move(list.front());
                    }catch(std::exception const&) {}
                    if(texture.mips.empty())continue;
                }else {
                    QImageReader reader(string(physical));
                    reader.setAutoTransform(true);
                    auto const size=reader.size();
                    if(!size.isValid() || size.width()>16384 ||
                       size.height()>16384)continue;
                    // Native PBR preview only needs a small sampled mip.
                    if(size.width()>512 || size.height()>512)
                        reader.setScaledSize(size.scaled(512,512,Qt::KeepAspectRatio));
                    auto image=reader.read().convertToFormat(QImage::Format_RGBA8888);
                    if(image.isNull())continue;
                    texture.name=bytes(virtual_path);
                    texture.srgb=QString::fromUtf8(pair.second)=="base_color" ||
                                 QString::fromUtf8(pair.second)=="emissive";
                    texture.format_id=1;
                    auto const count=std::size_t(image.width())*image.height()*4;
                    texture.mips.push_back({
                        static_cast<std::uint32_t>(image.width()),
                        static_cast<std::uint32_t>(image.height()),0,count});
                    texture.data.resize(count);
                    for(int row=0;row<image.height();++row)
                        std::memcpy(texture.data.data()+
                                        std::size_t(row)*image.width()*4,
                                    image.constScanLine(row),
                                    std::size_t(image.width())*4);
                }
                textures.push_back({bytes(virtual_path),std::move(texture)});
            }
        }else {
            auto const loaded=loadMaterialPreviewSource(root,bytes(reference));
            if(!loaded.material)return {};
            material=*loaded.material;
            textures=loaded.textures;
        }
        auto sphere=northstarcpp::editor_tools::preview_material(
            material,textures,dimension);
        if(!sphere.valid())return {};
        // Its ARGB32 native pixel layout is compatible with QImage on Win64.
        QImage view(reinterpret_cast<uchar const*>(sphere.argb.data()),
            sphere.width,sphere.height,sphere.width*4,QImage::Format_ARGB32);
        return view.copy(); // Own memory after native preview goes out of scope.
    }catch(std::exception const&) {
        return {};
    }
}
} // namespace northstarcpp::editor::material_authoring
