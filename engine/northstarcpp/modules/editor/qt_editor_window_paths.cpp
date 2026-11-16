#include "qt_editor_window_paths.hpp"
#include "qt_editor_viewport.hpp"
#include "qt_material_authoring.hpp"
#include <northstarcpp/scene_component_registry.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <array>
#include <cmath>
#include <system_error>
namespace northstarcpp::editor::window_detail {
namespace fs=std::filesystem;
std::string utf8(QString const& value) {return value.toUtf8().toStdString();}
QString from8(std::string const& value) {return QString::fromUtf8(value.data(),int(value.size()));}
fs::path native(QString const& value) {return fs::u8path(utf8(value));}
QString qpath(fs::path const& value) {return QString::fromUtf8(value.u8string().c_str());}
QVariantList cameraValues(editor_tools::EditorCamera const& camera) {
    auto const& p=camera.pose;
    return {p.position.x,p.position.y,p.position.z,p.yaw,p.pitch,p.roll,
            p.fov_y_degrees};
}
bool applyCameraValues(QtEditorViewport* viewport,QVariant const& data) {
    auto list=data.toList();
    if(list.size()!=7)return false;
    auto pose=viewport->camera().pose;
    bool valid=true;
    std::array<float,7> values{};
    for(std::size_t i=0;i<values.size();++i) {
        bool ok=false;
        auto value=list[int(i)].toFloat(&ok);
        if(!ok || !std::isfinite(value)){valid=false;break;}
        values[i]=value;
    }
    if(!valid || values[6]<5.f || values[6]>150.f)return false;
    pose.position={values[0],values[1],values[2]};
    pose.yaw=values[3];pose.pitch=values[4];pose.roll=values[5];
    pose.fov_y_degrees=values[6];
    viewport->setCameraPose(pose);
    return true;
}
QString sceneCameraKey(fs::path const& file) {
    auto canonical=file.empty()?QByteArray("untitled"):
        QDir::cleanPath(qpath(file)).toUtf8();
    return "Camera/Scenes/"+
        QString::fromLatin1(QCryptographicHash::hash(
            canonical,QCryptographicHash::Sha256).toHex());
}
QString folderRoot(fs::path const& scene) {
    return qpath(scene.parent_path()/"assets");
}
fs::path sharedMaterialRoot() {
    // Resolve the shared library relative to the installed editor or
    // source development build without hardcoding a user's machine path.
    auto parent=fs::u8path(
        QCoreApplication::applicationDirPath().toUtf8().toStdString());
    for(unsigned level=0;level<8;++level) {
        auto const candidate=parent/"sharedAssets";
        std::error_code ec;
        if(fs::is_directory(candidate,ec) &&
           !fs::is_symlink(candidate,ec))
            return candidate;
        if(parent==parent.parent_path())break;
        parent=parent.parent_path();
    }
    return {};
}
bool availableMaterial(fs::path const& root,QString const& reference) {
    if(root.empty() || !material_authoring::is_material(reference))return false;
    try {
        // Scene actors carry YMAT@entry identity. The filesystem contains
        // only YMAT; @name is resolved by the asset codec, never treated as
        // a literal filename.
        auto const at=reference.indexOf('@');
        auto const physical=at<0?reference:reference.left(at);
        (void)material_authoring::resolve_existing(root,physical);
        return true;
    }catch(...) {return false;}
}
bool isMesh(QString const& path) {
    auto const suffix=QFileInfo(path).suffix();
    return suffix.compare("ydd",Qt::CaseInsensitive)==0 ||
           suffix.compare("ydr",Qt::CaseInsensitive)==0 ||
           suffix.compare("obj",Qt::CaseInsensitive)==0;
}
}
