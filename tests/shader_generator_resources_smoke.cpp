// Check the Shader Generator EXE has its own icon and native Windows FileInfo.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
#include "resource_ids.h"

namespace {
unsigned read_u16(unsigned char const* data)
{
    return unsigned(data[0]) | (unsigned(data[1]) << 8);
}

bool verify_icon(char const* path)
{
    HMODULE module = LoadLibraryExA(path, nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) {
        std::fprintf(stderr, "Cannot read Windows resources from %s\n", path);
        return false;
    }
    HRSRC group = FindResourceA(module, MAKEINTRESOURCEA(IDI_SHADER_GENERATOR), RT_GROUP_ICON);
    if (!group || FindResourceA(module, MAKEINTRESOURCEA(IDI_NORTHSTARCPP_APP), RT_GROUP_ICON)
               || FindResourceA(module, MAKEINTRESOURCEA(IDI_BUGTRAP_APP), RT_GROUP_ICON)) {
        std::fprintf(stderr, "Missing Shader Generator icon or wrong application icon embedded\n");
        FreeLibrary(module);
        return false;
    }
    HGLOBAL handle = LoadResource(module, group);
    DWORD const length = SizeofResource(module, group);
    auto const* data = static_cast<unsigned char const*>(handle ? LockResource(handle) : nullptr);
    if (!data || length < 6) {
        FreeLibrary(module);
        return false;
    }
    unsigned const count = read_u16(data + 4);
    bool valid = count >= 3 && length >= 6 + 14 * count;
    std::set<unsigned> dimensions;
    for (unsigned i = 0; valid && i < count; ++i) {
        auto const* entry = data + 6 + 14 * i;
        unsigned const width = entry[0] ? entry[0] : 256;
        unsigned const height = entry[1] ? entry[1] : 256;
        unsigned const image_id = read_u16(entry + 12);
        HRSRC image = FindResourceA(module, MAKEINTRESOURCEA(image_id), RT_ICON);
        dimensions.insert(width);
        if (width != height || !image || SizeofResource(module, image) == 0)
            valid = false;
    }
    valid = valid && dimensions.count(16) && dimensions.count(32) && dimensions.count(256);
    FreeLibrary(module);
    if (!valid)
        std::fprintf(stderr, "Shader Generator icon frames are missing or invalid\n");
    return valid;
}

bool verify_file_info(char const* path, char const* expected_version)
{
    DWORD ignored = 0;
    DWORD const size = GetFileVersionInfoSizeA(path, &ignored);
    if (!size) {
        std::fprintf(stderr, "Shader Generator VERSIONINFO is missing\n");
        return false;
    }
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoA(path, 0, size, data.data()))
        return false;
    void* fixed = nullptr;
    UINT fixed_length = 0;
    if (!VerQueryValueA(data.data(), "\\", &fixed, &fixed_length)
        || fixed_length < sizeof(VS_FIXEDFILEINFO)
        || static_cast<VS_FIXEDFILEINFO*>(fixed)->dwSignature != 0xFEEF04BD)
        return false;
    struct Field { char const* name; char const* value; };
    Field const fields[] = {
        {"CompanyName", "Kayla's Systems"},
        {"FileDescription", "NorthStar Shader Generator"},
        {"FileVersion", expected_version},
        {"InternalName", "shader_generator"},
        {"OriginalFilename", "shader_generator.exe"},
        {"ProductName", "NorthStarCpp Engine"},
        {"ProductVersion", expected_version},
        {"LegalCopyright", "Copyright (C) 2026 Kayla's Systems - Kayla Verner"},
    };
    for (Field const& field : fields) {
        char key[128] = {};
        std::snprintf(key, sizeof(key), "\\StringFileInfo\\040904B0\\%s", field.name);
        void* value = nullptr;
        UINT length = 0;
        if (!VerQueryValueA(data.data(), key, &value, &length) || !length
            || std::strcmp(static_cast<char const*>(value), field.value) != 0) {
            std::fprintf(stderr, "Missing or incorrect FileInfo: %s\n", field.name);
            return false;
        }
    }
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    if (!verify_icon(argv[1]) || !verify_file_info(argv[1], argv[2]))
        return 1;
    std::puts("Shader Generator icon and Windows FileInfo verified");
    return 0;
}
