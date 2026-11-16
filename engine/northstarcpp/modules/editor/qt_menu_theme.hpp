// NorthStarCpp Qt menu styling shared by Editor, Content Browser, Project
// Browser, and tools. QMenu uses the :selected pseudo-state for hover; merely
// styling QMenu background does NOT highlight the hovered action.
#pragma once
#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QString>

namespace northstarcpp::editor {
enum class MenuTheme { Dark, Light };

inline QString menuThemeStyle(MenuTheme theme) {
    if(theme==MenuTheme::Light) {
        return QStringLiteral(R"QSS(
QMenuBar { background:#f6f8fb; color:#233749; }
QMenuBar::item { background:transparent; color:#233749;
                 padding:6px 11px; border-radius:4px; }
QMenuBar::item:selected, QMenuBar::item:pressed {
    background:#d3eafa; color:#125a82;
}
QMenu { background:#fff; color:#24374a; border:1px solid #b8cbd9;
        border-radius:5px; padding:5px 3px; }
QMenu::item { background:transparent; color:#24374a;
              padding:7px 26px 7px 28px; margin:1px 4px;
              border-radius:3px; }
QMenu::item:selected {
    background:#cce7f9; color:#105e8e;
}
QMenu::item:disabled { color:#8b9bac; background:transparent; }
QMenu::separator { height:1px; background:#dbe4ee;
                   margin:5px 9px; }
QMenu::indicator { width:15px; height:15px; }
)QSS");
    }
    return QStringLiteral(R"QSS(
QMenuBar { background:#1d2a3a; color:#e1ebf4; }
QMenuBar::item { background:transparent; color:#e1ebf4;
                 padding:6px 11px; border-radius:4px; }
QMenuBar::item:selected, QMenuBar::item:pressed {
    background:#36668a; color:#ffffff;
}
QMenu { background:#1d2a3a; color:#e1ebf4; border:1px solid #49627b;
        border-radius:5px; padding:5px 3px; }
QMenu::item { background:transparent; color:#e1ebf4;
              padding:7px 26px 7px 28px; margin:1px 4px;
              border-radius:3px; }
QMenu::item:selected {
    background:#386e98; color:#ffffff;
}
QMenu::item:disabled { color:#8195a8; background:transparent; }
QMenu::separator { height:1px; background:#3b5065;
                   margin:5px 9px; }
QMenu::indicator { width:15px; height:15px; }
)QSS");
}

// Call once after QApplication is constructed and after any app-level theme
// stylesheet has been installed. Qt draws menus in separate popup windows,
// so a widget-local stylesheet alone is insufficient for every popup.
// QComboBox popups are separate windows. The popup's item view needs
// explicit text/selection colors instead of inheriting the OS black text.
inline QString comboBoxThemeStyle(MenuTheme theme) {
    if(theme==MenuTheme::Light) {
        return QStringLiteral(R"QSS(
QComboBox { color:#233749; background:#ffffff; selection-color:#ffffff;
            selection-background-color:#1476a3; }
QComboBox:hover:!disabled { border-color:#70a9c8; }
QComboBox:focus { border-color:#168fc6; }
QComboBox:disabled { color:#667889; background:#eff2f6; }
QComboBox QAbstractItemView {
    color:#233749; background:#ffffff;
    selection-color:#ffffff; selection-background-color:#1476a3;
    border:1px solid #b8cbd9; outline:0;
}
QComboBox QAbstractItemView::item { color:#233749; padding:5px 9px; min-height:22px; }
QComboBox QAbstractItemView::item:hover,
QComboBox QAbstractItemView::item:selected { background:#1476a3; color:#ffffff; }
QComboBox QAbstractItemView::item:disabled { color:#8191a1; background:#ffffff; }
)QSS");
    }
    return QStringLiteral(R"QSS(
QComboBox { color:#e1ecf7; background:#192331; selection-color:#ffffff;
            selection-background-color:#386e98; }
QComboBox:hover:!disabled { border-color:#6785a0; }
QComboBox:focus { border-color:#67a9d2; }
QComboBox:disabled { color:#a7b5c5; background:#222c3a; }
QComboBox QAbstractItemView {
    color:#e1ecf7; background:#1d2a3a;
    selection-color:#ffffff; selection-background-color:#386e98;
    border:1px solid #49627b; outline:0;
}
QComboBox QAbstractItemView::item { color:#e1ecf7; padding:5px 9px; min-height:22px; }
QComboBox QAbstractItemView::item:hover,
QComboBox QAbstractItemView::item:selected { background:#386e98; color:#ffffff; }
QComboBox QAbstractItemView::item:disabled { color:#a1b2c2; background:#1d2a3a; }
)QSS");
}

// Qt's Windows popup delegate can use palette roles instead of the QSS.
// Keep system fallbacks consistent across active, inactive, disabled states.
inline void installComboBoxPalette(QApplication& app,MenuTheme theme) {
    auto palette=app.palette();
    auto const dark=theme==MenuTheme::Dark;
    auto const background=QColor(dark?"#1d2a3a":"#ffffff");
    auto const foreground=QColor(dark?"#e1ecf7":"#233749");
    auto const highlight=QColor(dark?"#386e98":"#1476a3");
    auto const disabled=QColor(dark?"#a1b2c2":"#667889");
    for(auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled}) {
        auto const text=group==QPalette::Disabled?disabled:foreground;
        palette.setColor(group,QPalette::Base,background);
        palette.setColor(group,QPalette::Text,text);
        palette.setColor(group,QPalette::WindowText,text);
        palette.setColor(group,QPalette::ButtonText,text);
        palette.setColor(group,QPalette::HighlightedText,QColor("#ffffff"));
        palette.setColor(group,QPalette::Highlight,highlight);
    }
    app.setPalette(palette);
}

inline void installMenuTheme(QApplication& app,MenuTheme theme) {
    installComboBoxPalette(app,theme);
    app.setStyleSheet(app.styleSheet()+menuThemeStyle(theme)+comboBoxThemeStyle(theme));
}
} // namespace northstarcpp::editor
