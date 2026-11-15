// NorthStarCpp Qt prefab preview: strict Scene V3 decoding and isometric cards.
// Does not instantiate or mutate the editor scene.
#pragma once
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/scene.hpp>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QColor>
#include <QString>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace northstarcpp::editor::prefab_preview {
namespace fs=std::filesystem;

inline bool inside(fs::path const& file,fs::path const& root) {
    auto a=file.begin(),b=root.begin();
    for(;b!=root.end();++a,++b)
        if(a==file.end()||*a!=*b)return false;
    return true;
}
inline fs::path source(fs::path const& content_root,std::string const& ref) {
    auto extension=fs::path(fs::u8path(ref)).extension().u8string();
    std::transform(extension.begin(),extension.end(),extension.begin(),
        [](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(!safe_virtual_path(ref) || ref.size()>1024 ||
       extension!=".nprefab" || ref.find('@')!=std::string::npos)
        throw std::invalid_argument("Invalid project prefab reference");
    std::error_code ec;
    auto const external_directory=content_root.parent_path()/"prefabs";
    // Both regular Content/*.nprefab assets and the historical sibling
    // project/prefabs collection are first-class, with the same Scene V3
    // schema and no surprise cross-project file access.
    bool const external=ref.rfind("prefabs/",0)==0 &&
        fs::is_directory(external_directory,ec);
    auto const directory=external?external_directory:content_root;
    if(fs::is_symlink(fs::symlink_status(directory,ec)) || ec)
        throw std::runtime_error("Project prefab folder must not be a symlink");
    auto root=fs::weakly_canonical(directory,ec);
    if(ec||!fs::is_directory(root))
        throw std::runtime_error("Project prefab folder unavailable");
    auto path=(external?content_root.parent_path():content_root)/
              fs::u8path(ref);
    auto actual=fs::weakly_canonical(path,ec);
    if(ec||!inside(actual,root))
        throw std::runtime_error("Prefab path escapes project folder");
    auto cursor=directory;
    auto const local=external?ref.substr(8):ref;
    for(auto const& part:fs::u8path(local)) {
        cursor/=part;
        if(fs::is_symlink(fs::symlink_status(cursor,ec))||ec)
            throw std::runtime_error("Prefab symlink or missing segment");
    }
    if(!fs::is_regular_file(actual,ec) || ec ||
       fs::file_size(actual,ec)>4*1024*1024 || ec)
        throw std::runtime_error("Prefab source is unavailable or exceeds 4 MiB");
    return actual;
}
inline Scene load(fs::path const& content_root,std::string const& ref) {
    auto path=source(content_root,ref);
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot open prefab");
    std::string text{std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>()};
    if(text.size()>4*1024*1024 ||
       text.rfind("NorthStarCpp.Scene 3",0)!=0)
        throw std::invalid_argument("Prefab requires Scene V3 source");
    auto scene=parse_scene_v3(text);
    validate_scene(scene);
    if(scene.version!=3||scene.objects.empty()||scene.objects.size()>16384)
        throw std::invalid_argument("Prefab must contain 1..16384 Scene V3 actors");
    return scene;
}
inline QString details(Scene const& scene) {
    std::size_t roots=0,meshes=0,colliders=0,children=0;
    for(auto const& obj:scene.objects) {
        if(!obj.node.parent)++roots;else ++children;
        if(!obj.node.asset.empty())++meshes;
        if(obj.node.collision)++colliders;
    }
    return QString("Prefab / Scene V3\nActors: %1\nRoot actors: %2\nChild actors: %3\n"
                   "Mesh references: %4\nColliders: %5\nComponents: %6\n"
                   "Layers: %7\nStreaming cells: %8\nDrag into Viewport to instantiate")
        .arg(scene.objects.size()).arg(roots).arg(children).arg(meshes)
        .arg(colliders).arg(scene.components.size()).arg(scene.layers.size())
        .arg(scene.cells.size())+
        [&scene] {
            QString names;
            std::size_t count=0;
            for(auto const& object:scene.objects) {
                if(count++>=12) {
                    names+="\n... additional actors";
                    break;
                }
                names+="\n"+QString(object.node.parent?"  - ":"- ")+
                    QString::fromUtf8(object.name.c_str());
                if(!object.node.asset.empty())
                    names+=" ["+QString::fromUtf8(object.node.asset.c_str())+"]";
            }
            return "\n\nHierarchy:"+names;
        }();
}
inline QImage thumbnail(Scene const& scene,int dimension=320) {
    dimension=std::clamp(dimension,64,1024);
    QImage result(dimension,dimension,QImage::Format_ARGB32);
    result.fill(QColor("#151e2a"));
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#304657"),1));
    for(int i=1;i<8;++i) {
        auto y=double(dimension)*i/8.;
        painter.drawLine(QPointF(0,y),QPointF(dimension,y));
    }
    float minx=0,maxx=0,minz=0,maxz=0;
    bool first=true;
    for(auto const& a:scene.objects) {
        if(!a.node.visible)continue;
        float x=a.position.x,z=a.position.z;
        if(first){minx=maxx=x;minz=maxz=z;first=false;}
        else {minx=std::min(minx,x);maxx=std::max(maxx,x);
              minz=std::min(minz,z);maxz=std::max(maxz,z);}
    }
    float span=std::max({maxx-minx,maxz-minz,1.f});
    float scale=dimension*.55f/span;
    auto project=[&](Vector3 p) {
        return QPointF(dimension*.5f+(p.x-(minx+maxx)*.5f-
                       (p.z-(minz+maxz)*.5f))*.72f*scale,
                       dimension*.58f+(p.x-(minx+maxx)*.5f+
                       p.z-(minz+maxz)*.5f)*.36f*scale-
                       p.y*.4f*scale);
    };
    int painted=0;
    for(auto const& object:scene.objects) {
        if(!object.node.visible||painted>=1024)continue;
        auto center=project(object.position);
        float const radius=std::clamp(object.size*scale*.3f,3.f,
                                      dimension*.13f);
        QColor color=object.node.asset.empty()?
            QColor("#8eacbd"):QColor("#64b8e9");
        QColor upper=color.lighter(135),side=color.darker(155);
        QPolygonF top{center+QPointF(0,-radius),
                     center+QPointF(radius,-radius*.45),
                     center+QPointF(0,radius*.1),
                     center+QPointF(-radius,-radius*.45)};
        QPolygonF left{top[3],top[2],top[2]+QPointF(0,radius),
                       top[3]+QPointF(0,radius)};
        QPolygonF right{top[2],top[1],top[1]+QPointF(0,radius),
                        top[2]+QPointF(0,radius)};
        painter.setPen(QPen(QColor("#122431"),1));
        painter.setBrush(upper);painter.drawPolygon(top);
        painter.setBrush(color);painter.drawPolygon(left);
        painter.setBrush(side);painter.drawPolygon(right);
        ++painted;
    }
    painter.setPen(QColor("#a2c7db"));
    painter.drawText(QRectF(8,dimension-28,dimension-16,20),
        Qt::AlignRight,QString("%1 actors").arg(scene.objects.size()));
    return result;
}
} // namespace northstarcpp::editor::prefab_preview
