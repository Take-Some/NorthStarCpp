// NorthStarCpp Project Browser: project-owned REAL scene screenshot only.
// Qt-only, cached; never loads arbitrary remote files or external image paths.
#pragma once
#include <northstarcpp/project_catalog.hpp>
#include <QColor>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>
#include <QRegularExpression>
#include <QString>
#include <QWidget>
#include <QPaintEvent>
#include <array>
#include <cmath>
#include <filesystem>
#include <vector>

namespace northstarcpp::project_browser {
namespace fs=std::filesystem;
struct Cover {
    QPixmap image;
    bool screenshot=false;
    QString source;
    QPixmap icon;
    QString iconSource;
};

inline QString projectCoverPath(fs::path const& project) {
    // Predictable, local-only filenames; never accept untrusted manifest paths.
    constexpr std::array<char const*,3> candidates{{
        "project_cover.png","project_thumbnail.png",
        "Saved/Screenshots/ProjectPreview.png"
    }};
    const auto root=QString::fromStdWString(project.wstring());
    const QDir dir(root);
    auto rootInfo=QFileInfo(root);
    const QString canonicalRoot=QDir::cleanPath(QDir::fromNativeSeparators(rootInfo.canonicalFilePath()));
    if(canonicalRoot.isEmpty())return {};
    for(auto const* relative:candidates){
        const auto candidate=dir.absoluteFilePath(QString::fromLatin1(relative));
        QFileInfo file(candidate);
        if(!file.isFile()||file.isSymLink()||file.size()<=0||
           file.size()>24*1024*1024)continue;
        auto const canonical=QDir::cleanPath(QDir::fromNativeSeparators(file.canonicalFilePath()));
        if(canonical.isEmpty()||
           !canonical.startsWith(canonicalRoot+"/",
                                 Qt::CaseInsensitive))continue;
        return canonical;
    }
    return {};
}
inline QString projectIconPath(fs::path const& project) {
    auto const root=QString::fromStdWString(project.wstring());
    QFileInfo const base(root), icon(QDir(root).filePath("project_icon.png"));
    if(!icon.isFile() || icon.isSymLink() || icon.size()<1 ||
       icon.size()>24*1024*1024)return {};
    auto const canonicalRoot=QDir::cleanPath(
        QDir::fromNativeSeparators(base.canonicalFilePath()));
    auto const canonical=QDir::cleanPath(
        QDir::fromNativeSeparators(icon.canonicalFilePath()));
    if(canonicalRoot.isEmpty() ||
       !canonical.startsWith(canonicalRoot+"/",Qt::CaseInsensitive))
        return {};
    return canonical;
}
inline QPixmap loadProjectIcon(fs::path const& project) {
    auto const file=projectIconPath(project);
    if(file.isEmpty())return {};
    QFileInfo const info(file);
    auto const key="NorthStarProjectIcon:"+file+":"+
        QString::number(info.size())+":"+
        QString::number(info.lastModified().toMSecsSinceEpoch());
    QPixmap cached;
    if(QPixmapCache::find(key,&cached))return cached;
    QImageReader reader(file);
    reader.setAutoTransform(true);
    auto const size=reader.size();
    if(!size.isValid() || size.width()>8192 || size.height()>8192 ||
       qint64(size.width())*size.height()>32000000)return {};
    reader.setScaledSize(size.scaled({256,256},Qt::KeepAspectRatioByExpanding));
    auto image=reader.read();
    if(image.isNull())return {};
    auto icon=QPixmap::fromImage(std::move(image));
    QPixmapCache::insert(key,icon);
    return icon;
}
inline QPixmap cropped(QPixmap const& input,QSize size) {
    auto result=QPixmap(size);
    result.fill(QColor("#0c111a"));
    if(input.isNull())return result;
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto scaled=input.size().scaled(size,Qt::KeepAspectRatioByExpanding);
    const QRect dest((size.width()-scaled.width())/2,
                     (size.height()-scaled.height())/2,
                     scaled.width(),scaled.height());
    p.drawPixmap(dest,input);
    return result;
}
inline Cover coverForProject(projects::Project const& project) {
    const auto file=projectCoverPath(project.directory);
    QString key;
    if(!file.isEmpty()) {
        QFileInfo info(file);
        key="NorthStarCover:"+file+":"+QString::number(info.size())+":"+
            QString::number(info.lastModified().toMSecsSinceEpoch());
        QPixmap pix;
        if(QPixmapCache::find(key,&pix))return
            {pix,true,file,loadProjectIcon(project.directory),projectIconPath(project.directory)};
        QImageReader reader(file);
        reader.setAutoTransform(true);
        const auto dimensions=reader.size();
        if(dimensions.isValid() && dimensions.width()<=8192 &&
           dimensions.height()<=8192 &&
           qint64(dimensions.width())*dimensions.height()<=32000000) {
            reader.setScaledSize(dimensions.scaled(QSize(960,540),
                                 Qt::KeepAspectRatioByExpanding));
            auto image=reader.read();
            if(!image.isNull()) {
                pix=cropped(QPixmap::fromImage(std::move(image)),{640,360});
                QPixmapCache::insert(key,pix);
                return {pix,true,file,loadProjectIcon(project.directory),projectIconPath(project.directory)};
            }
        }
    }
    // Never fabricate a scene image. Missing screenshots stay explicitly empty.
    auto icon=loadProjectIcon(project.directory);
    if(!icon.isNull())return {cropped(icon,{640,360}),true,
                              projectIconPath(project.directory),icon,projectIconPath(project.directory)};
    return {QPixmap{},false,{}};
}
class CoverWidget final:public QWidget {
public:
    explicit CoverWidget(projects::Project const& project,QWidget* parent=nullptr)
        :QWidget(parent),cover_(coverForProject(project)),
         category_(QString::fromStdWString(project.category)) {
        setObjectName("cardCover");
        setFixedHeight(160);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        setToolTip(cover_.screenshot?
            QString("Project image: %1").arg(cover_.source):
            QStringLiteral("Choose a project icon, or capture a scene frame."));
        setAttribute(Qt::WA_TransparentForMouseEvents,true);
    }
    bool hasScreenshot() const {return cover_.screenshot;}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;clip.addRoundedRect(rect(),8,8);
        painter.setClipPath(clip);
        if(cover_.screenshot && !cover_.image.isNull()) {
            painter.drawPixmap(rect(),cover_.image);
        }else{
            painter.fillRect(rect(),QColor("#101823"));
            painter.setPen(QPen(QColor("#45566b"),2));
            const auto cx=width()/2,cy=height()/2-12;
            painter.drawRoundedRect(QRect(cx-25,cy-20,50,34),5,5);
            painter.drawEllipse(QRect(cx-6,cy-13,12,12));
            painter.drawLine(cx-17,cy+7,cx-4,cy-2);
            painter.drawLine(cx-4,cy-2,cx+16,cy+12);
            painter.setPen(QColor("#8e9bb0"));
            painter.drawText(rect().adjusted(10,0,-10,12),
                             Qt::AlignHCenter|Qt::AlignVCenter,
                             "Choose project image");
        }
        QLinearGradient gradient(0,height()*0.56,0,height());
        gradient.setColorAt(0,QColor(0,0,0,0));
        gradient.setColorAt(1,QColor(4,10,17,185));
        painter.fillRect(rect(),gradient);
        if(!cover_.icon.isNull()) {
            auto const iconRect=QRect(12,12,53,53);
            painter.fillRect(iconRect.adjusted(-3,-3,3,3),QColor(9,17,28,210));
            QPainterPath iconClip;iconClip.addRoundedRect(iconRect,6,6);
            painter.save();
            painter.setClipPath(iconClip,Qt::IntersectClip);
            painter.drawPixmap(iconRect,cropped(cover_.icon,iconRect.size()));
            painter.restore();
        }
        painter.setPen(QColor("#e6edf7"));
        QFont f=font();
        f.setPointSize(9);f.setWeight(QFont::DemiBold);painter.setFont(f);
        painter.drawText(rect().adjusted(13,0,-12,-11),
                         Qt::AlignBottom|Qt::AlignLeft,
                         cover_.screenshot?
                              (cover_.source.endsWith("project_icon.png")?
                                  "PROJECT ICON":"PROJECT IMAGE"):
                              "NO PROJECT IMAGE");
        painter.setPen(QColor("#b9c5d1"));
        painter.drawText(rect().adjusted(12,0,-12,-11),
                         Qt::AlignBottom|Qt::AlignRight,
                         painter.fontMetrics().elidedText(category_.toUpper(),
                             Qt::ElideRight,width()/2-14));
    }
private:
    Cover cover_;
    QString category_;
};
} // namespace northstarcpp::project_browser
