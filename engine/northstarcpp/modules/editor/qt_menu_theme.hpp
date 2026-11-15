// NorthStarCpp Qt menu styling shared by Editor, Content Browser, Project
// Browser, and tools. QMenu uses the :selected pseudo-state for hover; merely
// styling QMenu background does NOT highlight the hovered action.
#pragma once
#include <QApplication>
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
inline void installMenuTheme(QApplication& app,MenuTheme theme) {
    app.setStyleSheet(app.styleSheet()+menuThemeStyle(theme));
}
} // namespace northstarcpp::editor
