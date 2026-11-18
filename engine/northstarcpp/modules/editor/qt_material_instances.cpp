#include "qt_material_instances.hpp"
#include "qt_material_authoring.hpp"
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/material.hpp>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <stdexcept>
#include <system_error>

namespace northstarcpp::editor::material_authoring {
namespace fs=std::filesystem;
namespace {
std::string utf8(QString const& value){return value.toUtf8().toStdString();}
QString qpath(fs::path const& value) {
    auto s=value.generic_u8string();
    return QString::fromUtf8(s.data(),int(s.size()));
}
bool local_source(QString const& ref) {
    return ref.endsWith(".ymat",Qt::CaseInsensitive) ||
           is_material_instance(ref);
}
void validate_overrides(QJsonObject const& overrides) {
    static QSet<QString> const accepted{
        "shader","base_color","roughness","metallic","opacity","alpha_cutoff",
        "blend_mode","base_color_texture","normal_texture",
        "roughness_texture","metallic_texture","emissive_texture","graph"};
    static QRegularExpression const hex("^#[0-9a-fA-F]{6}$");
    for(auto it=overrides.begin();it!=overrides.end();++it) {
        if(!accepted.contains(it.key()))
            throw std::invalid_argument("Unsupported instance override");
        if(it.key()=="base_color") {
            if(!it.value().isString() ||
               !hex.match(it.value().toString()).hasMatch())
                throw std::invalid_argument("Invalid instance Base Color override");
        }else if(it.key()=="shader") {
            if(!QStringList{"unlit","standard_lit"}.contains(it.value().toString()))
                throw std::invalid_argument("Unsupported instance shader");
        }else if(it.key()=="blend_mode") {
            if(!QStringList{"opaque","masked","translucent"}.contains(
                it.value().toString()))
                throw std::invalid_argument("Unsupported instance blend mode");
        }else if(it.key()=="graph") {
            if(!it.value().isObject())
                throw std::invalid_argument("Invalid instance graph override");
            auto const data=QJsonDocument(it.value().toObject()).toJson(
                QJsonDocument::Compact);
            (void)northstarcpp::compile_material_graph(data.toStdString());
        }else if(it.key().endsWith("_texture")) {
            if(!it.value().isString() ||
               (!it.value().toString().isEmpty() &&
                (!northstarcpp::editor::safe_virtual_path(utf8(it.value().toString())) ||
                 !is_texture(it.value().toString()))))
                throw std::invalid_argument("Unsafe instance texture");
        }else {
            double const value=it.value().toDouble(-1.0);
            if(!it.value().isDouble() || value<0. || value>1.)
                throw std::invalid_argument("Instance scalar override must be in [0,1]");
        }
    }
}
void validate_instance(fs::path const& root,QString const& reference,
                       QJsonObject const& object) {
    if(!is_material_instance(reference)||
       object.value("schema").toInt(-1)!=1)
        throw std::invalid_argument("Unsupported Material Instance schema");
    QString const parent=object.value("parent").toString();
    if(!local_source(parent) || parent==reference ||
       !northstarcpp::editor::safe_virtual_path(utf8(parent)))
        throw std::invalid_argument("Material Instance parent is unsafe");
    (void)resolve_existing(root,parent);
    if(!object.value("overrides").isObject())
        throw std::invalid_argument("Material Instance overrides must be an object");
    validate_overrides(object.value("overrides").toObject());
}
void verify_destination(fs::path const& root,QString const& reference) {
    if(!is_material_instance(reference) ||
       !reference.startsWith("Materials/") ||
       !northstarcpp::editor::safe_virtual_path(utf8(reference)))
        throw std::invalid_argument("Unsafe Material Instance destination");
    std::error_code ec;
    fs::path path=fs::u8path(utf8(reference));
    auto absolute=fs::weakly_canonical(root/path,ec);
    if(ec)throw std::runtime_error("Cannot resolve Material Instance destination");
    auto base=fs::weakly_canonical(root,ec);
    if(ec)throw std::runtime_error("Cannot resolve project root");
    auto relative=absolute.lexically_relative(base);
    if(relative.empty()||relative.is_absolute()||
       *relative.begin()=="..")
        throw std::invalid_argument("Redirected instance destination");
    fs::path current=root;
    for(auto const& part:path) {
        current/=part;
        ec.clear();
        auto status=fs::symlink_status(current,ec);
        if(ec==std::errc::no_such_file_or_directory)continue;
        if(ec || fs::is_symlink(status))
            throw std::invalid_argument("Instance destination contains link");
    }
}
QJsonObject read_raw(fs::path const& root,QString const& ref) {
    QFile file(qpath(resolve_existing(root,ref)));
    if(!file.open(QIODevice::ReadOnly)||file.size()>512*1024)
        throw std::runtime_error("Cannot read Material Instance");
    QJsonParseError error{};
    QJsonDocument doc=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!doc.isObject())
        throw std::invalid_argument("Invalid Material Instance JSON");
    return doc.object();
}
QJsonObject effective(fs::path const& root,QString const& reference,
                      QSet<QString>& visiting,int depth) {
    if(depth>8 || visiting.contains(reference))
        throw std::invalid_argument("Material Instance inheritance cycle/depth");
    visiting.insert(reference);
    struct Pop {
        QSet<QString>& s;QString key;
        ~Pop(){s.remove(key);}
    } pop{visiting,reference};
    if(!is_material_instance(reference))
        return load_descriptor(root,reference);
    QJsonObject instance=read_raw(root,reference);
    validate_instance(root,reference,instance);
    QJsonObject resolved=effective(root,instance.value("parent").toString(),
                                   visiting,depth+1);
    QJsonObject const overrides=instance.value("overrides").toObject();
    for(auto it=overrides.constBegin();it!=overrides.constEnd();++it)
        resolved.insert(it.key(),it.value());
    return resolved;
}
} // namespace
bool is_material_instance(QString const& ref) {
    return ref.endsWith(".material-instance.json",Qt::CaseInsensitive);
}
QJsonObject load_material_instance(fs::path const& root,QString const& ref) {
    if(!is_material_instance(ref))throw std::invalid_argument("Not an instance");
    QJsonObject document=read_raw(root,ref);
    validate_instance(root,ref,document);
    QSet<QString> visited;
    (void)effective(root,ref,visited,0);
    return document;
}
QJsonObject resolve_material_instance(fs::path const& root,QString const& ref) {
    QSet<QString> visited;
    return effective(root,ref,visited,0);
}
void save_material_instance(fs::path const& root,QString const& reference,
                            QJsonObject const& document) {
    verify_destination(root,reference);
    validate_instance(root,reference,document);
    // Do not persist a new inheritance edge that creates an existing cycle.
    QSet<QString> visited{reference};
    (void)effective(root,document.value("parent").toString(),visited,1);
    for(auto const& field:{"base_color_texture","normal_texture",
                           "roughness_texture","metallic_texture",
                           "emissive_texture"}) {
        QString ref=document.value("overrides").toObject().value(field).toString();
        if(!ref.isEmpty())(void)resolve_existing(root,ref);
    }
    auto target=root/fs::u8path(utf8(reference));
    std::error_code ec;
    fs::create_directories(target.parent_path(),ec);
    if(ec)throw std::runtime_error("Cannot create Material Instance directory");
    QSaveFile file(qpath(target));
    if(!file.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot open Material Instance for atomic save");
    QByteArray json=QJsonDocument(document).toJson(QJsonDocument::Indented);
    if(json.size()>512*1024 || file.write(json)!=json.size()||!file.commit())
        throw std::runtime_error("Cannot commit Material Instance");
}
QString create_material_instance(fs::path const& root,QString const& parent,
                                 QString const& name) {
    if(!local_source(parent))throw std::invalid_argument("Parent must be local material");
    (void)resolve_material_instance(root,parent);
    QString normalized=name.trimmed();
    static QRegularExpression const valid("^[A-Za-z0-9_-]{1,72}$");
    if(!valid.match(normalized).hasMatch())
        throw std::invalid_argument("Instance name must contain 1-72 ASCII letters/numbers/_/-");
    QString ref;
    for(unsigned n=0;n<10000;++n) {
        QString candidate="Materials/"+normalized+(n?QString("_%1").arg(n):QString{})+
                          ".material-instance.json";
        if(!QFileInfo::exists(qpath(root/fs::u8path(utf8(candidate))))) {
            ref=candidate;break;
        }
    }
    if(ref.isEmpty())throw std::runtime_error("Instance filename slots exhausted");
    save_material_instance(root,ref,
        QJsonObject{{"schema",1},{"parent",parent},{"overrides",QJsonObject{}}});
    return ref;
}
} // namespace northstarcpp::editor::material_authoring
