// NorthStar Qt Material Function editor: typed signature inspector, local
// source editing, bounded validation and atomic save. The compiler remains the
// sole authority for graph validity and recursion safety.
#include "qt_material_function_editor.hpp"
#include "qt_material_authoring.hpp"
#include <northstarcpp/material.hpp>
#include <QDialog>
#include <QFontDatabase>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <stdexcept>

namespace northstarcpp::editor {
namespace {
QJsonObject parse(QString const& text) {
    auto bytes=text.toUtf8();
    if(bytes.size()<2||bytes.size()>32768)
        throw std::invalid_argument("Material Function must be 2..32768 bytes");
    QJsonParseError error{};
    auto doc=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError||!doc.isObject())
        throw std::invalid_argument("Invalid Material Function JSON");
    auto descriptor=doc.object();
    if(descriptor.value("schema").toString()!="northstar.material-function.v1")
        throw std::invalid_argument("Unsupported Material Function schema");
    return descriptor;
}
QJsonObject constant(QString const& id,QString const& type) {
    QJsonObject result{{"id",id}};
    if(type=="scalar") {
        result["op"]="scalar";result["value"]=0.5;
    }else if(type=="vector2") {
        result["op"]="vector2";result["value"]=QJsonArray{0.5,0.5};
    }else if(type=="vector3") {
        result["op"]="vector3";result["value"]=QJsonArray{0.5,0.5,0.5};
    }else if(type=="vector4") {
        result["op"]="color";result["value"]=QJsonArray{0.5,0.5,0.5,1.0};
    }else throw std::invalid_argument("Unsupported Material Function pin type: "+
                                      type.toStdString());
    return result;
}
void validate(QString const& text,std::filesystem::path const& root,
              std::string const& reference) {
    auto function=parse(text);
    auto inputPins=function.value("inputs").toArray();
    auto outputs=function.value("outputs").toArray();
    if(outputs.isEmpty()||outputs.size()>8||inputPins.size()>8)
        throw std::invalid_argument("Material Function requires 1..8 outputs and up to 8 inputs");
    for(auto const& output:outputs) {
        auto target=output.toObject().value("name").toString();
        QJsonArray nodes;
        QJsonObject binding;
        for(int i=0;i<inputPins.size();++i) {
            auto pin=inputPins.at(i).toObject();
            auto id="probe_"+QString::number(i);
            nodes.append(constant(id,pin.value("type").toString()));
            binding[pin.value("name").toString()]=id;
        }
        nodes.append(QJsonObject{{"id","probe_call"},{"op","function"},
            {"path",QString::fromStdString(reference)},
            {"output",target},{"inputs",binding}});
        QJsonObject graph{
            {"nodes",nodes},{"outputs",QJsonObject{{"base_color","probe_call"}}}};
        auto data=QJsonDocument(graph).toJson(QJsonDocument::Compact);
        auto resolve=[&](std::string const& ref)->std::string {
            if(ref==reference)return text.toUtf8().toStdString();
            return material_authoring::material_function_source(root,ref);
        };
        (void)northstarcpp::compile_material_graph_with_functions(
            data.toStdString(),resolve);
    }
}
void describe(QTreeWidget* tree,QJsonObject const& descriptor) {
    tree->clear();
    auto* inputs=new QTreeWidgetItem(tree,{"Inputs","Typed"});
    for(auto const& pin:descriptor.value("inputs").toArray()) {
        auto entry=pin.toObject();
        new QTreeWidgetItem(inputs,{
            entry.value("name").toString(),entry.value("type").toString()});
    }
    auto* outputs=new QTreeWidgetItem(tree,{"Outputs","Typed"});
    for(auto const& pin:descriptor.value("outputs").toArray()) {
        auto entry=pin.toObject();
        new QTreeWidgetItem(outputs,{
            entry.value("name").toString(),entry.value("type").toString()});
    }
    auto* nodes=new QTreeWidgetItem(tree,{"Nodes","Graph"});
    for(auto const& node:descriptor.value("nodes").toArray()) {
        auto entry=node.toObject();
        new QTreeWidgetItem(nodes,{
            entry.value("id").toString(),entry.value("op").toString()});
    }
    tree->expandAll();
}
} // namespace
int openMaterialFunctionEditor(
    QWidget* parent,std::filesystem::path const& root,
    std::string const& reference) {
    QDialog dialog(parent);
    dialog.setObjectName("materialFunctionEditorDialog");
    dialog.setWindowTitle("Material Function Editor — "+
        QString::fromStdString(reference));
    dialog.resize(1010,690);
    auto* layout=new QVBoxLayout(&dialog);
    auto* intro=new QLabel(
        "Reusable typed function • Input/output signatures and node DAG. "
        "Validate before saving; source is committed atomically.",&dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* splitter=new QSplitter(Qt::Horizontal,&dialog);
    auto* tree=new QTreeWidget(splitter);
    tree->setObjectName("materialFunctionSignature");
    tree->setHeaderLabels({"Pin / Node","Type / Operation"});
    tree->setMinimumWidth(290);
    auto* source=new QTextEdit(splitter);
    source->setObjectName("materialFunctionSource");
    source->setAcceptRichText(false);
    source->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    source->setLineWrapMode(QTextEdit::NoWrap);
    splitter->addWidget(tree);
    splitter->addWidget(source);
    splitter->setStretchFactor(0,1);
    splitter->setStretchFactor(1,3);
    layout->addWidget(splitter,1);
    auto* message=new QLabel(&dialog);
    message->setObjectName("materialFunctionStatus");
    message->setWordWrap(true);
    layout->addWidget(message);
    auto* actions=new QHBoxLayout;
    auto* validateButton=new QPushButton("Validate Function",&dialog);
    auto* save=new QPushButton("Validate && Save",&dialog);
    auto* close=new QPushButton("Close",&dialog);
    actions->addWidget(validateButton);
    actions->addWidget(save);
    actions->addStretch();
    actions->addWidget(close);
    layout->addLayout(actions);
    QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
    try {
        auto original=material_authoring::material_function_source(root,reference);
        auto doc=QJsonDocument::fromJson(QByteArray::fromStdString(original));
        if(!doc.isObject())throw std::runtime_error("Malformed Material Function");
        source->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
        describe(tree,doc.object());
    }catch(std::exception const& e) {
        source->setReadOnly(true);
        save->setEnabled(false);
        validateButton->setEnabled(false);
        message->setText("Cannot open function: "+QString::fromUtf8(e.what()));
    }
    auto check=[&]() -> bool {
        try {
            auto text=source->toPlainText();
            validate(text,root,reference);
            describe(tree,parse(text));
            message->setStyleSheet("color:#8bcba9;");
            message->setText("Function signatures and dependency graph validated.");
            return true;
        }catch(std::exception const& e) {
            message->setStyleSheet("color:#e3a1a1;");
            message->setText("Validation failed: "+QString::fromUtf8(e.what()));
            return false;
        }
    };
    QObject::connect(validateButton,&QPushButton::clicked,&dialog,
                     [&] { (void)check(); });
    QObject::connect(save,&QPushButton::clicked,&dialog,[&] {
        if(!check())return;
        try {
            auto target=material_authoring::resolve_existing(
                root,QString::fromStdString(reference));
            QSaveFile file(QString::fromStdWString(target.wstring()));
            if(!file.open(QIODevice::WriteOnly))
                throw std::runtime_error("Cannot open atomic Material Function save");
            auto bytes=QJsonDocument(parse(source->toPlainText()))
                .toJson(QJsonDocument::Indented);
            if(file.write(bytes)!=bytes.size()||!file.commit())
                throw std::runtime_error("Cannot commit Material Function");
            message->setText("Saved Material Function: "+
                QString::fromStdString(reference));
        }catch(std::exception const& e) {
            message->setStyleSheet("color:#e3a1a1;");
            message->setText("Save failed: "+QString::fromUtf8(e.what()));
        }
    });
    return dialog.exec();
}
} // namespace northstarcpp::editor
