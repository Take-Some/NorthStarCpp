#!/usr/bin/env python3
"""Separate Qt panel construction from signal wiring without changing their order."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
ed=root/"engine/northstarcpp/modules/editor"
src=ed/"qt_editor_window_inspector_workspace.cpp"
header=ed/"qt_editor_window.hpp"
cmake_path=root/"engine/northstarcpp/CMakeLists.txt"
out=ed/"qt_editor_window_workspace_signals.cpp"
assert not out.exists()
s=src.read_text(encoding="utf-8")
h=header.read_text(encoding="utf-8")
c=cmake_path.read_text(encoding="utf-8")
assert 'wireWorkspaceSignals(' not in s+h+c
start=s.index("    connect(outlinerSearch_,&QLineEdit::textChanged")
end=s.rfind("\n}\n} // namespace northstarcpp::editor")
assert end>start
block=s[start:end]
assert 'connect(apply,&QPushButton::clicked' in block
# Original function's complete set of signal connections stays intact.
prefix=s[:s.index('namespace {\nQDoubleSpinBox* scalar')]
new_file=prefix+'''void QtEditorWindow::wireWorkspaceSignals(QPushButton* apply) {
'''+block+'''
}
} // namespace northstarcpp::editor
'''
s=s[:start]+"    wireWorkspaceSignals(apply);"+s[end:]
anchor="    void setupInspectorAndWorkspace(QMenu* viewMenu,QMenu* assetMenu,QAction* configureCharacter);"
assert h.count(anchor)==1
h=h.replace(anchor,anchor+"\n    void wireWorkspaceSignals(QPushButton* apply);",1)
anchor='    modules/editor/qt_editor_window_inspector_workspace.cpp'
assert c.count(anchor)==1
c=c.replace(anchor,anchor+'\n    modules/editor/qt_editor_window_workspace_signals.cpp',1)
out.write_text(new_file,encoding="utf-8")
src.write_text(s,encoding="utf-8")
header.write_text(h,encoding="utf-8")
cmake_path.write_text(c,encoding="utf-8")
print("Qt signals moved:",len(block.splitlines()),"lines")
