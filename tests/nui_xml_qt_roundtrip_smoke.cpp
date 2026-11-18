#include "../engine/northstarcpp/modules/editor/qt_nui_xml_codec.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        if(argc<2)throw std::runtime_error("Supply at least one .nui XML file");
        for(int i=1;i<argc;++i){
            QFile input(QString::fromLocal8Bit(argv[i]));
            if(!input.open(QIODevice::ReadOnly))
                throw std::runtime_error("Cannot read .nui XML file");
            auto source=input.readAll();
            auto parsed=northstarcpp::editor::nui_qt::parse(source);
            auto encoded=northstarcpp::editor::nui_qt::serialize(parsed);
            auto restored=northstarcpp::editor::nui_qt::parse(encoded);
            if(restored!=parsed)
                throw std::runtime_error("Qt XML authoring roundtrip changed UI document");
        }
        if(!northstarcpp::editor::nui_qt::typedEdit("string","true").isString() ||
           northstarcpp::editor::nui_qt::typedEdit("string","0012").toString()!="0012")
            throw std::runtime_error("UI editor erased explicit string types");
        std::cout<<"NUI_XML_QT_ROUNDTRIP_PASS "<<argc-1<<" files\n";
    }catch(std::exception const& e){
        std::cerr<<"NUI_XML_QT_ROUNDTRIP_FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
