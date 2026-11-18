#!/usr/bin/env python3
"""Move Win32 UTF-8/text/number controls to an independently built module."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
editor=root/"engine/northstarcpp/modules/editor"
src=editor/"window.cpp"
cmake_path=root/"engine/northstarcpp/CMakeLists.txt"
h=editor/"window_win32_controls.hpp"
cpp=editor/"window_win32_controls.cpp"
for p in (h,cpp):assert not p.exists()
s=src.read_text(encoding='utf8')
c=cmake_path.read_text(encoding='utf8')
begin=s.index("std::wstring to_wide(std::string_view text)")
end=s.index("using runtime_options::Options;",begin)
body=s[begin:end].replace("\r\n","\n")
for name in ["to_wide","to_utf8","get_text","parse_number","set_number"]:
 assert body.count(name+"(")>=1
assert s.count('#include "window_runtime_options.hpp"')==1
assert c.count("    modules/editor/window_runtime_project_files.cpp")==1
head='''#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <string_view>
namespace northstarcpp::win32_controls {
std::wstring to_wide(std::string_view);
std::string to_utf8(std::wstring_view);
std::wstring get_text(HWND);
float parse_number(HWND);
void set_number(HWND,float);
}
'''
impl='''#include "window_win32_controls.hpp"
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace northstarcpp::win32_controls {
'''+body+''' } // namespace northstarcpp::win32_controls
'''
aliases='''using win32_controls::to_wide;
using win32_controls::to_utf8;
using win32_controls::get_text;
using win32_controls::parse_number;
using win32_controls::set_number;

'''
s=s[:begin]+aliases+s[end:]
s=s.replace('#include "window_runtime_options.hpp"',
            '#include "window_runtime_options.hpp"\n#include "window_win32_controls.hpp"',1)
c=c.replace('    modules/editor/window_runtime_project_files.cpp',
            '    modules/editor/window_runtime_project_files.cpp\n    modules/editor/window_win32_controls.cpp',1)
h.write_text(head,encoding='utf8')
cpp.write_text(impl,encoding='utf8')
src.write_text(s,encoding='utf8')
cmake_path.write_text(c,encoding='utf8')
print(f'Extracted {len(body.splitlines())} Win32 control helper lines.')
