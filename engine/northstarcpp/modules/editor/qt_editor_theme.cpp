#include "qt_editor_theme.hpp"
namespace northstarcpp::editor {
QString editor_dark_style_sheet() {
    return QString::fromUtf8(R"QSS(
QMainWindow,QDockWidget {background:#151c26;color:#d6e4f2;}
QDockWidget {titlebar-close-icon:none;titlebar-normal-icon:none;}
QDockWidget::title {background:#263446;padding:6px 8px;border:1px solid #34485d;}
QTreeWidget,QLineEdit,QDoubleSpinBox,QPlainTextEdit,QComboBox {
 background:#192331;border:1px solid #36495e;color:#e1ecf7;
 selection-background-color:#356588;border-radius:3px;padding:3px;
}
QTreeWidget::item {padding:4px;}
QTreeWidget::item:selected {background:#366085;}
QWidget#placeActorsPanel {background:#172230;color:#d7e5f2;}
QLabel#placeActorsHeading {color:#e9c469;font-weight:700;font-size:11px;letter-spacing:1px;}
QLabel#placeActorsHelp {color:#8ca4bb;font-size:11px;}
QLineEdit#placeActorsSearch,QComboBox#placeActorsCategory {
 background:#101d2b;border:1px solid #40576e;color:#e5edf7;
 border-radius:4px;padding:6px;
}
QListWidget#placeActorsList {
 background:#182534;border:1px solid #33485c;color:#e3eaf4;
 border-radius:4px;outline:none;padding:4px;
}
QListWidget#placeActorsList::item {padding:6px;border-radius:4px;}
QListWidget#placeActorsList::item:hover {background:#30485f;}
QListWidget#placeActorsList::item:selected {background:#345f82;color:white;}
QLabel {color:#acbed1;}
QPushButton,QToolButton {
 background:#293e51;border:1px solid #45607c;
 color:#e0e9f6;padding:5px;border-radius:4px;
}
QPushButton:hover,QToolButton:hover {background:#386180;}
QStatusBar {background:#1d2a3a;color:#e1ebf4;}
QToolBar {background:#202c3d;border:0;spacing:6px;padding:3px 7px;}
QToolBar#authoringToolbar {background:#1b2735;border-bottom:1px solid #3b5165;spacing:6px;padding:5px 8px;}
QMenu {background:#202c3b;color:#e6eef8;border:1px solid #43566a;padding:5px;}
QMenu::item {padding:7px 24px 7px 12px;border-radius:3px;}
QMenu::item:selected:enabled {background:#345a78;color:#ffffff;}
QMenu::item:disabled {color:#718499;}
QMenu::separator {height:1px;background:#415368;margin:5px 8px;}
QWidget#actorPlacementSearchBox {background:#202c3b;}
QLineEdit#actorPlacementSearch {min-height:24px;background:#131d2b;border:1px solid #49627b;border-radius:4px;padding:3px 8px;}
QToolButton#playModeButton,QToolButton#toolbarContentButton,
QToolButton#placeActorsDockButton {background:#293e51;border:1px solid #415c73;border-radius:4px;padding:5px 9px;color:#e4edf8;}
QToolButton#playModeButton:hover,QToolButton#toolbarContentButton:hover,
QToolButton#placeActorsDockButton:hover {background:#38546d;border-color:#6994b2;}
QToolBar#viewportToolbar {background:#1b2634;border-top:1px solid #35465a;}
QToolBar::separator {background:#455669;width:1px;margin:5px 7px;}
QLabel#toolbarSectionLabel {color:#899db0;font-size:10px;font-weight:700;padding:0 5px;}
QToolButton#createActorButton {
 background:#344b61;border:1px solid #53738e;border-radius:4px;
 color:#f0f5fb;font-weight:600;padding:6px 12px;
}
QToolButton#createActorButton:hover {background:#426581;border-color:#7298b6;}
QWidget#pieControlGroup {
 background:#1b2937;border:1px solid #35485b;border-radius:5px;
}
QToolButton[pieControl="true"] {
 background:transparent;border:1px solid transparent;border-radius:4px;
 padding:0;
}
QToolButton[pieControl="true"]:hover:!disabled {
 background:#35495e;border-color:#50708b;
}
QToolButton[pieControl="true"]:pressed:!disabled {
 background:#192c3e;border-color:#6593ad;
}
QToolButton[pieControl="true"]:disabled {
 background:transparent;border-color:transparent;
}
QCheckBox {color:#dce7f5;}
/* Details is a scrollable dock, not a platform-default white QScrollArea. */
QDockWidget#inspectorDock {background:#151c26;color:#dce7f5;}
QScrollArea#inspectorScrollArea {
 background:#151c26;color:#d6e4f2;border:0;
}
QWidget#inspectorViewport,QWidget#inspectorFormContent {
 background:#151c26;color:#d6e4f2;
}
QWidget#inspectorFormContent QLabel {color:#afc2d5;}
QWidget#inspectorFormContent QCheckBox {color:#dce7f5;}
QGroupBox#sceneComponentsGroup {
 background:#1d2937;color:#dce7f5;
 border:1px solid #34485d;border-radius:5px;
 margin-top:13px;padding-top:6px;
}
QGroupBox#sceneComponentsGroup::title {
 subcontrol-origin:margin;subcontrol-position:top left;
 left:9px;padding:0 5px;background:#151c26;color:#dce7f5;
}
QTreeWidget#sceneComponentsTree {
 background:#192331;alternate-background-color:#192331;
 border:1px solid #36495e;color:#e1ecf7;
}
QTreeWidget#sceneComponentsTree::item:selected {
 background:#31597b;color:#ffffff;
}
QScrollArea#inspectorScrollArea QScrollBar:vertical {
 background:#151c26;width:11px;margin:0;border:0;
}
QScrollArea#inspectorScrollArea QScrollBar::handle:vertical {
 background:#415971;border-radius:4px;min-height:26px;margin:2px;
}
QScrollArea#inspectorScrollArea QScrollBar::handle:vertical:hover {
 background:#597996;
}
QScrollArea#inspectorScrollArea QScrollBar::add-line:vertical,
QScrollArea#inspectorScrollArea QScrollBar::sub-line:vertical {
 height:0;background:none;border:0;
}
QScrollArea#inspectorScrollArea QScrollBar::add-page:vertical,
QScrollArea#inspectorScrollArea QScrollBar::sub-page:vertical {
 background:none;
}
QWidget#worldOutlinerPanel {background:#191f29;}
QWidget#outlinerToolbar {background:#212b38;border-bottom:1px solid #334356;}
QLineEdit#outlinerSearch {
 background:#151e2b;border:1px solid #3c4d60;border-radius:5px;
 padding:4px 8px;color:#e4eaf3;selection-background-color:#2a719b;
}
QLineEdit#outlinerSearch:focus {border-color:#4387b5;}
QToolButton#outlinerExpand,QToolButton#outlinerCollapse,
QToolButton#outlinerAdd,QToolButton#outlinerSort {
 background:#273545;border:1px solid #394a5e;border-radius:4px;
 padding:0;color:#b7c9db;font-size:16px;font-weight:600;
}
QToolButton#outlinerExpand:hover,QToolButton#outlinerCollapse:hover,
QToolButton#outlinerAdd:hover,QToolButton#outlinerSort:hover {
 background:#35516a;border-color:#4c7292;color:white;
}
QToolButton#outlinerSort:checked {background:#31526a;color:#e4f1fb;}
QTreeWidget#actorTree {
 background:#191f29;alternate-background-color:#191f29;
 border:0;border-radius:0;padding:0;color:#dce5ef;
 outline:0;show-decoration-selected:1;
}
QTreeWidget#actorTree::item {
 background:transparent;color:#dce5ef;border:0;
 padding:3px 3px;min-height:25px;
}
QTreeWidget#actorTree::item:hover:!selected {background:#2a3747;color:#f1f6fd;}
QTreeWidget#actorTree::item:selected {background:#31597b;color:white;}
QTreeWidget#actorTree::item:selected:!active {background:#30475e;color:white;}
QTreeWidget#actorTree::branch {background:transparent;}
QTreeWidget#actorTree::branch:hover {background:#273545;}
QLabel#outlinerCount {
 background:#202b38;border-top:1px solid #344354;
 padding:3px 10px;color:#91a8bf;font-size:11px;
}
)QSS");
}
}
