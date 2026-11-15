#include "qt_inspector_scroll.hpp"
#include <QAbstractScrollArea>
#include <QDockWidget>
#include <QFormLayout>
#include <QFrame>
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
    auto* scroll=new QScrollArea(owner);
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
    return {scroll,details,form};
}
} // namespace northstarcpp::editor
