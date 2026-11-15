#pragma once
// Shared Qt Editor inspector layout contract. The scroll viewport,
// not the form's content, determines the dock's minimum size.
class QDockWidget;
class QScrollArea;
class QWidget;
class QFormLayout;

namespace northstarcpp::editor {
struct InspectorScrollLayout {
    QScrollArea* scroll=nullptr;
    QWidget* content=nullptr;
    QFormLayout* form=nullptr;
};
InspectorScrollLayout createInspectorScroll(QDockWidget* owner);
} // namespace northstarcpp::editor
