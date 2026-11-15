// Full per-entry YMAT Material Editor for a standalone or embedded Content
// Browser. NEF8 library entries are selected by @name; all other entries stay
// intact after atomic save. Unknown PBR parameters/texture slots are editable.
#include "qt_material_asset_dialog.hpp"
#include "qt_material_authoring.hpp"
#include "qt_material_graph_editor.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QSet>
#include <QMap>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <cmath>
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
namespace northstarcpp::editor {
namespace {
QTableWidget* table(QWidget* parent,QStringList headers) {
    auto* result=new QTableWidget(parent);
    result->setColumnCount(headers.size());
    result->setHorizontalHeaderLabels(headers);
    result->horizontalHeader()->setStretchLastSection(true);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setObjectName(headers.first()=="Name"?
        "materialParameterTable":"materialTextureTable");
    return result;
}
QString text(QTableWidget const* view,int row,int column) {
    auto* item=view->item(row,column);
    return item?item->text().trimmed():QString{};
}
QJsonArray readParameters(QTableWidget* view) {
    QJsonArray records;
    QSet<QString> names;
    QMap<QString,int> arity{
        {"float",1},{"float2",2},{"float3",3},{"float4",4},{"color",4}};
    for(int row=0;row<view->rowCount();++row) {
        auto name=text(view,row,0),type=text(view,row,1);
        auto value=text(view,row,2);
        if(name.isEmpty()&&type.isEmpty()&&value.isEmpty())continue;
        if(name.isEmpty()||names.contains(name)||
           !arity.contains(type))
            throw std::invalid_argument("Duplicate name or invalid YMAT parameter type");
        names.insert(name);
        auto parts=value.split(',',Qt::KeepEmptyParts);
        if(parts.size()!=arity.value(type))
            throw std::invalid_argument(
                ("YMAT parameter "+name+" has wrong component count").toStdString());
        QJsonArray values;
        for(auto const& part:parts) {
            bool valid=false;
            double number=part.trimmed().toDouble(&valid);
            if(!valid||!std::isfinite(number))
                throw std::invalid_argument("Invalid numeric YMAT parameter");
            values.append(number);
        }
        records.append(QJsonObject{{"name",name},{"type",type},{"value",values}});
    }
    return records;
}
QJsonArray readTextures(QTableWidget* view) {
    QJsonArray records;
    QSet<QString> usedSlots;
    for(int row=0;row<view->rowCount();++row) {
        auto slot=text(view,row,0),ref=text(view,row,1);
        if(slot.isEmpty()&&ref.isEmpty())continue;
        if(slot.isEmpty()||usedSlots.contains(slot))
            throw std::invalid_argument("Invalid or duplicate YMAT texture slot");
        usedSlots.insert(slot);
        if(ref.isEmpty())continue;
        auto const at=ref.indexOf('@');
        if(at<=0||at==ref.size()-1 ||
           !ref.left(at).endsWith(".ytd",Qt::CaseInsensitive) ||
           ref.indexOf('@',at+1)>=0)
            throw std::invalid_argument("YMAT texture must reference a YTD@entry");
        records.append(QJsonObject{{"slot",slot},{"ref",ref}});
    }
    return records;
}
void fill(QTableWidget* view,QJsonArray const& values,
          std::initializer_list<char const*> fields) {
    view->setRowCount(0);
    for(auto const& raw:values) {
        auto object=raw.toObject();
        int const row=view->rowCount();
        view->insertRow(row);
        int column=0;
        for(auto field:fields) {
            QString value;
            if(QString(field)=="value") {
                QStringList numbers;
                for(auto const& scalar:object.value(field).toArray())
                    numbers.append(QString::number(scalar.toDouble(),'g',9));
                value=numbers.join(',');
            }else value=object.value(field).toString();
            view->setItem(row,column++,new QTableWidgetItem(value));
        }
    }
}
}
int openMaterialAssetDialog(QWidget* parent,
    std::filesystem::path const& contentRoot,
    std::string const& reference) {
    namespace ma=material_authoring;
    QString const name=QString::fromUtf8(reference.data(),int(reference.size()));
    QDialog dialog(parent);
    dialog.setObjectName("northstarMaterialAssetEditor");
    dialog.setWindowTitle("Material Editor - "+name);
    dialog.resize(1120,820);
    auto* layout=new QVBoxLayout(&dialog);
    auto* tabs=new QTabWidget(&dialog);
    tabs->setObjectName("materialAssetEditorTabs");
    layout->addWidget(tabs,1);

    auto* preview=new QWidget(tabs);
    auto* previewLayout=new QVBoxLayout(preview);
    auto* previewTitle=new QLabel(name,preview);
    previewTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewLayout->addWidget(previewTitle);
    auto* sphere=new QLabel(preview);
    sphere->setObjectName("materialAssetPreviewSphere");
    sphere->setAlignment(Qt::AlignCenter);
    sphere->setMinimumSize(260,260);
    previewLayout->addWidget(sphere,1);
    tabs->addTab(preview,"Preview");

    auto* surface=new QWidget(tabs);
    auto* scroll=new QScrollArea(tabs);
    scroll->setWidgetResizable(true);
    scroll->setWidget(surface);
    auto* surfaceLayout=new QVBoxLayout(surface);
    auto* basic=new QGroupBox("Surface properties",surface);
    auto* form=new QFormLayout(basic);
    auto* shader=new QLineEdit(basic);
    shader->setObjectName("materialShader");
    auto* blend=new QComboBox(basic);
    blend->addItems({"opaque","masked","translucent"});
    auto* color=new QLineEdit(basic);
    color->setObjectName("materialBaseColor");
    auto* twoSided=new QCheckBox(basic);
    auto decimal=[&](char const* id) {
        auto* value=new QDoubleSpinBox(basic);
        value->setObjectName(id);value->setRange(0,1);
        value->setDecimals(5);value->setSingleStep(.025);
        return value;
    };
    auto* metallic=decimal("materialMetallic");
    auto* roughness=decimal("materialRoughness");
    auto* cutoff=decimal("materialAlphaCutoff");
    form->addRow("Shader",shader);
    form->addRow("Blend mode",blend);
    form->addRow("Base color",color);
    form->addRow("Metallic",metallic);
    form->addRow("Roughness",roughness);
    form->addRow("Alpha cutoff",cutoff);
    form->addRow("Two-sided",twoSided);
    surfaceLayout->addWidget(basic);

    auto* parametersTitle=new QLabel("All material parameters (name / type / comma-separated values)",surface);
    surfaceLayout->addWidget(parametersTitle);
    auto* parameters=table(surface,{"Name","Type","Value"});
    parameters->setMinimumHeight(170);
    surfaceLayout->addWidget(parameters);
    auto* paramControls=new QWidget(surface);
    auto* paramActions=new QHBoxLayout(paramControls);
    paramActions->setContentsMargins(0,0,0,0);
    auto* addParam=new QPushButton("Add Parameter",paramControls);
    auto* removeParam=new QPushButton("Remove Selected",paramControls);
    paramActions->addWidget(addParam);paramActions->addWidget(removeParam);
    paramActions->addStretch();
    surfaceLayout->addWidget(paramControls);
    QObject::connect(addParam,&QPushButton::clicked,parameters,[parameters] {
        parameters->insertRow(parameters->rowCount());
    });
    QObject::connect(removeParam,&QPushButton::clicked,parameters,[parameters] {
        auto rows=parameters->selectionModel()->selectedRows();
        std::sort(rows.begin(),rows.end(),[](auto const& a,auto const& b) {
            return a.row()>b.row();
        });
        for(auto const& row:rows)parameters->removeRow(row.row());
    });

    auto* texturesTitle=new QLabel("Texture bindings (including custom slots)",surface);
    surfaceLayout->addWidget(texturesTitle);
    auto* textures=table(surface,{"Slot","YTD@Entry reference"});
    textures->setMinimumHeight(160);
    surfaceLayout->addWidget(textures);
    auto* textureControls=new QWidget(surface);
    auto* textureActions=new QHBoxLayout(textureControls);
    textureActions->setContentsMargins(0,0,0,0);
    auto* addTexture=new QPushButton("Add Texture Binding",textureControls);
    auto* removeTexture=new QPushButton("Remove Selected",textureControls);
    textureActions->addWidget(addTexture);textureActions->addWidget(removeTexture);
    textureActions->addStretch();
    surfaceLayout->addWidget(textureControls);
    QObject::connect(addTexture,&QPushButton::clicked,textures,[textures] {
        textures->insertRow(textures->rowCount());
    });
    QObject::connect(removeTexture,&QPushButton::clicked,textures,[textures] {
        auto rows=textures->selectionModel()->selectedRows();
        std::sort(rows.begin(),rows.end(),[](auto const& a,auto const& b) {
            return a.row()>b.row();
        });
        for(auto const& row:rows)textures->removeRow(row.row());
    });
    tabs->addTab(scroll,"Surface");

    auto* graphPage=new QWidget(tabs);
    auto* graphLayout=new QVBoxLayout(graphPage);
    auto* graphStatus=new QLabel(graphPage);
    graphStatus->setObjectName("materialAssetGraphStatus");
    graphStatus->setWordWrap(true);
    graphLayout->addWidget(graphStatus);
    auto* graph=new MaterialGraphEditor({},graphPage,contentRoot);
    graph->setObjectName("materialAssetGraphCanvas");
    graphLayout->addWidget(graph,1);
    tabs->addTab(graphPage,"Material Graph");

    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    auto* save=buttons->addButton("Save YMAT",QDialogButtonBox::AcceptRole);
    save->setObjectName("materialAssetSaveYmat");
    layout->addWidget(buttons);
    QObject::connect(buttons->button(QDialogButtonBox::Close),
                     &QPushButton::clicked,&dialog,&QDialog::accept);

    QJsonObject document;
    bool writable=false;
    try {
        document=ma::load_descriptor(contentRoot,name);
        writable=true;
        shader->setText(document.value("shader").toString());
        blend->setCurrentText(document.value("blend_mode").toString("opaque"));
        color->setText(document.value("base_color").toString("#ffffff"));
        metallic->setValue(document.value("metallic").toDouble());
        roughness->setValue(document.value("roughness").toDouble(.5));
        cutoff->setValue(document.value("alpha_cutoff").toDouble());
        twoSided->setChecked(document.value("two_sided").toBool());
        fill(parameters,document.value("parameters").toArray(),{"name","type","value"});
        fill(textures,document.value("textures").toArray(),{"slot","ref"});
        graph->setGraph(document.value("graph").toObject());
        graphStatus->setText(
            "Editing selected YMAT entry. All other materials in this library "
            "and their original bindings are preserved.");
    }catch(std::exception const& error) {
        graphStatus->setText("Material read failed: "+QString::fromUtf8(error.what()));
    }
    try {
        auto image=ma::sphere_preview(contentRoot,name,384);
        if(!image.isNull())
            sphere->setPixmap(QPixmap::fromImage(image).scaled(
                QSize(450,450),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        else sphere->setText("Material preview unavailable");
    }catch(std::exception const&) {
        sphere->setText("Material preview unavailable");
    }
    graph->setEnabled(writable);
    save->setEnabled(writable);
    surface->setEnabled(writable);
    QObject::connect(save,&QPushButton::clicked,&dialog,[&] {
        QString diagnostic;
        if(!graph->validate(&diagnostic)) {
            tabs->setCurrentWidget(graphPage);
            QMessageBox::warning(&dialog,"Material Graph",diagnostic);
            return;
        }
        try {
            auto updated=document;
            updated["shader"]=shader->text().trimmed();
            updated["blend_mode"]=blend->currentText();
            updated["base_color"]=color->text().trimmed();
            updated["metallic"]=metallic->value();
            updated["roughness"]=roughness->value();
            updated["alpha_cutoff"]=cutoff->value();
            updated["two_sided"]=twoSided->isChecked();
            updated["parameters"]=readParameters(parameters);
            updated["textures"]=readTextures(textures);
            // Surface controls and the advanced table edit the same PBR
            // fields. A value changed only in the table must not be reset by
            // an untouched surface spinbox.
            auto advanced=updated.value("parameters").toArray();
            for(auto const& raw:advanced) {
                auto entry=raw.toObject();
                auto field=entry.value("name").toString();
                auto values=entry.value("value").toArray();
                if(values.isEmpty())continue;
                if(field=="roughness" &&
                   std::abs(roughness->value()-
                       document.value("roughness").toDouble(.5))<1e-6)
                    updated["roughness"]=values[0].toDouble();
                if(field=="metallic" &&
                   std::abs(metallic->value()-
                       document.value("metallic").toDouble())<1e-6)
                    updated["metallic"]=values[0].toDouble();
                if(field=="base_color" && values.size()>=3 &&
                   color->text().trimmed()==document.value("base_color").toString()) {
                    auto linearToSrgb=[](double x) {
                        x=std::clamp(x,0.,1.);
                        return x<=0.0031308?x*12.92:
                            1.055*std::pow(x,1./2.4)-0.055;
                    };
                    QColor selected;
                    selected.setRgbF(linearToSrgb(values[0].toDouble()),
                                     linearToSrgb(values[1].toDouble()),
                                     linearToSrgb(values[2].toDouble()));
                    updated["base_color"]=selected.name();
                }
            }
            // Synchronize well-known slots with dynamically edited bindings.
            for(QString slot:{"base_color","normal","roughness","metallic","emissive"})
                updated.remove(slot+"_texture");
            for(auto const& entry:updated.value("textures").toArray()) {
                auto bound=entry.toObject();
                QString const slot=bound.value("slot").toString();
                if(slot=="base_color"||slot=="normal"||slot=="roughness"||
                   slot=="metallic"||slot=="emissive")
                    updated[slot+"_texture"]=bound.value("ref").toString();
            }
            if(graph->enabled()&&graph->graph().contains("nodes"))
                updated["graph"]=graph->graph();
            else updated.remove("graph");
            ma::save_descriptor(contentRoot,name,updated);
            dialog.accept();
        }catch(std::exception const& error) {
            QMessageBox::warning(&dialog,"Material save failed",
                                 QString::fromUtf8(error.what()));
        }
    });
    return dialog.exec();
}
} // namespace northstarcpp::editor
