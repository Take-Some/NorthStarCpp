# NorthStarCpp Project Browser — Qt 6 Widgets

NorthStarCpp's current Project Browser uses **C++17, Qt 6 Widgets and Font Awesome 6 Free**.
Its interface is light-themed and the Qt application is independent of Python.

The previous Win32/GDI browser has been superseded and is **not** compiled into
northstarcpp_project_manager.exe. The native C++ Project Catalog API, module
contracts and editor launching behavior remain supported.

For architecture, supported project actions, build requirements, Qt runtime
deployment, licenses and acceptance tests, see **PROJECT_BROWSER_QT.md**.

Run the Qt Browser with:

    northstarcpp.cmd project-manager

Or from the configured build tree:

    out/qt-browser-debug/northstarcpp_project_manager.exe

To run its automated UI smoke:

    out/qt-browser-debug/northstarcpp_project_manager.exe --smoke

CMake requires Qt6 Widgets to be installed with the same MinGW toolchain.
Font Awesome 6 Free is embedded into Qt resources; licenses are preserved.
