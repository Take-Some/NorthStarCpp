#!/usr/bin/env python3
"""Idempotent narrow extraction of Player options and Qt editor dock/theme code.
Preserves existing code verbatim and does not alter providers or frame loop.
Run once from the repository root. Aborts before writing on any mismatch.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
editor = root / "engine/northstarcpp/modules/editor"
source = editor / "window.cpp"
qt_source = editor / "qt_editor_window_setup.cpp"
cmake_source = root / "engine/northstarcpp/CMakeLists.txt"

def read_exact(path):
    return path.read_bytes().decode("utf-8")

def one(source, needle):
    assert source.count(needle) == 1, f"Expected one {needle!r}, got {source.count(needle)}"
    return source.index(needle)

window = read_exact(source)
qt = read_exact(qt_source)
cmake = read_exact(cmake_source)

# Guard against accidental second runs or source divergence.
assert "window_runtime_options.hpp" not in window
assert "qt_editor_window_content_docks.cpp" not in cmake
assert "qt_editor_theme.hpp" not in qt

opt_start = one(window, "struct Options {")
opt_parse = one(window, "Options options()")
opt_end = one(window, "Scene read_document(")
assert opt_start < opt_parse < opt_end
struct = window[opt_start:opt_parse].replace("\r\n", "\n")
functions = window[opt_parse:opt_end].replace("\r\n", "\n")
assert functions.count("Options options()") == 1
assert functions.count("fs::path discover_shared_assets(") == 1
functions = functions.replace("Options options()", "Options parse_options(bool editor)", 1)
runtime_header = """#pragma once
// Player/Editor command-line contracts, kept independent of runtime frame code.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>

namespace northstarcpp::runtime_options {
namespace fs = std::filesystem;
""" + struct + """Options parse_options(bool editor);
fs::path discover_shared_assets(Options const&, fs::path const&);
} // namespace northstarcpp::runtime_options
"""
runtime_cpp = """#include "window_runtime_options.hpp"
#include <shellapi.h>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace northstarcpp::runtime_options {
""" + functions + "} // namespace northstarcpp::runtime_options\n"

replacement = ("using runtime_options::Options;\n"
               "using runtime_options::discover_shared_assets;\n"
               "Options options() { return runtime_options::parse_options(editor); }\n\n")
if "\r\n" in window:
    replacement = replacement.replace("\n", "\r\n")
window = window[:opt_start] + replacement + window[opt_end:]
window = window.replace('#include "window_scene_streaming.hpp"',
                        '#include "window_scene_streaming.hpp"' + ("\r\n" if "\r\n" in window else "\n") +
                        '#include "window_runtime_options.hpp"', 1)

# Qt dock lifecycle is self-contained; leave all shared action wiring unchanged.
dock_start = one(qt, "QtContentBrowser* QtEditorWindow::createContentBrowserTab(")
setup_start = one(qt, "void QtEditorWindow::setupUi() {")
assert dock_start < setup_start
dock_methods = qt[dock_start:setup_start].replace("\r\n", "\n")
dock_cpp = """#include "qt_editor_window.hpp"
#include "qt_content_browser.hpp"
#include "qt_content_drawer.hpp"
#include "qt_text_asset_workspace.hpp"
#include "qt_material_authoring.hpp"
#include <QDockWidget>
#include <QMessageBox>
#include <QStatusBar>

namespace northstarcpp::editor {
""" + dock_methods + "} // namespace northstarcpp::editor\n"
qt = qt[:dock_start] + qt[setup_start:]

# Extract the editor-wide stylesheet so UI setup only manages widget lifecycle.
css_call = 'setStyleSheet(R"QSS('
css_start = one(qt, css_call)
css_end = qt.index(')QSS");', css_start) + len(')QSS");')
css_raw = qt[css_start + len("setStyleSheet("):css_end - len(");")]
assert css_raw.startswith('R"QSS(') and css_raw.endswith(')QSS"')
theme_header = """#pragma once
#include <QString>
namespace northstarcpp::editor {
QString editor_dark_style_sheet();
}
"""
theme_cpp = """#include "qt_editor_theme.hpp"
namespace northstarcpp::editor {
QString editor_dark_style_sheet() {
    return QString::fromUtf8(""" + css_raw.replace("\r\n", "\n") + """);
}
}
"""
qt = qt[:css_start] + "setStyleSheet(editor_dark_style_sheet());" + qt[css_end:]
qt = qt.replace('#include "qt_editor_window.hpp"',
                '#include "qt_editor_window.hpp"' + ("\r\n" if "\r\n" in qt else "\n") +
                '#include "qt_editor_theme.hpp"', 1)

nl = "\r\n" if "\r\n" in cmake else "\n"
cmake_anchor = "    modules/editor/qt_editor_window_setup.cpp"
one(cmake, cmake_anchor)
cmake = cmake.replace(cmake_anchor, cmake_anchor + nl +
                      "    modules/editor/qt_editor_window_content_docks.cpp" + nl +
                      "    modules/editor/qt_editor_theme.cpp", 1)
player_anchor = "    modules/editor/window.cpp modules/editor/window_scene_streaming.cpp"
one(cmake, player_anchor)
cmake = cmake.replace(player_anchor, player_anchor + nl +
                      "    modules/editor/window_runtime_options.cpp", 1)

outputs = {
    editor / "window_runtime_options.hpp": runtime_header,
    editor / "window_runtime_options.cpp": runtime_cpp,
    editor / "qt_editor_window_content_docks.cpp": dock_cpp,
    editor / "qt_editor_theme.hpp": theme_header,
    editor / "qt_editor_theme.cpp": theme_cpp,
}
for dest in outputs:
    assert not dest.exists(), f"Refusing to overwrite existing {dest}"
# All guards passed; only now write the mutations.
for dest, payload in outputs.items():
    dest.write_bytes(payload.encode("utf-8"))
source.write_bytes(window.encode("utf-8"))
qt_source.write_bytes(qt.encode("utf-8"))
cmake_source.write_bytes(cmake.encode("utf-8"))
print(f"Extracted {len(functions.splitlines())} Player option lines, "
      f"{len(dock_methods.splitlines())} dock lines, "
      f"{len(css_raw.splitlines())} theme lines into 5 files.")
