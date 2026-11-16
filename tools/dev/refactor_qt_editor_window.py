#!/usr/bin/env python3
"""One-time guarded decomposition of the active Qt editor shell.

Moves only whole out-of-line methods; keeps QtEditorWindow and engine ABI intact.
Backs up the original source under out/ before writing.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
editor = root / "engine/northstarcpp/modules/editor"
source = editor / "qt_editor_window.cpp"
raw = source.read_bytes()
newline = "\r\n" if b"\r\n" in raw else "\n"
s = raw.decode("utf-8").replace("\r\n", "\n")
if ("QString QtEditorWindow::playerExecutable()" not in s
        and "bool QtEditorWindow::runSmoke(" not in s):
    print("QtEditorWindow decomposition already present; nothing to migrate.")
    raise SystemExit(0)
a = s.index("std::string utf8(QString const& value)")
b = s.index("bool validateMaterialComponent(", a)
c = s.index("QString QtEditorWindow::playerExecutable()")
d = s.index("void QtEditorWindow::showContentBrowser(", c)
e = s.index("bool QtEditorWindow::runSmoke(")
assert a < b < c < d < e and s.rstrip().endswith("}\n}")
assert s.count("QString QtEditorWindow::playerExecutable()") == 1
assert s.count("bool QtEditorWindow::runSmoke(") == 1
old_helpers = s[a:b]
play = s[c:d]
smoke = s[e:].rsplit("\n}", 1)[0] + "\n"
backup = root / "out/refactor_backup/qt_editor_window.cpp"
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists():
    backup.write_bytes(raw)

common_h = """// Reusable editor path/camera helpers shared by shell, Play-In-Editor and QA.
#pragma once
#include "editor_tools.hpp"
#include <QString>
#include <QVariant>
#include <filesystem>
#include <string>
namespace northstarcpp::editor {
class QtEditorViewport;
namespace window_detail {
std::string utf8(QString const& value);
QString from8(std::string const& value);
std::filesystem::path native(QString const& value);
QString qpath(std::filesystem::path const& value);
QVariantList cameraValues(editor_tools::EditorCamera const& camera);
bool applyCameraValues(QtEditorViewport* viewport,QVariant const& data);
QString sceneCameraKey(std::filesystem::path const& file);
QString folderRoot(std::filesystem::path const& scene);
std::filesystem::path sharedMaterialRoot();
bool availableMaterial(std::filesystem::path const& root,QString const& reference);
}
}
"""
common_cpp = """#include "qt_editor_window_paths.hpp"
#include "qt_editor_viewport.hpp"
#include "qt_material_authoring.hpp"
#include <northstarcpp/scene_component_registry.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <array>
#include <cmath>
#include <system_error>
namespace northstarcpp::editor::window_detail {
namespace fs=std::filesystem;
""" + old_helpers + "}\n"

preamble = s[:s.index("namespace northstarcpp::editor {")]
extracted = preamble.replace(
    '#include "qt_editor_window.hpp"',
    '#include "qt_editor_window.hpp"\n#include "qt_editor_window_paths.hpp"', 1)
prefix = extracted + "\nnamespace northstarcpp::editor {\nnamespace fs=std::filesystem;\nusing namespace window_detail;\n"
new_play = prefix + play + "}\n"
new_smoke = prefix + smoke + "}\n"
old = s[:a] + s[b:c] + s[d:e] + "}\n"
old = old.replace(
    '#include "qt_editor_window.hpp"',
    '#include "qt_editor_window.hpp"\n#include "qt_editor_window_paths.hpp"', 1)
old = old.replace(
    "namespace fs=std::filesystem;\nnamespace {",
    "namespace fs=std::filesystem;\nusing namespace window_detail;\nnamespace {", 1)

cmake = root / "engine/northstarcpp/CMakeLists.txt"
cm = cmake.read_bytes()
cm_nl = "\r\n" if b"\r\n" in cm else "\n"
ct = cm.decode("utf-8").replace("\r\n", "\n")
marker = "    modules/editor/qt_editor_window.cpp\n"
assert ct.count(marker) == 1
ct = ct.replace(
    marker,
    marker + "    modules/editor/qt_editor_window_paths.cpp\n"
    + "    modules/editor/qt_editor_window_play.cpp\n"
    + "    modules/editor/qt_editor_window_smoke.cpp\n", 1)
outputs = {
    source: old,
    editor/"qt_editor_window_paths.hpp": common_h,
    editor/"qt_editor_window_paths.cpp": common_cpp,
    editor/"qt_editor_window_play.cpp": new_play,
    editor/"qt_editor_window_smoke.cpp": new_smoke,
    cmake: ct,
}
for path, content in outputs.items():
    path.write_bytes(content.replace("\n", cm_nl if path == cmake else newline).encode("utf-8"))
print(f"Qt shell: {len(s.splitlines())} -> {len(old.splitlines())} lines; "
      f"PIE {len(play.splitlines())}, QA {len(smoke.splitlines())}, "
      f"helpers {len(old_helpers.splitlines())}.")
print(f"Backup: {backup}")
