// NorthStar Qt Material Function editor: typed signature inspector, local
// source editing, bounded validation and atomic save. The compiler remains the
// sole authority for graph validity and recursion safety.
#include "qt_material_function_editor.hpp"
#include "qt_material_authoring.hpp"
#include "qt_material_graph_editor.hpp"
#include <northstarcpp/material.hpp>
#include <QDialog>
#include <QTabWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QFontDatabase>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QJsonParseError>
#include <QSplitter>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <stdexcept>
#include <algorithm>

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
void validateMaterialFunctionDocument(
    QJsonObject const& function,std::filesystem::path const& root,
    std::string const& reference) {
    validate(QString::fromUtf8(
        QJsonDocument(function).toJson(QJsonDocument::Compact)),root,reference);
}
int openMaterialFunctionEditor(
    QWidget* parent,std::filesystem::path const& root,
    std::string const& reference) {
    QDialog dialog(parent);
    dialog.setObjectName("materialFunctionEditorDialog");
    dialog.setWindowTitle("Material Function Editor — "+
        QString::fromStdString(reference));
    dialog.resize(1230,790);
    auto* layout=new QVBoxLayout(&dialog);
    auto* intro=new QLabel(
        "Reusable typed function  |  Visual Graph / JSON Source  |  "
        "Edits are validated before atomic save.",&dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* controls=new QHBoxLayout;
    auto* addInput=new QPushButton("Add Input",&dialog);
    auto* addOutput=new QPushButton("Add Output",&dialog);
    auto* removeInput=new QPushButton("Remove Input",&dialog);
    auto* removeOutput=new QPushButton("Remove Output",&dialog);
    for(auto* control:{addInput,addOutput,removeInput,removeOutput})
        controls->addWidget(control);
    controls->addStretch();
    layout->addLayout(controls);

    auto* splitter=new QSplitter(Qt::Horizontal,&dialog);
    auto* tree=new QTreeWidget(splitter);
    tree->setObjectName("materialFunctionSignature");
    tree->setHeaderLabels({"Pin / Node","Type / Operation"});
    tree->setMinimumWidth(230);
    auto* tabs=new QTabWidget(splitter);
    tabs->setObjectName("materialFunctionEditorTabs");
    auto* graph=new MaterialGraphEditor({},tabs,root);
    graph->setObjectName("materialFunctionVisualGraph");
    auto* source=new QTextEdit(tabs);
    source->setObjectName("materialFunctionSource");
    source->setAcceptRichText(false);
    source->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    source->setLineWrapMode(QTextEdit::NoWrap);
    tabs->addTab(graph,"Visual Graph");
    tabs->addTab(source,"JSON Source");
    splitter->addWidget(tree);
    splitter->addWidget(tabs);
    splitter->setStretchFactor(0,1);
    splitter->setStretchFactor(1,4);
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

    bool loaded=false;
    bool dirty=false;
    bool synchronizing=false;
    int previousTab=0;
    auto setError=[&](QString const& value) {
        message->setStyleSheet("color:#e3a1a1;");
        message->setText(value);
    };
    auto markDirty=[&] {
        if(!loaded || synchronizing)return;
        dirty=true;
        dialog.setWindowModified(true);
    };
    auto activeDocument=[&]() -> QJsonObject {
        return tabs->currentWidget()==source?
            parse(source->toPlainText()):graph->functionDocument();
    };
    try {
        auto original=material_authoring::material_function_source(root,reference);
        auto doc=QJsonDocument::fromJson(QByteArray::fromStdString(original));
        if(!doc.isObject())throw std::runtime_error("Malformed Material Function");
        synchronizing=true;
        source->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
        graph->setFunctionDocument(doc.object(),reference);
        describe(tree,doc.object());
        synchronizing=false;
        loaded=true;
        message->setText("Visual graph loaded. Connect nodes to typed function output pins.");
    }catch(std::exception const& e) {
        synchronizing=false;
        source->setReadOnly(true);
        tabs->setEnabled(false);
        for(auto* item:{save,validateButton,addInput,addOutput,removeInput,removeOutput})
            item->setEnabled(false);
        setError("Cannot open Material Function: "+QString::fromUtf8(e.what()));
    }
    QObject::connect(source,&QTextEdit::textChanged,&dialog,markDirty);
    QObject::connect(graph,&MaterialGraphEditor::graphChanged,&dialog,[&] {
        if(!synchronizing && loaded) {
            describe(tree,graph->functionDocument());
            markDirty();
        }
    });
    QObject::connect(tabs,&QTabWidget::currentChanged,&dialog,[&](int next) {
        if(!loaded || synchronizing)return;
        if(next==previousTab)return;
        try {
            synchronizing=true;
            if(next==0) {
                graph->setFunctionDocument(parse(source->toPlainText()),reference);
                describe(tree,graph->functionDocument());
            }else {
                auto const doc=graph->functionDocument();
                source->setPlainText(QString::fromUtf8(
                    QJsonDocument(doc).toJson(QJsonDocument::Indented)));
                describe(tree,doc);
            }
            previousTab=next;
        }catch(std::exception const& e) {
            setError("Switch rejected: "+QString::fromUtf8(e.what()));
            QSignalBlocker block(tabs);
            tabs->setCurrentIndex(previousTab);
        }
        synchronizing=false;
    });

    // Signatures are part of the function asset, not a second transient
    // graph schema. The canvas creates one immutable source node per input.
    auto editSignature=[&](bool input,bool remove) {
        if(!loaded)return;
        if(tabs->currentWidget()==source) {
            tabs->setCurrentWidget(graph);
            if(tabs->currentWidget()!=graph)return;
        }
        auto doc=graph->functionDocument();
        QString const field=input?"inputs":"outputs";
        QJsonArray pins=doc.value(field).toArray();
        if(remove) {
            if(pins.isEmpty() || (!input && pins.size()==1)) {
                setError("Cannot remove the last required output.");return;
            }
            QStringList choices;
            for(auto const& pin:pins)
                choices.append(pin.toObject().value("name").toString());
            bool ok=false;
            auto name=QInputDialog::getItem(&dialog,
                input?"Remove Input":"Remove Output",
                "Pin:",choices,0,false,&ok);
            if(!ok)return;
            QJsonArray remaining;
            QString oldId;
            for(auto const& pin:pins) {
                auto entry=pin.toObject();
                if(entry.value("name").toString()==name)continue;
                remaining.append(entry);
            }
            pins=remaining;
            if(input) {
                auto nodes=doc.value("nodes").toArray();
                for(auto const& raw:nodes) {
                    auto node=raw.toObject();
                    if(node.value("op").toString()=="function_input" &&
                       node.value("name").toString()==name)
                        oldId=node.value("id").toString();
                }
                QJsonArray cleaned;
                for(auto const& raw:nodes) {
                    auto node=raw.toObject();
                    if(node.value("id").toString()==oldId)continue;
                    for(auto const* slot:{"a","b","t"})
                        if(node.value(slot).toString()==oldId)node.remove(slot);
                    if(node.value("op").toString()=="function") {
                        auto bound=node.value("inputs").toObject();
                        for(auto const& key:bound.keys())
                            if(bound.value(key).toString()==oldId)bound.remove(key);
                        node["inputs"]=bound;
                    }
                    cleaned.append(node);
                }
                doc["nodes"]=cleaned;
                auto outputs=doc.value("outputs").toArray();
                for(int i=0;i<outputs.size();++i) {
                    auto out=outputs[i].toObject();
                    if(out.value("node").toString()==oldId)out["node"]="";
                    outputs[i]=out;
                }
                doc["outputs"]=outputs;
            }
        }else {
            if(pins.size()>=8) {
                setError("A function supports up to eight pins per direction.");return;
            }
            bool ok=false;
            auto name=QInputDialog::getText(&dialog,
                input?"Add Function Input":"Add Function Output",
                "Identifier:",QLineEdit::Normal,"",&ok).trimmed();
            if(!ok || name.isEmpty())return;
            static QRegularExpression const ident("^[A-Za-z_][A-Za-z0-9_]{0,47}$");
            if(!ident.match(name).hasMatch()) {
                setError("Pin name must be an ASCII identifier (maximum 48 characters).");
                return;
            }
            for(auto const& pin:pins) {
                if(pin.toObject().value("name").toString()==name) {
                    setError("Duplicate Material Function pin name: "+name);
                    return;
                }
            }
            auto const types=QStringList{"scalar","vector2","vector3","vector4"};
            auto type=QInputDialog::getItem(&dialog,"Typed Function Pin",
                "Type:",types,input?3:3,false,&ok);
            if(!ok)return;
            QJsonObject pin{{"name",name},{"type",type}};
            if(input) {
                QString id="input_"+name;
                auto nodes=doc.value("nodes").toArray();
                for(int index=1;;++index) {
                    bool exists=false;
                    for(auto const& raw:nodes)
                        if(raw.toObject().value("id").toString()==id)
                            exists=true;
                    if(!exists)break;
                    id="input_"+QString::number(index);
                }
                if(nodes.size()>=64) {
                    setError("Function node budget exceeded (64 nodes).");return;
                }
                nodes.append(QJsonObject{{"id",id},{"op","function_input"},
                    {"name",name},{"x",-390},{"y",40+int(pins.size())*130}});
                doc["nodes"]=nodes;
            }else pin["node"]="";
            pins.append(pin);
        }
        doc[field]=pins;
        graph->setFunctionDocument(doc,reference);
        dirty=true;
        dialog.setWindowModified(true);
        describe(tree,doc);
        message->setText("Signature updated. Connect output pins and validate before saving.");
    };
    QObject::connect(addInput,&QPushButton::clicked,&dialog,
                     [&]{editSignature(true,false);});
    QObject::connect(addOutput,&QPushButton::clicked,&dialog,
                     [&]{editSignature(false,false);});
    QObject::connect(removeInput,&QPushButton::clicked,&dialog,
                     [&]{editSignature(true,true);});
    QObject::connect(removeOutput,&QPushButton::clicked,&dialog,
                     [&]{editSignature(false,true);});

    auto check=[&]() -> bool {
        try {
            auto doc=activeDocument();
            validateMaterialFunctionDocument(doc,root,reference);
            describe(tree,doc);
            message->setStyleSheet("color:#8bcba9;");
            message->setText("All typed outputs, nested dependencies and HLSL validated.");
            return true;
        }catch(std::exception const& e) {
            setError("Validation failed: "+QString::fromUtf8(e.what()));
            return false;
        }
    };
    QObject::connect(validateButton,&QPushButton::clicked,&dialog,
                     [&] { (void)check(); });
    QObject::connect(save,&QPushButton::clicked,&dialog,[&] {
        if(!loaded || !check())return;
        try {
            auto doc=activeDocument();
            auto target=material_authoring::resolve_existing(
                root,QString::fromStdString(reference));
            QSaveFile file(QString::fromStdWString(target.wstring()));
            if(!file.open(QIODevice::WriteOnly))
                throw std::runtime_error("Cannot open atomic Material Function save");
            auto bytes=QJsonDocument(doc).toJson(QJsonDocument::Indented);
            if(file.write(bytes)!=bytes.size()||!file.commit())
                throw std::runtime_error("Cannot commit Material Function");
            dirty=false;
            dialog.setWindowModified(false);
            message->setStyleSheet("color:#8bcba9;");
            message->setText("Saved Material Function: "+
                QString::fromStdString(reference));
        }catch(std::exception const& e) {
            setError("Save failed: "+QString::fromUtf8(e.what()));
        }
    });
    QObject::connect(close,&QPushButton::clicked,&dialog,[&] {
        if(dirty) {
            auto answer=QMessageBox::question(&dialog,"Unsaved Function",
                "Discard unsaved Material Function edits?",
                QMessageBox::Discard|QMessageBox::Cancel,
                QMessageBox::Cancel);
            if(answer!=QMessageBox::Discard)return;
        }
        dialog.accept();
    });
    return dialog.exec();
}
} // namespace northstarcpp::editor
