#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[2]
editor=root/"engine/northstarcpp/modules/editor"
src=editor/"window.cpp"
cmake_path=root/"engine/northstarcpp/CMakeLists.txt"
data=src.read_bytes().decode("utf-8")
cmake=cmake_path.read_bytes().decode("utf-8")
start=data.index("std::vector<std::string> script_args(std::string_view json)")
end=data.index("// Projects may place their startup scene",start)
block=data[start:end]
for needle in ("CharacterAssetSpec parse_script_character","UiDocument parse_script_ui_document","std::size_t script_index","double script_number"):
    assert block.count(needle)==1
assert "window_runtime_script_contracts.hpp" not in data
assert "window_runtime_script_contracts.cpp" not in cmake
header='''#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
namespace northstarcpp {
struct UiDocument;
struct CharacterAssetSpec;
namespace runtime_script_contracts {
std::vector<std::string> script_args(std::string_view json);
UiDocument parse_script_ui_document(std::string_view args_json);
std::size_t script_index(std::string const& value);
double script_number(std::string const& value);
CharacterAssetSpec parse_script_character(std::string_view json);
}
}
'''
impl='''#include "window_runtime_script_contracts.hpp"
#include "../viewport/viewport.hpp"
#include <northstarcpp/scripting.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
namespace northstarcpp::runtime_script_contracts {
namespace fs = std::filesystem;
'''+block.replace("\r\n","\n")+''' } // namespace northstarcpp::runtime_script_contracts
'''
newline="\r\n" if "\r\n" in data else "\n"
aliases='''using runtime_script_contracts::script_args;
using runtime_script_contracts::parse_script_ui_document;
using runtime_script_contracts::script_index;
using runtime_script_contracts::script_number;
using runtime_script_contracts::parse_script_character;

'''.replace("\n",newline)
updated=data[:start]+aliases+data[end:]
include_anchor='#include "window_runtime_options.hpp"'
assert updated.count(include_anchor)==1
updated=updated.replace(include_anchor,include_anchor+newline+'#include "window_runtime_script_contracts.hpp"',1)
anchor="    modules/editor/window_runtime_options.cpp"
assert cmake.count(anchor)==1
cmake=cmake.replace(anchor,anchor+("\r\n" if "\r\n" in cmake else "\n")+"    modules/editor/window_runtime_script_contracts.cpp",1)
out={editor/"window_runtime_script_contracts.hpp":header,editor/"window_runtime_script_contracts.cpp":impl}
for p in out: assert not p.exists(),str(p)
for p,v in out.items(): p.write_bytes(v.encode("utf-8"))
src.write_bytes(updated.encode("utf-8"))
cmake_path.write_bytes(cmake.encode("utf-8"))
print("Extracted",len(block.splitlines()),"script-contract lines from Player.")
