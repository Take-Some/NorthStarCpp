#!/usr/bin/env python3
"""Extract Qt outliner panel into independently compiled member method."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
ed=root/"engine/northstarcpp/modules/editor"
src=ed/"qt_editor_window_setup.cpp"
hdr=ed/"qt_editor_window.hpp"
cmake_path=root/"engine/northstarcpp/CMakeLists.txt"
out=ed/"qt_editor_window_outliner.cpp"
assert not out.exists()
data=src.read_text(encoding="utf8")
head=hdr.read_text(encoding="utf8")
cmake=cmake_path.read_text(encoding="utf8")
assert "setupOutlinerPanel" not in data+head+cmake
start=data.index('    outlinerDock_=new QDockWidget("World Outliner",this);')
end=data.index('    detailsDock_=new QDockWidget("Details | Properties",this);',start)
block=data[start:end]
assert "addButton->setMenu(makePlaceActorMenu(addButton));" in block
assert "placeActorsDock_" in block
prefix=data[:data.index('namespace northstarcpp::editor {')]
# The callback is used just once during widget creation and preserves Qt menu ownership.
new_file=prefix+'''namespace northstarcpp::editor {
namespace fs=std::filesystem;
using namespace window_detail;
void QtEditorWindow::setupOutlinerPanel(
    std::function<QMenu*(QWidget*)> const& makePlaceActorMenu) {
'''+block+'''
}
} // namespace northstarcpp::editor
'''
data=data[:start]+"    setupOutlinerPanel(makePlaceActorMenu);\n"+data[end:]
anchor='    void setupUi();'
assert head.count(anchor)==1
head=head.replace(anchor,anchor+'\n    void setupOutlinerPanel(std::function<QMenu*(QWidget*)> const& makePlaceActorMenu);',1)
anchor='    modules/editor/qt_editor_window_setup.cpp'
assert cmake.count(anchor)==1
cmake=cmake.replace(anchor,anchor+'\n    modules/editor/qt_editor_window_outliner.cpp',1)
out.write_text(new_file,encoding="utf8")
src.write_text(data,encoding="utf8")
hdr.write_text(head,encoding="utf8")
cmake_path.write_text(cmake,encoding="utf8")
print("Qt outliner extracted:",len(block.splitlines()),"lines")
