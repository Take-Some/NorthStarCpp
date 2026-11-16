#pragma once
// Shared Qt Editor inspector layout contract. The scroll viewport,
// not the form's content, determines the dock's minimum size.
class QDockWidget;
class QScrollArea;
class QWidget;
class QFormLayout;
class QLineEdit;

namespace northstarcpp::editor {
struct InspectorScrollLayout {
    QWidget* panel=nullptr;
    QLineEdit* search=nullptr;
    QScrollArea* scroll=nullptr;
    QWidget* content=nullptr;
    QFormLayout* form=nullptr;
};
InspectorScrollLayout createInspectorScroll(QDockWidget* owner);
} // namespace northstarcpp::editor
