// Modal Unreal-style NorthStar asset inspector. CPU decoding and mesh preview
// work without a configured Vulkan device or running engine.assets provider.
#include "qt_asset_preview_dialog.hpp"
#include "qt_asset_warnings.hpp"
#include "qt_prefab_preview.hpp"
#include "qt_material_function_editor.hpp"
#include "qt_material_asset_dialog.hpp"
#include "editor_prefab_composition.hpp"
#include "editor_asset_thumbnails.hpp"
#include "editor_asset_material_source.hpp"
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/material.hpp>
#include <northstarcpp/skeleton.hpp>

#include <QtConcurrent/QtConcurrent>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QTextEdit>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

namespace northstarcpp::editor {
namespace fs=std::filesystem;
namespace {
QString qt(std::string const& value) {
    return QString::fromUtf8(value.data(),static_cast<int>(value.size()));
}
QImage imageFrom(editor_tools::AssetPreviewImage const& source) {
    if(!source.valid())return {};
    QImage shallow(reinterpret_cast<uchar const*>(source.argb.data()),
                   source.width,source.height,source.width*4,
                   QImage::Format_ARGB32);
    return shallow.copy();
}
QString formatName(std::uint16_t id) {
    switch(id) {
    case 1:return "RGBA8";
    case 101:return "BC1";
    case 102:return "BC4";
    case 103:return "BC3";
    case 104:return "BC7";
    default:return "NETD "+QString::number(id);
    }
}
QString sizeString(std::size_t bytes) {
    if(bytes>1024u*1024u)
        return QString::number(double(bytes)/(1024.*1024.),'f',2)+" MiB";
    return QString::number(double(bytes)/1024.,'f',1)+" KiB";
}
QImage readImage(fs::path const& path) {
    QImageReader reader(QString::fromStdWString(path.wstring()));
    reader.setAutoTransform(true);
    auto size=reader.size();
    if(size.width()>4096 || size.height()>4096)
        reader.setScaledSize(size.scaled(QSize(4096,4096),Qt::KeepAspectRatio));
    return reader.read();
}
struct LoadedAsset {
    std::optional<shared_assets::Texture> texture;
    std::optional<shared_assets::Drawable> drawable;
    std::vector<MaterialDefinition> drawable_materials;
    std::optional<shared_assets::Material> material;
    std::vector<shared_assets::ResolvedAsset<shared_assets::Texture>> material_textures;
    QImage image;
    QString message;
    QString details;
    std::vector<AssetWarning> warnings;
    bool supported=false;
};
struct LoadedPrefab {
    std::optional<Scene> prefab;
    std::optional<prefab_preview::Composition> composition;
    QString error;
};

LoadedAsset loadAsset(fs::path const& root,std::string const& reference,
                      AssetKind kind) {
    LoadedAsset result;
    try {
        auto const at=reference.rfind('@');
        auto const relative=at==std::string::npos ? reference :
                            reference.substr(0,at);
        auto const name=at==std::string::npos ?std::string{}:
                        reference.substr(at+1);
        if(!safe_virtual_path(relative) || relative.empty() ||
           (at!=std::string::npos && (name.empty() ||
           name.find_first_of("/\\@:")!=std::string::npos)))
            throw std::runtime_error("Unsafe asset path");
        std::error_code ec;
        auto canonical_root=fs::weakly_canonical(root,ec);
        if(ec)throw std::runtime_error("Invalid project Content root");
        auto physical=root/fs::u8path(relative);
        auto canonical_file=fs::weakly_canonical(physical,ec);
        if(ec)throw std::runtime_error("Invalid source asset");
        auto parent=canonical_root.begin();
        auto item=canonical_file.begin();
        for(;parent!=canonical_root.end();++parent,++item)
            if(item==canonical_file.end() || *item!=*parent)
                throw std::runtime_error("Source path escapes Content root");
        if(fs::is_symlink(fs::symlink_status(physical,ec)) || ec)
            throw std::runtime_error("Symbolic-link assets are not allowed");
        auto size=fs::file_size(physical,ec);
        if(ec || size>128u*1024u*1024u)
            throw std::runtime_error("Asset missing or exceeds 128 MiB limit");
        result.details="Source: "+qt(relative)+"\nCompressed: "+sizeString(size);
        if(kind==AssetKind::material) {
            auto source=loadMaterialPreviewSource(root,reference);
            if(!source.material)throw std::runtime_error("Material entry not found");
            auto const& material=*source.material;
            result.details+="\nMaterial: "+qt(material.name)+
                "\nShader: "+qt(material.shader)+
                "\nBlend: "+qt(material.blend)+
                "\nTwo-sided: "+QString(material.two_sided?"Yes":"No")+
                "\nTexture bindings: "+QString::number(material.textures.size())+
                "\nResolved textures: "+QString::number(source.textures.size())+
                "\nParameters: "+QString::number(material.parameters.size());
            for(auto const& parameter:material.parameters) {
                result.details+="\n  "+qt(parameter.name)+": ";
                for(auto i=0u;i<parameter.values.size();++i) {
                    if(i)result.details+=", ";
                    result.details+=QString::number(parameter.values[i],'f',3);
                }
            }
            for(auto const& missing:source.missing)
                result.warnings.push_back({
                    AssetWarningKind::Texture,qt(missing),
                    "Referenced by this material but not found in the mounted assets."});
            result.material=std::move(source.material);
            result.material_textures=std::move(source.textures);
            result.supported=true;
        } else if(!name.empty() && (kind==AssetKind::texture || kind==AssetKind::mesh)) {
            std::ifstream input(physical,std::ios::binary);
            if(!input)throw std::runtime_error("Cannot open ListFile");
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            if(!input.read(reinterpret_cast<char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("Truncated ListFile");
            if(kind==AssetKind::texture) {
                auto parsed=shared_assets::load_textures_from_bytes(bytes,relative);
                auto found=std::find_if(parsed.begin(),parsed.end(),
                    [&](auto const& t){return t.name==name;});
                if(found==parsed.end())throw std::runtime_error("Texture not found in YTD");
                result.texture=std::move(*found);
                auto const& tex=*result.texture;
                result.image=imageFrom(
                    editor_tools::preview_texture(tex,2048,0));
                if(result.image.isNull())
                    throw std::runtime_error("Unsupported or invalid texture payload");
                if(tex.mips.empty())throw std::runtime_error("Texture has no mip levels");
                result.details+="\nEntry: "+qt(name)+
                    "\nFormat: "+formatName(tex.format_id)+
                    "\nResolution: "+QString::number(tex.mips[0].width)+" × "+
                                      QString::number(tex.mips[0].height)+
                    "\nMip levels: "+QString::number(tex.mips.size())+
                    "\nColor space: "+QString(tex.srgb?"sRGB":"Linear")+
                    "\nTexture payload: "+sizeString(tex.data.size());
                result.supported=true;
            } else {
                auto parsed=shared_assets::load_drawables_from_bytes(bytes,relative);
                auto found=std::find_if(parsed.begin(),parsed.end(),
                    [&](auto const& d){return d.name==name;});
                if(found==parsed.end())throw std::runtime_error("Drawable not found in YDD");
                result.drawable=std::move(*found);
                auto const& mesh=*result.drawable;
                std::size_t vertices=0,triangles=0;
                for(auto const& part:mesh.meshes) {
                    vertices+=part.vertices.size();
                    triangles+=part.indices.size()/3;
                }
                // Use the same native material and skeleton decoders as
                // Player. Root may be sharedAssets or a project Content root.
                AssetManager resources;
                resources.mount_shared_assets(root);
                std::unordered_map<std::string,MaterialDefinition> definitions;
                std::size_t material_count=0;
                result.drawable_materials.reserve(mesh.meshes.size());
                for(auto const& part:mesh.meshes) {
                    if(part.material.empty()) {
                        result.drawable_materials.emplace_back();
                        continue;
                    }
                    try {
                        auto found=definitions.find(part.material);
                        if(found==definitions.end())
                            found=definitions.emplace(part.material,
                                load_material_definition(resources,part.material)).first;
                        result.drawable_materials.push_back(found->second);
                        ++material_count;
                    } catch(std::exception const& e) {
                        // An absent material must be visible in the details
                        // rather than silently flattened into an OBJ proxy.
                        result.warnings.push_back({
                            AssetWarningKind::Material,qt(part.material),qt(e.what())});
                        result.drawable_materials.emplace_back();
                    }
                }
                result.details+="\nNative YMAT/YTD sections: "+
                    QString::number(material_count)+"/"+
                    QString::number(mesh.meshes.size());
                if(relative.size()>=4 &&
                   (relative.compare(relative.size()-4,4,".ydd")==0 ||
                    relative.compare(relative.size()-4,4,".ydr")==0)) {
                    auto skeleton_path=fs::path(relative);
                    skeleton_path.replace_extension(".skeleton.ymt");
                    auto const skeleton_ref=skeleton_path.generic_u8string();
                    try {
                        auto skeleton=load_skeleton(resources,skeleton_ref);
                        result.details+="\nSkeleton: "+qt(skeleton_ref)+
                            "\nJoints: "+QString::number(skeleton.joints.size());
                    } catch (std::exception const&) {
                        result.details+="\nSkeleton: not attached";
                    }
                }
                result.details+="\nEntry: "+qt(name)+
                    "\nMesh parts: "+QString::number(mesh.meshes.size())+
                    "\nVertices: "+QString::number(vertices)+
                    "\nTriangles: "+QString::number(triangles)+
                    "\nBounds min: "+QString("%1, %2, %3").arg(
                    mesh.bounds_min[0]).arg(mesh.bounds_min[1]).arg(mesh.bounds_min[2])+
                    "\nBounds max: "+QString("%1, %2, %3").arg(
                    mesh.bounds_max[0]).arg(mesh.bounds_max[1]).arg(mesh.bounds_max[2]);
                result.supported=true;
            }
        } else if(kind==AssetKind::texture ||
                  (relative.size()>=4 && (relative.rfind(".png")==relative.size()-4 ||
                     relative.rfind(".jpg")==relative.size()-4 ||
                     relative.rfind(".bmp")==relative.size()-4))) {
            result.image=readImage(physical);
            if(result.image.isNull())throw std::runtime_error("Image decoder failed");
            result.details+="\nResolution: "+QString::number(result.image.width())+
                " × "+QString::number(result.image.height());
            result.supported=true;
        } else {
            // Unknown/binary assets are not misrepresented as a mesh/texture.
            result.message="No visual decoder is available for this asset type.";
        }
    } catch(std::exception const& e) {
        result.message=qt(e.what());
        auto const at=reference.find('@');
        auto const asset_path=reference.substr(0,at);
        std::error_code missing_error;
        bool const missing=!fs::exists(root/fs::u8path(asset_path),missing_error);
        AssetWarningKind warning_kind=AssetWarningKind::Other;
        if(kind==AssetKind::texture)
            warning_kind=AssetWarningKind::Texture;
        else if(kind==AssetKind::mesh)
            warning_kind=AssetWarningKind::Model;
        else if(kind==AssetKind::material)
            warning_kind=AssetWarningKind::Material;
        // Surface the underlying path and exception in a standalone
        // warning card even when the preview never becomes available.
        result.warnings.push_back({warning_kind,qt(reference),
            (missing?"Resource file not found. ":"")+result.message});
    }
    return result;
}

class AssetPreviewCanvas final:public QWidget {
public:
    explicit AssetPreviewCanvas(QWidget* parent=nullptr):QWidget(parent) {
        setObjectName("assetPreviewCanvas");
        setMinimumSize(480,400);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setProperty("previewReady",false);
        setStyleSheet("background:#131923");
    }
    void setMaterial(shared_assets::Material material,
                     std::vector<shared_assets::ResolvedAsset<shared_assets::Texture>> textures) {
        material_=std::move(material);
        material_textures_=std::move(textures);
        texture_.reset();drawable_.reset();external_={};displayed_={};
        zoom_=1.f;yaw_=0.f;pitch_=0.f;offset_={};
        dirty_=true;
        setProperty("previewReady",true);
        setProperty("previewRendered",false);
        update();
    }
    void setTexture(shared_assets::Texture texture,QImage decoded) {
        material_.reset();material_textures_.clear();
        texture_=std::move(texture);drawable_.reset();external_={};
        current_mip_=0;displayed_=std::move(decoded);
        zoom_=1.f;offset_={};
        setProperty("previewReady",!displayed_.isNull());update();
    }
    void setDrawable(shared_assets::Drawable drawable,
                     std::vector<MaterialDefinition> materials) {
        material_.reset();material_textures_.clear();
        drawable_materials_=std::move(materials);
        drawable_=std::move(drawable);texture_.reset();external_={};
        zoom_=1.f;yaw_=0.62f;pitch_=0.27f;wireframe_=false;
        dirty_=true;
        setProperty("previewReady",true);
        setProperty("previewRendered",false);
        update();
    }
    void setImage(QImage image) {
        material_.reset();material_textures_.clear();
        texture_.reset();drawable_.reset();external_=std::move(image);
        displayed_=external_;zoom_=1.f;offset_={};
        setProperty("previewReady",!displayed_.isNull());update();
    }
    void setMip(int mip) {
        if(!texture_ || mip<0 || std::size_t(mip)>=texture_->mips.size())return;
        current_mip_=mip;
        displayed_=imageFrom(editor_tools::preview_texture(*texture_,2048,
                                                           std::size_t(mip)));
        zoom_=1.f;offset_={};
        setProperty("previewReady",!displayed_.isNull());
        update();
    }
    void setWireframe(bool on) {wireframe_=on;dirty_=true;update();}
    void setLightIntensity(float intensity) {
        light_intensity_=std::clamp(intensity,0.f,3.f);
        dirty_=true;
        setProperty("previewRendered",false);
        update();
    }
    void reset() {
        zoom_=1.f;offset_={};yaw_=.62f;pitch_=.27f;
        dirty_=true;update();
    }
    void oneToOne() {
        if(displayed_.isNull())return;
        float const fit=std::min(float(width())/displayed_.width(),
                                 float(height())/displayed_.height());
        zoom_=std::clamp(1.f/std::max(fit,.001f),.1f,20.f);
        offset_={};update();
    }
    void setZoom(float value) {zoom_=std::clamp(value,.1f,20.f);dirty_=true;update();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform,true);
        painter.fillRect(rect(),QColor("#111824"));
        if(material_) {
            auto const dimension=std::clamp(std::min(width(),height()),128,720);
            if(dirty_ || model_image_.size()!=QSize(dimension,dimension)) {
                model_image_=imageFrom(editor_tools::preview_material(
                    *material_,material_textures_,dimension,yaw_,pitch_,zoom_));
                dirty_=false;
            }
            if(!model_image_.isNull()) {
                auto side=std::min(width(),height());
                painter.drawImage(QRect((width()-side)/2,(height()-side)/2,
                                        side,side),model_image_);
                setProperty("previewRendered",true);
            }
            return;
        }
        if(drawable_) {
            auto const dimension=std::clamp(std::min(width(),height()),128,900);
            if(dirty_ || model_image_.size()!=QSize(dimension,dimension)) {
                model_image_=imageFrom(editor_tools::preview_drawable(
                    *drawable_,dimension,dimension,yaw_,pitch_,
                    std::clamp(zoom_,.35f,7.f),wireframe_,
                    &drawable_materials_,light_intensity_));
                dirty_=false;
            }
            if(!model_image_.isNull()) {
                auto side=std::min(width(),height());
                painter.drawImage(QRect((width()-side)/2,(height()-side)/2,
                                        side,side),model_image_);
                setProperty("previewRendered",true);
            }
            return;
        }
        if(displayed_.isNull()) {
            painter.setPen(QColor("#aebdd0"));
            painter.drawText(rect(),Qt::AlignCenter,
                "Loading preview…");
            return;
        }
        float const fit=std::min(float(width())/displayed_.width(),
                                 float(height())/displayed_.height());
        float const scaled=fit*zoom_;
        auto const w=displayed_.width()*scaled,h=displayed_.height()*scaled;
        QRectF destination((width()-w)*.5+offset_.x(),
                           (height()-h)*.5+offset_.y(),w,h);
        // Neutral checker beneath alpha textures.
        painter.setClipRect(destination.intersected(QRectF(rect())));
        int const grid=16;
        auto bounds=destination.intersected(QRectF(rect())).toAlignedRect();
        for(int y=bounds.top()/grid*grid;y<=bounds.bottom();y+=grid)
            for(int x=bounds.left()/grid*grid;x<=bounds.right();x+=grid)
                painter.fillRect(x,y,grid,grid,
                    ((x/grid+y/grid)&1)?QColor("#55606b"):QColor("#3c4651"));
        painter.drawImage(destination,displayed_);
        setProperty("previewRendered",true);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton) {
            last_=event->position();dragging_=true;
            setCursor(Qt::ClosedHandCursor);
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if(!dragging_)return;
        QPointF const delta=event->position()-last_;
        last_=event->position();
        if(drawable_ || material_) {
            yaw_+=float(delta.x())*.011f;
            pitch_=std::clamp(pitch_+float(delta.y())*.009f,-1.42f,1.42f);
            dirty_=true;
        } else offset_+=delta;
        update();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton) {
            dragging_=false;unsetCursor();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent*) override {reset();}
    void wheelEvent(QWheelEvent* event) override {
        float const steps=float(event->angleDelta().y())/120.f;
        zoom_=std::clamp(zoom_*std::pow(1.16f,(drawable_||material_)?-steps:steps),
                         (drawable_||material_)?.35f:.1f,
                         (drawable_||material_)?7.f:20.f);
        dirty_=true;
        update();event->accept();
    }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        dirty_=true;
    }
private:
    std::optional<shared_assets::Texture> texture_;
    std::optional<shared_assets::Drawable> drawable_;
    std::optional<shared_assets::Material> material_;
    std::vector<shared_assets::ResolvedAsset<shared_assets::Texture>> material_textures_;
    std::vector<MaterialDefinition> drawable_materials_;
    QImage external_,displayed_,model_image_;
    int current_mip_=0;
    float yaw_=.62f,pitch_=.27f,zoom_=1.f,light_intensity_=1.f;
    QPointF offset_,last_;
    bool dragging_=false,wireframe_=false,dirty_=true;
};

// Read-only NEF8 rig and clip inspector. Shares the authoritative engine
// skeleton parser and YCD clip table validation, not a second Qt decoder.
struct NativeRigInspection {
    std::optional<Skeleton> skeleton;
    std::vector<shared_assets::AnimationClipInfo> clips;
    QString failure;
};
NativeRigInspection inspectRig(fs::path const& root,
                              std::string const& reference,AssetKind kind) {
    NativeRigInspection result;
    try {
        auto const separator=reference.find('@');
        auto const relative=reference.substr(0,separator);
        auto const selected=separator==std::string::npos?
            std::string{}:reference.substr(separator+1);
        if(!safe_virtual_path(relative) ||
           (separator!=std::string::npos &&
           (selected.empty()||selected.size()>256||
            selected.find_first_of("@/\\:")!=std::string::npos)))
            throw std::runtime_error("Unsafe YMT/YCD asset reference");
        std::error_code ec;
        auto const canonical_root=fs::weakly_canonical(root,ec);
        if(ec)throw std::runtime_error("Invalid Content root");
        auto const physical=root/fs::u8path(relative);
        auto const canonical=fs::weakly_canonical(physical,ec);
        if(ec || !fs::is_regular_file(canonical))
            throw std::runtime_error("YMT/YCD asset is missing");
        auto part=canonical.begin();
        for(auto root_part=canonical_root.begin();root_part!=canonical_root.end();
            ++root_part,++part) {
            if(part==canonical.end() || *part!=*root_part)
                throw std::runtime_error("Asset path escapes Content root");
        }
        auto const size=fs::file_size(canonical,ec);
        if(ec || size<32 || size>64u*1024u*1024u)
            throw std::runtime_error("Invalid or oversized NEF8 file");
        std::ifstream input(canonical,std::ios::binary);
        if(!input)throw std::runtime_error("Cannot open NEF8 file");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if(!input.read(reinterpret_cast<char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Incomplete NEF8 payload");
        if(kind==AssetKind::skeleton) {
            result.skeleton=load_skeleton_from_bytes(bytes);
            if(!selected.empty()) {
                const auto& joints=result.skeleton->joints;
                if(std::none_of(joints.begin(),joints.end(),
                    [&](SkeletonJoint const& joint){return joint.name==selected;}))
                    throw std::runtime_error("YMT joint not found");
            }
        }else {
            result.clips=shared_assets::inspect_animation_clip_dictionary(bytes);
            if(!selected.empty() &&
               std::none_of(result.clips.begin(),result.clips.end(),
                    [&](auto const& clip){return clip.name==selected;}))
                throw std::runtime_error("YCD clip not found");
        }
    }catch(std::exception const& e) {
        result.failure=QString::fromUtf8(e.what());
    }
    return result;
}
int openNativeRigInspector(QWidget* parent,fs::path const& content_root,
                           std::string const& reference,AssetKind kind) {
    QDialog dialog(parent);
    dialog.setObjectName("nativeRigClipPreviewDialog");
    dialog.setWindowTitle(QString(kind==AssetKind::skeleton?
        "NorthStar Skeleton Inspector — ":"NorthStar Animation Inspector — ")+
        qt(reference));
    dialog.resize(1040,680);
    dialog.setStyleSheet(
        "QDialog{background:#151d28;color:#dce9f5}"
        "QTreeWidget{background:#1c2838;color:#e5ebf3;alternate-background-color:#233145;"
        "border:1px solid #3c5067;selection-background-color:#315c84}"
        "QHeaderView::section{background:#26384b;color:#cedded;padding:7px;"
        "border:1px solid #39516a}"
        "QLabel{color:#b7cce2}"
        "QPushButton{padding:7px 15px;background:#314a60;color:#fff;border-radius:5px}");
    auto* layout=new QVBoxLayout(&dialog);
    auto* title=new QLabel(qt(reference),&dialog);
    title->setWordWrap(true);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont heading=title->font();heading.setBold(true);heading.setPointSize(13);
    title->setFont(heading);layout->addWidget(title);
    auto* tree=new QTreeWidget(&dialog);
    tree->setObjectName(kind==AssetKind::skeleton?
        "skeletonHierarchyTree":"animationClipList");
    tree->setAlternatingRowColors(true);
    if(kind==AssetKind::skeleton)
        tree->setHeaderLabels({"Joint hierarchy","Parent","Bind position","Index"});
    else
        tree->setHeaderLabels({"Clip","Frames","FPS","Tracks","Duration","Loop"});
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(tree,1);
    auto* details=new QLabel("Loading native NEF8 data...",&dialog);
    details->setObjectName("nativeRigClipDetails");
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details->setWordWrap(true);
    details->setMinimumHeight(64);
    layout->addWidget(details);
    auto* row=new QHBoxLayout;
    auto* status=new QLabel("Read-only • Native engine codec",&dialog);
    status->setStyleSheet("color:#86b3d0");
    row->addWidget(status,1);
    auto* copy=new QPushButton("Copy reference",&dialog);
    row->addWidget(copy);
    QObject::connect(copy,&QPushButton::clicked,&dialog,
        [reference]{QApplication::clipboard()->setText(qt(reference));});
    auto* close=new QPushButton("Close",&dialog);
    row->addWidget(close);
    QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
    layout->addLayout(row);
    auto* watcher=new QFutureWatcher<NativeRigInspection>(&dialog);
    QObject::connect(watcher,&QFutureWatcher<NativeRigInspection>::finished,
        &dialog,[watcher,tree,details,status,kind,reference] {
            auto data=watcher->result();
            if(!data.failure.isEmpty()) {
                details->setText("Cannot decode native asset: "+data.failure);
                status->setText("Invalid or unsupported NEF8 resource");
                watcher->deleteLater();
                return;
            }
            auto const separator=reference.find('@');
            auto const selected=separator==std::string::npos?
                std::string{}:reference.substr(separator+1);
            if(kind==AssetKind::skeleton && data.skeleton) {
                auto const& rig=*data.skeleton;
                std::vector<QTreeWidgetItem*> items;
                items.reserve(rig.joints.size());
                for(std::size_t i=0;i<rig.joints.size();++i) {
                    auto const& joint=rig.joints[i];
                    QString const position=QString("%1, %2, %3")
                        .arg(joint.bind.position.x,0,'f',3)
                        .arg(joint.bind.position.y,0,'f',3)
                        .arg(joint.bind.position.z,0,'f',3);
                    auto* item=new QTreeWidgetItem(QStringList{
                        qt(joint.name),
                        joint.parent>=0?qt(rig.joints[static_cast<std::size_t>(
                            joint.parent)].name):QString("Root"),
                        position,QString::number(i)});
                    if(joint.parent>=0)
                        items[static_cast<std::size_t>(joint.parent)]->addChild(item);
                    else tree->addTopLevelItem(item);
                    items.push_back(item);
                    if(joint.name==selected)tree->setCurrentItem(item);
                }
                tree->expandToDepth(1);
                if(!selected.empty()&&tree->currentItem())
                    tree->scrollToItem(tree->currentItem());
                details->setText(QString(
                    "Skeleton: %1 joints   •   Eye height: %2 m   •   "
                    "Inverse bind matrices: %3\n"
                    "Mirrored helper pairs: %4   •   Secondary constraints: %5")
                    .arg(rig.joints.size())
                    .arg(rig.eye_height,0,'f',2)
                    .arg(rig.inverse_bind.size())
                    .arg(rig.mirrored_helpers.size())
                    .arg(rig.secondary_palette_constraints.size()));
                status->setText("YMT / Native skeleton decoded");
            }else if(kind==AssetKind::animation) {
                for(auto const& clip:data.clips) {
                    auto* item=new QTreeWidgetItem(tree,{
                        qt(clip.name),QString::number(clip.frames),
                        QString::number(clip.sample_rate,'f',2),
                        QString::number(clip.tracks),
                        QString::number(clip.duration,'f',2)+" s",
                        clip.loop?"Loop":"Once"});
                    if(clip.name==selected)tree->setCurrentItem(item);
                }
                tree->resizeColumnToContents(0);
                details->setText(QString(
                    "Native YCD dictionary: %1 clips\n"
                    "Decoded clip table; animation playback requires a compatible YMT skeleton.")
                    .arg(data.clips.size()));
                status->setText("YCD / Native animation metadata decoded");
            }
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run(
        [root=content_root,reference,kind] {
            return inspectRig(root,reference,kind);
        }));
    return dialog.exec();
}
} // namespace

int openAssetPreviewModal(QWidget* parent,fs::path const& content_root,
                          std::string const& asset_reference,AssetKind kind) {
    if(kind==AssetKind::skeleton || kind==AssetKind::animation)
        return openNativeRigInspector(parent,content_root,asset_reference,kind);
    if(kind==AssetKind::material_function)
        return openMaterialFunctionEditor(parent,content_root,asset_reference);
    if(kind==AssetKind::material) {
        // Keep the existing categorized missing-resource warning panel for
        // broken references; real material assets open the tabbed editor.
        auto const at=asset_reference.find('@');
        auto const file=asset_reference.substr(0,at);
        auto const physical=content_root/fs::u8path(file);
        std::error_code error;
        if(fs::is_regular_file(physical,error) && !error)
            return openMaterialAssetDialog(parent,content_root,asset_reference);
    }
    // Prefab is a scene graph template, not a drawable or a plain scene.
    // Show an isolated composition preview and its hierarchy summary.
    if(kind==AssetKind::prefab) {
        QDialog dialog(parent);
        dialog.setObjectName("prefabPreviewDialog");
        dialog.setWindowTitle("Prefab Preview - "+qt(asset_reference));
        dialog.resize(890,650);
        auto* layout=new QHBoxLayout(&dialog);
        auto* preview=new AssetPreviewCanvas(&dialog);
        preview->setObjectName("prefabPreviewCanvas");
        preview->setMinimumSize(480,480);
        layout->addWidget(preview,3);
        auto* side=new QVBoxLayout;
        auto* title=new QLabel(qt(asset_reference),&dialog);
        title->setWordWrap(true);
        title->setStyleSheet("font-weight:bold;font-size:16px");
        side->addWidget(title);
        auto* prefabWarnings=new AssetWarningPanel(&dialog);
        side->addWidget(prefabWarnings);
        auto* meta=new QLabel("Loading native prefab composition...",&dialog);
        meta->setObjectName("prefabPreviewDetails");
        meta->setWordWrap(true);
        meta->setTextInteractionFlags(Qt::TextSelectableByMouse);
        side->addWidget(meta);
        side->addStretch();
        auto* wire=new QCheckBox("Wireframe",&dialog);
        wire->setObjectName("prefabPreviewWireframe");
        QObject::connect(wire,&QCheckBox::toggled,preview,
            [preview](bool enabled){preview->setWireframe(enabled);});
        side->addWidget(wire);
        auto* light_label=new QLabel("Studio key light: 100%",&dialog);
        side->addWidget(light_label);
        auto* key_light=new QSlider(Qt::Horizontal,&dialog);
        key_light->setObjectName("prefabPreviewKeyLight");
        key_light->setRange(0,250);
        key_light->setValue(100);
        QObject::connect(key_light,&QSlider::valueChanged,preview,
            [preview,light_label](int value) {
                light_label->setText(QString("Studio key light: %1%").arg(value));
                preview->setLightIntensity(float(value)*.01f);
            });
        side->addWidget(key_light);
        auto* reset=new QPushButton("Fit / Reset",&dialog);
        QObject::connect(reset,&QPushButton::clicked,preview,
                         [preview]{preview->reset();});
        side->addWidget(reset);
        auto* close=new QPushButton("Close",&dialog);
        QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
        side->addWidget(close);
        layout->addLayout(side,1);
        // Keep the GUI event loop responsive while resolving multi-YDD
        // geometry and YMAT/YTD textures through engine.assets.
        auto* watcher=new QFutureWatcher<LoadedPrefab>(&dialog);
        QObject::connect(watcher,&QFutureWatcher<LoadedPrefab>::finished,
                         &dialog,[watcher,preview,meta,prefabWarnings,
                                  asset_reference] {
            LoadedPrefab loaded;
            try {loaded=watcher->result();}
            catch(std::exception const& e) {loaded.error=qt(e.what());}
            if(loaded.prefab && loaded.composition) {
                auto const& prefab=*loaded.prefab;
                auto& composition=*loaded.composition;
                QString information=prefab_preview::details(prefab);
                information+=QString("\n\n3D Composition\nVisible: %1\n"
                                     "Native models: %2\nProxies: %3\n"
                                     "Triangles: %4\nMaterial sections: %5\n"
                                     "Missing models: %6")
                    .arg(composition.visible_objects)
                    .arg(composition.modeled_objects)
                    .arg(composition.proxy_objects)
                    .arg(composition.triangles)
                    .arg(composition.drawable.meshes.size())
                    .arg(composition.missing_assets);
                std::vector<AssetWarning> issues;
                for(auto const& issue:composition.warnings) {
                    auto const message=qt(issue);
                    if(message.startsWith("Missing model ")) {
                        auto const path=message.mid(14).section(" for ",0,0);
                        issues.push_back({AssetWarningKind::Model,path,message});
                    }else if(message.startsWith("Missing material ")) {
                        auto const path=message.mid(17).section(": ",0,0);
                        issues.push_back({AssetWarningKind::Material,path,message});
                    }else {
                        issues.push_back({AssetWarningKind::Other,
                            qt(asset_reference),message});
                    }
                }
                if(!composition.drawable.meshes.empty()) {
                    preview->setDrawable(std::move(composition.drawable),
                                         std::move(composition.materials));
                    preview->setProperty("previewMode","prefab-composition");
                    preview->setProperty("prefabActorCount",
                        static_cast<int>(prefab.objects.size()));
                    information+=(prefab.objects.size()==1?
                        "\n\n3D model: orbit with mouse, zoom with wheel":
                        "\n\n3D composition: orbit with mouse, zoom with wheel");
                    information+="\nNative YDD/YDR meshes and YMAT/YTD textures"
                                 "\nSoftware studio lighting (not GPU PBR)";
                }else {
                    preview->setImage(prefab_preview::thumbnail(prefab,640));
                    information+="\nNo visible 3D geometry";
                }
                meta->setText(information);
                prefabWarnings->setWarnings(issues);
                preview->setProperty("previewReady",true);
            } else if(loaded.prefab) {
                // Decoder/provider failures must not make a valid Scene V3
                // appear corrupt: preserve a readable schematic fallback.
                preview->setImage(prefab_preview::thumbnail(
                    *loaded.prefab,640));
                preview->setProperty("previewMode","prefab-schematic");
                preview->setProperty("previewReady",true);
                meta->setText(prefab_preview::details(*loaded.prefab)+
                    "\n\n3D preview unavailable: "+loaded.error+
                    "\nSchematic composition fallback");
                prefabWarnings->setWarnings({
                    {AssetWarningKind::Other,qt(asset_reference),loaded.error}});
            } else {
                meta->setText("Invalid Scene V3 prefab");
                prefabWarnings->setWarnings({
                    {AssetWarningKind::Prefab,qt(asset_reference),loaded.error}});
                preview->setProperty("previewReady",false);
            }
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([content_root,asset_reference] {
            LoadedPrefab loaded;
            try {
                loaded.prefab=prefab_preview::load(content_root,
                                                   asset_reference);
                AssetManager assets;
                assets.mount_directory("",content_root,100);
                std::error_code ec;
                for(auto current=content_root.parent_path();!current.empty();) {
                    auto shared=current/"sharedAssets";
                    if(fs::is_directory(shared,ec)) {
                        assets.mount_shared_assets(shared);
                        break;
                    }
                    auto parent=current.parent_path();
                    if(parent==current)break;
                    current=parent;
                }
                loaded.composition=prefab_preview::load_composition(
                    *loaded.prefab,assets);
            } catch(std::exception const& e) {
                loaded.error=qt(e.what());
            }
            return loaded;
        }));
        return dialog.exec();
    }
    QDialog dialog(parent);
    dialog.setObjectName("assetPreviewDialog");
    dialog.setWindowTitle("NorthStar Asset Preview — "+qt(asset_reference));
    dialog.setModal(true);
    dialog.resize(1150,785);
    dialog.setMinimumSize(790,535);
    dialog.setStyleSheet(
        "QDialog{background:#161d27;color:#dde5ef}"
        "QLabel{color:#d2dceb}"
        "QPushButton{background:#2a3545;color:#e7ecf4;border:1px solid #46566b;"
        "border-radius:5px;padding:7px 11px}"
        "QPushButton:hover{background:#37516b}"
        "QComboBox{background:#2a3545;color:#f1f1f1;padding:5px;"
        "border:1px solid #46566b}"
        "QCheckBox{color:#cad7e4}");
    auto* root=new QVBoxLayout(&dialog);
    root->setSpacing(10);
    root->setContentsMargins(14,13,14,13);
    auto* header=new QHBoxLayout;
    auto const separator=asset_reference.rfind('@');
    auto const display_name=separator==std::string::npos?
        asset_reference:asset_reference.substr(separator+1);
    auto* name=new QLabel(qt(display_name));
    auto title=name->font();title.setPointSize(17);title.setBold(true);
    name->setFont(title);name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    header->addWidget(name,1);
    auto* copy=new QPushButton("Copy reference");
    header->addWidget(copy);
    QObject::connect(copy,&QPushButton::clicked,&dialog,[ref=qt(asset_reference)] {
        QApplication::clipboard()->setText(ref);
    });
    auto* close=new QPushButton("Close");
    header->addWidget(close);
    QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
    root->addLayout(header);
    auto* splitter=new QSplitter(Qt::Horizontal,&dialog);
    auto* canvas=new AssetPreviewCanvas(splitter);
    canvas->setObjectName("assetPreviewCanvas");
    splitter->addWidget(canvas);
    auto* sidebar=new QWidget(splitter);
    sidebar->setMinimumWidth(235);
    auto* right=new QVBoxLayout(sidebar);
    right->setContentsMargins(12,10,8,10);
    auto* warnings=new AssetWarningPanel(sidebar);
    right->addWidget(warnings);
    auto* caption=new QLabel("ASSET DETAILS");
    caption->setStyleSheet("color:#75bce3;font-weight:bold;font-size:12px");
    right->addWidget(caption);
    auto* information=new QLabel("Reading asset…");
    information->setObjectName("assetPreviewDetails");
    information->setAlignment(Qt::AlignLeft|Qt::AlignTop);
    information->setWordWrap(true);
    information->setTextInteractionFlags(Qt::TextSelectableByMouse);
    right->addWidget(information);
    right->addStretch();
    auto* tips=new QLabel("Texture: scroll to zoom, drag to pan.\n"
                           "Model/Material: drag to orbit, scroll to zoom.\n"
                          "Double-click viewport to reset.");
    tips->setWordWrap(true);
    tips->setStyleSheet("color:#8296aa;font-size:11px");
    right->addWidget(tips);
    splitter->addWidget(sidebar);
    splitter->setStretchFactor(0,4);
    splitter->setStretchFactor(1,1);
    root->addWidget(splitter,1);
    auto* toolbar=new QHBoxLayout;
    auto* fit=new QPushButton("Fit / Reset");
    toolbar->addWidget(fit);
    QObject::connect(fit,&QPushButton::clicked,canvas,[canvas]{canvas->reset();});
    auto* actual=new QPushButton("1:1");
    actual->setEnabled(false);toolbar->addWidget(actual);
    QObject::connect(actual,&QPushButton::clicked,canvas,[canvas]{canvas->oneToOne();});
    auto* mip_label=new QLabel("Mip:");
    auto* mip=new QComboBox; mip->setEnabled(false);
    toolbar->addSpacing(12);toolbar->addWidget(mip_label);toolbar->addWidget(mip);
    QObject::connect(mip,QOverload<int>::of(&QComboBox::currentIndexChanged),
                     canvas,[canvas](int i){canvas->setMip(i);});
    auto* wire=new QCheckBox("Wireframe");
    wire->setEnabled(false);
    toolbar->addSpacing(12);toolbar->addWidget(wire);
    QObject::connect(wire,&QCheckBox::toggled,canvas,
                     [canvas](bool active){canvas->setWireframe(active);});
    toolbar->addStretch();
    auto* status=new QLabel("NEF8 / Read-only");
    status->setStyleSheet("color:#8fb4ca");
    toolbar->addWidget(status);
    root->addLayout(toolbar);

    // Decode on a worker; the modal opens immediately even for a large
    // compressed ListFile. Closing it disconnects all UI callbacks safely.
    auto* watcher=new QFutureWatcher<LoadedAsset>(&dialog);
    QObject::connect(watcher,&QFutureWatcher<LoadedAsset>::finished,
                     &dialog,[watcher,canvas,information,warnings,mip,wire,actual,status,kind,ref=qt(asset_reference)] {
        LoadedAsset value;
        try {value=watcher->result();}
        catch(std::exception const& e){
            value.message=qt(e.what());
            auto const type=kind==AssetKind::texture?AssetWarningKind::Texture:
                kind==AssetKind::mesh?AssetWarningKind::Model:
                kind==AssetKind::material?AssetWarningKind::Material:
                AssetWarningKind::Other;
            value.warnings.push_back({type,ref,value.message});
        }
        warnings->setWarnings(value.warnings);
        information->setText(value.details);
        if(value.texture) {
            auto const count=value.texture->mips.size();
            mip->blockSignals(true);
            for(std::size_t i=0;i<count;++i)
                mip->addItem(QString("%1 · %2 × %3")
                    .arg(i).arg(value.texture->mips[i].width)
                    .arg(value.texture->mips[i].height));
            mip->setCurrentIndex(0);
            mip->blockSignals(false);
            mip->setEnabled(true);actual->setEnabled(true);
            canvas->setTexture(std::move(*value.texture),std::move(value.image));
            status->setText("Texture / Decoded");
        } else if(value.drawable) {
            wire->setEnabled(true);
            canvas->setDrawable(std::move(*value.drawable),
                                std::move(value.drawable_materials));
            status->setText("3D model / Orbit view");
        } else if(value.material) {
            canvas->setMaterial(std::move(*value.material),
                                std::move(value.material_textures));
            status->setText("Material / Studio sphere");
        } else if(!value.image.isNull()) {
            canvas->setImage(std::move(value.image));
            actual->setEnabled(true);
            status->setText("Image / Decoded");
        } else {
            // Diagnostic failures live only in the warning cards, not in
            // metadata plain text. Unsupported types retain their hint.
            if(value.warnings.empty()&&!value.message.isEmpty())
                information->setText(value.details+"\n\n"+value.message);
            status->setText(value.warnings.empty()?
                "Preview unavailable":"Missing dependencies / Review warnings");
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(
        [root=content_root,ref=asset_reference,kind] {
            return loadAsset(root,ref,kind);
        }));
    return dialog.exec();
}
} // namespace northstarcpp::editor
