// Editable project collision channel names (slots 6..15).
#pragma once
#include "qt_collision_project_settings.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>
namespace northstarcpp::editor::collision_project {
inline bool edit_names(QWidget* owner,std::filesystem::path const& root) {
    Settings settings=load(root);
    QDialog dialog(owner);
    dialog.setObjectName("projectCollisionChannelsDialog");
    dialog.setWindowTitle("Project Collision Channels");
    dialog.resize(460,560);
    auto* layout=new QVBoxLayout(&dialog);
    auto* form=new QFormLayout;
    std::array<QLineEdit*,10> editors{};
    for(int i=6;i<16;++i) {
        auto* input=new QLineEdit(settings.names[i],&dialog);
        input->setMaxLength(40);
        input->setObjectName(QString("collisionChannelName_%1").arg(i));
        editors[i-6]=input;
        form->addRow(QString("Channel %1").arg(i),input);
    }
    layout->addLayout(form);
    auto* buttons=new QDialogButtonBox(
        QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        try {
            for(int i=6;i<16;++i) {
                auto name=editors[i-6]->text().trimmed();
                if(!valid_name(name))
                    throw std::invalid_argument("Names must start with a letter and use A-Z, 0-9, _ or -");
                settings.names[i]=name;
            }
            for(int i=0;i<16;++i)
                for(int j=0;j<i;++j)
                    if(settings.names[i].compare(settings.names[j],Qt::CaseInsensitive)==0)
                        throw std::invalid_argument("Duplicate collision channel names");
            save(root,settings);
            dialog.accept();
        }catch(std::exception const& ex) {
            QMessageBox::warning(&dialog,"Collision Channels",
                QString::fromUtf8(ex.what()));
        }
    });
    return dialog.exec()==QDialog::Accepted;
}
} // namespace northstarcpp::editor::collision_project
