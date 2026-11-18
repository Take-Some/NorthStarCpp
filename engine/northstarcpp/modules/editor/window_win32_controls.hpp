#pragma once
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
