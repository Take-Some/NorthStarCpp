// First-party Material Instance UI; persistent schema uses runtime inheritance.
#include "qt_material_instances.hpp"
#include "qt_material_authoring.hpp"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <functional>
#include <vector>
#include <stdexcept>

namespace northstarcpp::editor::material_authoring {
bool edit_material_instance_dialog(QWidget* owner,
        std::filesystem::path const& root,QString const& reference) {
    auto original=load_material_instance(root,reference);
    auto const inherited=resolve_material_instance(
        root,original.value("parent").toString());
    auto const overrides=original.value("overrides").toObject();
    QDialog dialog(owner);
    dialog.setObjectName("northstarMaterialInstanceEditor");
    dialog.setWindowTitle("Material Instance - "+reference);
    dialog.resize(630,700);
    auto* page=new QVBoxLayout(&dialog);
    auto* parent=new QLabel(
        "Parent: "+original.value("parent").toString()+"\n"
        "Unchecked values inherit from the parent material.",&dialog);
    parent->setWordWrap(true);
    page->addWidget(parent);
    auto* scroll=new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    auto* body=new QWidget(scroll);
    auto* fields=new QFormLayout(body);
    scroll->setWidget(body);
    page->addWidget(scroll,1);
    struct Entry {
        QString name;
        QCheckBox* enabled;
        std::function<QJsonValue()> value;
    };
    std::vector<Entry> entries;
    auto addField=[&](QString const& key,QString const& title,
                      QWidget* control,std::function<QJsonValue()> read) {
        auto* wrap=new QWidget(body);
        auto* line=new QHBoxLayout(wrap);
        line->setContentsMargins(0,0,0,0);
        auto* checkbox=new QCheckBox("Override",wrap);
        checkbox->setObjectName("instanceOverride_"+key);
        checkbox->setChecked(overrides.contains(key));
        control->setEnabled(checkbox->isChecked());
        QObject::connect(checkbox,&QCheckBox::toggled,control,
                         &QWidget::setEnabled);
        line->addWidget(checkbox);
        line->addWidget(control,1);
        fields->addRow(title,wrap);
        entries.push_back({key,checkbox,std::move(read)});
    };
    auto resolved=[&](QString const& name)->QJsonValue {
        return overrides.contains(name)?overrides.value(name):
               inherited.value(name);
    };
    auto* shader=new QComboBox(body);
    shader->setObjectName("instanceShader");
    shader->addItems({"standard_lit","unlit"});
    shader->setCurrentText(resolved("shader").toString("standard_lit"));
    addField("shader","Shader",shader,[shader]{return QJsonValue(shader->currentText());});
    auto* color=new QLineEdit(resolved("base_color").toString("#ffffff"),body);
    color->setObjectName("instanceBaseColor");
    addField("base_color","Base Color",color,[color]{
        return QJsonValue(color->text().trimmed());
    });
    auto number=[&](QString const& key,QString const& label,double fallback) {
        auto* spin=new QDoubleSpinBox(body);
        spin->setObjectName("instance_"+key);
        spin->setRange(0.,1.);
        spin->setDecimals(4);
        spin->setSingleStep(.025);
        spin->setValue(resolved(key).toDouble(fallback));
        addField(key,label,spin,[spin]{return QJsonValue(spin->value());});
    };
    number("metallic","Metallic",0.);
    number("roughness","Roughness",.65);
    number("opacity","Opacity",1.);
    auto const textures=list_textures(root);
    for(auto const& spec:std::initializer_list<std::pair<const char*,const char*>>{
        {"base_color_texture","Base Color Texture"},
        {"normal_texture","Normal Texture"},
        {"roughness_texture","Roughness Texture"},
        {"metallic_texture","Metallic Texture"},
        {"emissive_texture","Emissive Texture"}}) {
        QString const key=QString::fromUtf8(spec.first);
        auto* picker=new QComboBox(body);
        picker->setObjectName("instance_"+key);
        picker->addItem("None",QString{});
        for(auto const& ref:textures)picker->addItem(ref,ref);
        QString const current=resolved(key).toString();
        int selected=picker->findData(current);
        if(selected<0 && !current.isEmpty()) {
            picker->addItem("Missing: "+current,current);
            selected=picker->count()-1;
        }
        picker->setCurrentIndex(std::max(0,selected));
        addField(key,QString::fromUtf8(spec.second),picker,[picker]{
            return QJsonValue(picker->currentData().toString());
        });
    }
    if(overrides.contains("graph")) {
        auto* notice=new QLabel(
            "This instance contains a graph override. It will be preserved "
            "unchanged; node editing is available on source materials.",body);
        notice->setWordWrap(true);
        fields->addRow(notice);
    }
    auto* buttons=new QDialogButtonBox(
        QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
    page->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons->button(QDialogButtonBox::Save),
                     &QPushButton::clicked,&dialog,[&] {
        QJsonObject nextOverrides=overrides;
        for(auto const& field:entries) {
            if(field.enabled->isChecked())
                nextOverrides.insert(field.name,field.value());
            else nextOverrides.remove(field.name);
        }
        QJsonObject next=original;
        next["overrides"]=nextOverrides;
        try {
            save_material_instance(root,reference,next);
            dialog.accept();
        }catch(std::exception const& e) {
            QMessageBox::warning(&dialog,"Material Instance rejected",
                                 QString::fromUtf8(e.what()));
        }
    });
    return dialog.exec()==QDialog::Accepted;
}
} // namespace northstarcpp::editor::material_authoring
