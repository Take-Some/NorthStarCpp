// Qt Windows DPI & 50k-line editor acceptance. No project source mutation.
#include "qt_text_asset_editor.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QWidget>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    using northstarcpp::editor::text_assets::Format;
    using northstarcpp::editor::text_assets::createSourceEditor;
    int failures=0;
    auto check=[&](bool result,const char* message){
        if(!result){std::cerr<<"FAIL: "<<message<<"\n";++failures;}
    };
    QWidget host;
    host.resize(940,680);
    auto* editor=createSourceEditor(&host,Format::JavaScript,false);
    editor->setGeometry(12,12,890,610);
    host.show();
    editor->show();
    editor->setFocus();
    QCoreApplication::processEvents();
    constexpr int lines=50000;
    QString source;
    source.reserve(lines*40);
    for(int i=1;i<=lines;++i)
        source+=QStringLiteral("const line_%1 = %2; // NorthStar\n").arg(i).arg(i);
    QElapsedTimer load;
    load.start();
    editor->setPlainText(source);
    QCoreApplication::processEvents();
    const auto loadMs=load.elapsed();
    auto* gutter=editor->findChild<QWidget*>("northstarLineNumbers");
    auto* bar=editor->verticalScrollBar();
    check(editor->blockCount()>=lines,"50k lines loaded");
    check(gutter && gutter->width()>30,"five-digit gutter");
    check(bar && bar->maximum()>lines/2,"scrollbar range");
    check(editor->document()->findChildren<QSyntaxHighlighter*>().size()==1,
          "exactly one syntax highlighter");
    check(editor->property("northstarLineRuling").toBool(),"row separators");
    auto snapshot=[&](QString const& phase) {
        auto const image=editor->grab().toImage();
        check(!image.isNull(),"rendered screenshot");
        if(image.isNull())return;
        // Verify the actual Windows raster output at each DPI, not just that
        // the gutter and row-ruling flags exist as QObject properties.
        const auto scale=editor->devicePixelRatioF();
        check(image.width()>=int(editor->width()*scale)-2,
              "screenshot retains device-pixel resolution");
        auto const viewportImage=editor->viewport()->grab().toImage();
        auto const gutterImage=gutter?gutter->grab().toImage():QImage{};
        bool guide=false,number=false;
        if(!viewportImage.isNull()) {
            const int x=std::max(0,viewportImage.width()-int(12*scale));
            for(int y=0;y<viewportImage.height();++y) {
                if(viewportImage.pixelColor(x,y)==QColor("#273543")) {
                    guide=true;break;
                }
            }
        }
        if(!gutterImage.isNull()) {
            const QColor background("#17202b");
            for(int y=0;y<gutterImage.height() && !number;++y)
                for(int x=0;x<gutterImage.width();++x)
                    if(gutterImage.pixelColor(x,y)!=background) {
                        number=true;break;
                    }
        }
        check(guide,"visible horizontal line pixels");
        check(number,"rendered line numbers in gutter");
        if(argc>1) {
            auto dir=QString::fromLocal8Bit(argv[1]);
            QDir().mkpath(dir);
            auto scale=QString::fromUtf8(qgetenv("QT_SCALE_FACTOR"));
            if(scale.isEmpty())scale="native";
            check(image.save(QDir(dir).filePath(
                QString("code-editor-%1-%2.png").arg(scale,phase))),"snapshot saved");
        }
    };
    snapshot("top");
    if(bar) {
        bar->setValue(bar->maximum()/2);
        QCoreApplication::processEvents();
        const auto before=bar->value();
        QWheelEvent wheel(QPointF(250,200),QPointF(250,200),QPoint(0,0),
            QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(editor->viewport(),&wheel);
        QCoreApplication::processEvents();
        check(bar->value()>before,"wheel advances scrollbar");
    }
    QKeyEvent end(QEvent::KeyPress,Qt::Key_End,Qt::ControlModifier);
    QCoreApplication::sendEvent(editor,&end);
    QCoreApplication::processEvents();
    check(editor->textCursor().blockNumber()>=lines-1,"Ctrl+End reaches last line");
    check(editor->cursorForPosition(QPoint(8,8)).blockNumber()>=lines-100,
          "visible block follows last line");
    snapshot("bottom");
    const auto width=gutter?gutter->width():0;
    auto f=editor->font();
    f.setPointSize(std::max(17,f.pointSize()+6));
    editor->setFont(f);
    QCoreApplication::processEvents();
    check(gutter && gutter->width()>width,"font DPI change updates gutter width");
    snapshot("large-font");
    std::array<double,128> ms{};
    if(bar)for(std::size_t i=0;i<ms.size();++i) {
        QElapsedTimer t;t.start();
        bar->setValue(int(double(bar->maximum())*i/(ms.size()-1)));
        QCoreApplication::processEvents();
        check(!editor->viewport()->grab().isNull(),"viewport paints scrolling");
        ms[i]=double(t.nsecsElapsed())/1.e6;
    }
    std::sort(ms.begin(),ms.end());
    auto quantile=[&](double p) {
        return ms[std::min(ms.size()-1,std::size_t(std::ceil((ms.size()-1)*p)))];
    };
    QKeyEvent home(QEvent::KeyPress,Qt::Key_Home,Qt::ControlModifier);
    QCoreApplication::sendEvent(editor,&home);
    QCoreApplication::processEvents();
    check(editor->textCursor().blockNumber()==0,"Ctrl+Home reaches first line");
    check(bar && bar->value()==0,"scroll returns to start");
    snapshot("home");
    auto* screen=QGuiApplication::primaryScreen();
    std::cout<<"Code Editor Windows DPI + scroll: "<<(failures?"FAIL":"PASS")
        <<" dpi="<<(screen?screen->logicalDotsPerInch():0)
        <<" dpr="<<editor->devicePixelRatioF()
        <<" lines="<<editor->blockCount()<<" load_ms="<<loadMs
        <<" p50="<<quantile(.5)<<" p95="<<quantile(.95)
        <<" p99="<<quantile(.99)<<" samples="<<ms.size()
        <<" failures="<<failures<<"\n";
    return failures?1:0;
}
