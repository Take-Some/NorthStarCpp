#!/usr/bin/env python3
"""Guarded extraction: Qt setup inspector/content/providers/signal wiring to its own TU."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
editor=root/"engine/northstarcpp/modules/editor"
src=editor/"qt_editor_window_setup.cpp"
header=editor/"qt_editor_window.hpp"
cmake_path=root/"engine/northstarcpp/CMakeLists.txt"
out=editor/"qt_editor_window_inspector_workspace.cpp"
assert not out.exists(), f"Refusing overwrite {out}"
original=src.read_text(encoding="utf-8")
hdr=header.read_text(encoding="utf-8")
cmake=cmake_path.read_text(encoding="utf-8")
assert "setupInspectorAndWorkspace" not in original + hdr + cmake
start=original.index('    auto const inspector=createInspectorScroll(detailsDock_);')
tail=original.index('    statusBar()->showMessage(',start)
assert start<tail
assert original.rstrip().endswith('}\n}') or original.rstrip().endswith('}\r\n}')
assert original[original.rfind('    statusBar()->showMessage('):].count('}')==2
header_pos=original.index("namespace {\nQDoubleSpinBox* scalar")
method_pos=original.index("void QtEditorWindow::setupUi()")
helpers=original[header_pos:method_pos]
assert helpers.count('QDoubleSpinBox* scalar(')==1
prefix=original[:header_pos]
# This method owns its inspector temporary variables and all following signal wiring.
# The only prior local references are viewMenu, assetMenu and configureCharacter.
moved=original[start:original.rfind('\n}\n}')].rstrip()
wrapper='''    setupInspectorAndWorkspace(viewMenu,assetMenu,configureCharacter);
'''
setup=(original[:start]+wrapper+'}\n}\n')
setup=setup[:header_pos]+setup[method_pos:]
# The new translation unit reuses Qt setup's same include environment and helper scope.
new_file=prefix+helpers+'''void QtEditorWindow::setupInspectorAndWorkspace(
    QMenu* viewMenu,QMenu* assetMenu,QAction* configureCharacter) {
'''+moved+'''
}
} // namespace northstarcpp::editor
'''
assert '    auto const inspector=' not in setup
assert new_file.count('    auto const inspector=')==1
anchor='    void setupUi();'
assert hdr.count(anchor)==1
hdr=hdr.replace(anchor,anchor+'\n    void setupInspectorAndWorkspace(QMenu* viewMenu,QMenu* assetMenu,QAction* configureCharacter);',1)
hdr=hdr.replace('class QAction;','class QAction;\nclass QMenu;',1)
anchor='    modules/editor/qt_editor_window_setup.cpp'
assert cmake.count(anchor)==1
cmake=cmake.replace(anchor,anchor+'\n    modules/editor/qt_editor_window_inspector_workspace.cpp',1)
out.write_text(new_file,encoding="utf-8")
src.write_text(setup,encoding="utf-8")
header.write_text(hdr,encoding="utf-8")
cmake_path.write_text(cmake,encoding="utf-8")
print("Qt workspace extracted:",len(moved.splitlines()),"lines; setupUi:",len(setup.splitlines()),"lines")
