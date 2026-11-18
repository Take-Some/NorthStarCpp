// Author a project-owned thumbnail from real native GPU viewport pixels.
// No generated graphics, unrelated stock image, or editor UI chrome.
#include "qt_editor_window.hpp"
#include "qt_editor_viewport.hpp"
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <QWidget>
#include <filesystem>

namespace northstarcpp::editor {
bool QtEditorWindow::captureProjectThumbnail(QString* diagnostic) {
    auto fail=[&](QString const& message){
        if(diagnostic)*diagnostic=message;
        return false;
    };
    if(file_.empty()||!viewport_)
        return fail("Open and render a project scene before capturing its thumbnail.");
    if(!viewport_->gpuActive())
        return fail("The native GPU scene renderer is not active.");
    auto project=file_.parent_path();
    std::filesystem::path root;
    for(int depth=0;depth<7&&!project.empty();depth++){
        std::error_code ec;
        if(std::filesystem::is_regular_file(
               project/"northstarcpp.project.json",ec)) {
            root=project;break;
        }
        if(project==project.parent_path())break;
        project=project.parent_path();
    }
    if(root.empty())
        return fail("This scene does not belong to a NorthStarCpp project.");
    if(!isVisible() || !viewport_->isVisible())
        return fail("The editor scene viewport must be visible to capture GPU pixels.");
    auto shot=viewport_->captureRenderedFrame();
    if(shot.isNull())
        return fail("GPU framebuffer capture failed; project thumbnail was not modified.");
    auto image=shot.toImage();
    if(image.width()<64 || image.height()<64)
        return fail("The GPU frame is too small to use as a project thumbnail.");
    QSet<quint32> colorBuckets;
    for(int y=0;y<image.height();y+=std::max(1,image.height()/22))
        for(int x=0;x<image.width();x+=std::max(1,image.width()/32)){
            auto const pixel=image.pixel(x,y);
            colorBuckets.insert(((qRed(pixel)>>4)<<8)|
                                ((qGreen(pixel)>>4)<<4)|(qBlue(pixel)>>4));
        }
    if(colorBuckets.size()<10)
        return fail("Screen capture returned a blank/uniform frame, not the rendered project scene.");
    // Keep this a genuine screenshot; only crop and resize the pixels.
    const auto scaled=image.scaled({1280,720},Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
    const auto normalized=scaled.copy(
        (scaled.width()-1280)/2,(scaled.height()-720)/2,1280,720);
    if(normalized.isNull())
        return fail("GPU capture could not be normalized.");
    const auto file=QString::fromStdWString(
        (root/"project_thumbnail.png").wstring());
    if(QFileInfo(file).isSymLink())
        return fail("Refusing to write to a symlinked thumbnail file.");
    QSaveFile output(file);
    if(!output.open(QIODevice::WriteOnly))
        return fail("Cannot open project thumbnail for writing: "+output.errorString());
    if(!normalized.save(&output,"PNG")){
        output.cancelWriting();
        return fail("Cannot encode the captured GPU frame as PNG.");
    }
    if(!output.commit())return fail("Cannot commit project thumbnail: "+output.errorString());
    if(diagnostic)*diagnostic="Captured actual scene viewport: "+file;
    return true;
}
} // namespace northstarcpp::editor
