// Focused Qt regression: one source editor surface for all text documents.
#include "qt_text_asset_editor.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QWidget>
#include <array>
#include <iostream>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    using northstarcpp::editor::text_assets::Format;
    using northstarcpp::editor::text_assets::createSourceEditor;
    struct Case {Format format;char const* source;bool colored;};
    constexpr std::array<Case,7> cases{{
        {Format::JavaScript,"const answer = 42; // tested\nreturn answer;",true},
        {Format::TypeScript,"export class Example {\n value: number = 1; }",true},
        {Format::VertexShader,"uniform vec3 light;\nvoid main() {}",true},
        {Format::FragmentShader,"out vec4 color;\nvoid main() {}",true},
        {Format::Json,"{\"name\": 17,\n \"active\": true}",true},
        {Format::Markdown,"# Title\n\n**emphasis**",true},
        {Format::Plain,"Plain row one\nPlain row two",false}
    }};
    bool okay=true;
    QWidget host;
    host.resize(800,300);
    host.show();
    for(auto const& testcase:cases) {
        auto* editor=createSourceEditor(&host,testcase.format,true);
        editor->resize(600,150);
        editor->show();
        editor->setPlainText(QString::fromUtf8(testcase.source));
        QCoreApplication::processEvents();
        auto* numbers=editor->findChild<QWidget*>("northstarLineNumbers");
        auto const colored=editor->document()->firstBlock().layout()->formats().size()>0;
        bool const active=editor->property("northstarLineRuling").toBool();
        bool const highlighted=
            !editor->document()->findChildren<QSyntaxHighlighter*>().isEmpty();
        auto const image=editor->viewport()->grab().toImage();
        bool guidePixel=false;
        if(!image.isNull()) {
            auto const x=std::max(1,image.width()-12);
            for(int y=0;y<std::min(90,image.height());++y)
                if(image.pixelColor(x,y)==QColor("#273543")) {
                    guidePixel=true;
                    break;
                }
        }
        if(!numbers || numbers->width()<=0 || !active || !highlighted ||
           editor->blockCount()<2 || colored!=testcase.colored || !guidePixel) {
            okay=false;
            std::cerr<<"Source editor check failed: kind="<<int(testcase.format)
                     <<" gutter="<<(numbers!=nullptr)<<" highlighted="<<highlighted
                     <<" colored="<<colored<<" ruling="<<active<<" guide_pixel="<<guidePixel<<"\n";
        }
        delete editor;
    }
    std::cout<<"NorthStar Qt code editor highlight/line numbers/ruling: "
             <<(okay?"PASS":"FAIL")<<"\n";
    return okay?0:1;
}
