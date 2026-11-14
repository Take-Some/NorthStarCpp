// Verify the specific embedded icon of each NorthStarCpp Windows application.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <filesystem>
#include <set>
#include "resource_ids.h"

namespace {
unsigned read_u16(unsigned char const* data)
{
    return unsigned(data[0]) | (unsigned(data[1]) << 8);
}

bool verify(wchar_t const* path)
{
    auto const filename = std::filesystem::path(path).filename();
    bool const is_bugtrap = filename == L"bugtrap.exe";
    bool const is_editor = filename == L"northstarcpp_editor_qt.exe";
    unsigned const expected_id = is_bugtrap ? IDI_BUGTRAP_APP : IDI_NORTHSTARCPP_APP;
    unsigned const unexpected_id = is_bugtrap ? IDI_NORTHSTARCPP_APP : IDI_BUGTRAP_APP;
    std::set<unsigned> const expected_dimensions = is_bugtrap
        ? std::set<unsigned>{16, 32, 48}
        : is_editor ? std::set<unsigned>{16, 24, 32, 48, 64, 128, 256}
        : std::set<unsigned>{64, 128, 256};

    HMODULE const module = LoadLibraryExW(path, nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) {
        std::fwprintf(stderr, L"Cannot load EXE resources: %ls\n", path);
        return false;
    }
    auto const group = FindResourceW(module, MAKEINTRESOURCEW(expected_id), RT_GROUP_ICON);
    auto const wrong = FindResourceW(module, MAKEINTRESOURCEW(unexpected_id), RT_GROUP_ICON);
    if (!group || wrong) {
        std::fwprintf(stderr, L"Incorrect application icon in: %ls\n", path);
        FreeLibrary(module);
        return false;
    }
    auto const resource = LoadResource(module, group);
    auto const length = SizeofResource(module, group);
    auto const* data = static_cast<unsigned char const*>(LockResource(resource));
    if (!data || length < 6) {
        FreeLibrary(module);
        return false;
    }
    unsigned const count = read_u16(data + 4);
    std::set<unsigned> dimensions;
    bool valid = count == expected_dimensions.size() && length >= 6 + 14 * count;
    for (unsigned i = 0; valid && i < count; ++i) {
        auto const* entry = data + 6 + 14 * i;
        unsigned const width = entry[0] ? entry[0] : 256;
        unsigned const height = entry[1] ? entry[1] : 256;
        unsigned const icon_id = read_u16(entry + 12);
        auto const image = FindResourceW(module, MAKEINTRESOURCEW(icon_id), RT_ICON);
        dimensions.insert(width);
        if (width != height || !image || SizeofResource(module, image) == 0)
            valid = false;
    }
    valid = valid && dimensions == expected_dimensions;
    FreeLibrary(module);
    if (!valid) {
        std::fwprintf(stderr, L"Unexpected application icon frames in: %ls\n", path);
        return false;
    }
    std::wprintf(L"Verified %ls icon in %ls\n",
        is_bugtrap ? L"Bug.ico" : is_editor ? L"northstar-editor.ico" : L"logoStar.ico", path);
    return true;
}
} // namespace

int main()
{
    int argc = 0;
    auto** args = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!args || argc < 2)
        return 2;
    bool okay = true;
    for (int index = 1; index < argc; ++index)
        okay = verify(args[index]) && okay;
    LocalFree(args);
    return okay ? 0 : 1;
}
