#include "qt_inspector_scroll.hpp"
#include <QAbstractScrollArea>
#include <QDockWidget>
#include <QFormLayout>
#include <QFrame>
#include <QAbstractButton>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QPalette>
#include <QScrollArea>
#include <QSizePolicy>
#include <QWidget>
#include <stdexcept>

namespace northstarcpp::editor {
InspectorScrollLayout createInspectorScroll(QDockWidget* owner) {
    if(!owner)
        throw std::invalid_argument("Inspector requires a QDockWidget");
    // QDockWidget minimum size must not inherit the form's full height:
    // otherwise the dock can occupy the ContentBrowser's allocated space.
    owner->setMinimumSize(0,0);
    auto* panel=new QWidget(owner);
    panel->setObjectName("inspectorPanel");
    panel->setMinimumSize(0,0);
    auto* column=new QVBoxLayout(panel);
    column->setContentsMargins(0,0,0,0);
    column->setSpacing(0);
    auto* search=new QLineEdit(panel);
    search->setObjectName("detailsPropertySearch");
    search->setPlaceholderText("Search properties (name, material, collision...)");
    search->setClearButtonEnabled(true);
    search->setMinimumWidth(0);
    column->addWidget(search);
    auto* scroll=new QScrollArea(panel);
    column->addWidget(scroll,1);
    scroll->setObjectName("inspectorScrollArea");
    // QScrollArea and its internal viewport do not inherit QDockWidget's
    // styled background. Set an explicit dark palette for both surfaces,
    // otherwise the platform's light Base/Window brush leaks into Details.
    QPalette inspectorPalette=scroll->palette();
    QColor const inspectorBackground("#151c26");
    QColor const inspectorText("#d6e4f2");
    inspectorPalette.setColor(QPalette::Window,inspectorBackground);
    inspectorPalette.setColor(QPalette::Base,inspectorBackground);
    inspectorPalette.setColor(QPalette::Text,inspectorText);
    inspectorPalette.setColor(QPalette::WindowText,inspectorText);
    panel->setPalette(inspectorPalette);
    panel->setAutoFillBackground(true);
    scroll->setPalette(inspectorPalette);
    scroll->setAutoFillBackground(true);
    scroll->viewport()->setObjectName("inspectorViewport");
    scroll->viewport()->setPalette(inspectorPalette);
    scroll->viewport()->setAutoFillBackground(true);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scroll->setMinimumSize(0,0);
    scroll->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Ignored);
    auto* details=new QWidget(scroll);
    details->setObjectName("inspectorFormContent");
    details->setPalette(inspectorPalette);
    details->setAutoFillBackground(true);
    details->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);
    auto* form=new QFormLayout(details);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setSizeConstraint(QLayout::SetMinimumSize);
    form->setContentsMargins(12,12,12,12);
    form->setSpacing(8);
    // Property search modifies presentation only, never scene data or History.
    QObject::connect(search,&QLineEdit::textChanged,form,
        [form](QString const& text) {
            QString const needle=text.trimmed();
            auto matches=[&needle](QLayoutItem* item) {
                if(!item)return false;
                QWidget* widget=item->widget();
                if(!widget)return false;
                QString descriptor=widget->objectName();
                if(auto* label=qobject_cast<QLabel*>(widget))
                    descriptor+=' '+label->text();
                if(auto* group=qobject_cast<QGroupBox*>(widget))
                    descriptor+=' '+group->title();
                if(auto* button=qobject_cast<QAbstractButton*>(widget))
                    descriptor+=' '+button->text();
                for(auto* label:widget->findChildren<QLabel*>())
                    descriptor+=' '+label->text();
                for(auto* button:widget->findChildren<QAbstractButton*>())
                    descriptor+=' '+button->text();
                return descriptor.contains(needle,Qt::CaseInsensitive);
            };
            for(int row=0;row<form->rowCount();++row) {
                bool const visible=needle.isEmpty() ||
                    matches(form->itemAt(row,QFormLayout::LabelRole)) ||
                    matches(form->itemAt(row,QFormLayout::FieldRole)) ||
                    matches(form->itemAt(row,QFormLayout::SpanningRole));
#if QT_VERSION >= QT_VERSION_CHECK(6,4,0)
                form->setRowVisible(row,visible);
#else
                // Qt < 6.4: row widgets can be hidden, but some layout
                // spacing may remain until a newer Qt is installed.
                for(auto role:{QFormLayout::LabelRole,QFormLayout::FieldRole,
                               QFormLayout::SpanningRole})
                    if(auto* item=form->itemAt(row,role))
                        if(auto* widget=item->widget())widget->setVisible(visible);
#endif
            }
        });
    return {panel,search,scroll,details,form};
}
} // namespace northstarcpp::editor
