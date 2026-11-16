#!/usr/bin/env python3
"""Extract Player virtual-file, save-path and serialization helpers."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
editor=root/"engine/northstarcpp/modules/editor"
src=editor/"window.cpp"
cmake_path=root/"engine/northstarcpp/CMakeLists.txt"
data=src.read_bytes().decode("utf-8")
cmake=cmake_path.read_bytes().decode("utf-8")
start=data.index("// Projects may place their startup scene")
end=data.index("class App {",start)
block=data[start:end]
for needle in ("void validate_script_virtual_path", "fs::path validated_script_file", "fs::path script_project_root", "SaveGameStore project_save_store", "std::string script_json_quote", "std::string script_object_payload"):
    assert block.count(needle)==1
assert "window_runtime_project_files.hpp" not in data
assert "window_runtime_project_files.cpp" not in cmake
header='''#pragma once
#include "window_runtime_options.hpp"
#include <northstarcpp/scene.hpp>
#include <northstarcpp/save_game.hpp>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
namespace northstarcpp::runtime_project_files {
void validate_script_virtual_path(std::filesystem::path const&);
std::filesystem::path validated_script_file(std::filesystem::path const&,std::filesystem::path const&);
std::filesystem::path script_project_root(std::filesystem::path const&);
SaveGameStore project_save_store(runtime_options::Options const&,std::filesystem::path const&);
std::string script_json_quote(std::string_view);
std::string script_object_payload(SceneObject const&,std::size_t,float dt=-1.f);
}
'''
block=block.replace("std::size_t index,float dt=-1.f)","std::size_t index,float dt)")
assert "float dt=-1.f" not in block
impl='''#include "window_runtime_project_files.hpp"
#include <northstarcpp/scripting.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace northstarcpp::runtime_project_files {
namespace fs = std::filesystem;
using runtime_options::Options;
'''+block.replace("\r\n","\n")+''' } // namespace northstarcpp::runtime_project_files
'''
newline="\r\n" if "\r\n" in data else "\n"
aliases='''using runtime_project_files::validate_script_virtual_path;
using runtime_project_files::validated_script_file;
using runtime_project_files::script_project_root;
using runtime_project_files::project_save_store;
using runtime_project_files::script_json_quote;
using runtime_project_files::script_object_payload;

'''.replace("\n",newline)
updated=data[:start]+aliases+data[end:]
include_anchor='#include "window_runtime_script_contracts.hpp"'
assert updated.count(include_anchor)==1
updated=updated.replace(include_anchor,include_anchor+newline+'#include "window_runtime_project_files.hpp"',1)
anchor="    modules/editor/window_runtime_script_contracts.cpp"
assert cmake.count(anchor)==1
cmake=cmake.replace(anchor,anchor+("\r\n" if "\r\n" in cmake else "\n")+"    modules/editor/window_runtime_project_files.cpp",1)
out={editor/"window_runtime_project_files.hpp":header,editor/"window_runtime_project_files.cpp":impl}
for p in out: assert not p.exists(),str(p)
for p,v in out.items(): p.write_bytes(v.encode("utf-8"))
src.write_bytes(updated.encode("utf-8"))
cmake_path.write_bytes(cmake.encode("utf-8"))
print("Extracted",len(block.splitlines()),"project path/save/payload lines.")
