// NorthStarCpp BugTrap reporter; 2026 Kayla's Systems — Kayla Verner.
// Separate native GUI process, started only after the engine writes a crash report.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "resource_ids.h"
#include <filesystem>
#include <string>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 2;
    std::wstring report;
    bool silent = false;
    bool self_test = argc == 2 && std::wstring(argv[1]) == L"--self-test";
    if (!self_test && argc >= 3 && argc <= 4 && std::wstring(argv[1]) == L"--report") {
        report = argv[2];
        if (argc == 4) {
            if (std::wstring(argv[3]) != L"--silent") {
                LocalFree(argv);
                return 2;
            }
            silent = true;
        }
    }
    LocalFree(argv);
    if (self_test) {
        bool const has_icon = FindResourceW(instance, MAKEINTRESOURCEW(IDI_BUGTRAP_APP),
                                             RT_GROUP_ICON) != nullptr;
        bool const has_fileinfo = FindResourceW(instance, MAKEINTRESOURCEW(1),
                                                 RT_VERSION) != nullptr;
        return has_icon && has_fileinfo ? 0 : 5;
    }
    if (report.empty())
        return 2;

    DWORD const attributes = GetFileAttributesW(report.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return 3;
    if (silent)
        return 0;

    std::wstring const message =
        L"NorthStarCpp encountered an unexpected error.\n\n"
        L"A diagnostic report has been saved locally:\n" + report +
        L"\n\nA minidump may also be available in the same folder. "
        L"Reports are not uploaded automatically.\n\nOpen the crash-report folder?";
    MSGBOXPARAMSW options{};
    options.cbSize = sizeof(options);
    options.hInstance = instance;
    options.lpszText = message.c_str();
    options.lpszCaption = L"NorthStarCpp - BugTrap";
    options.dwStyle = MB_YESNO | MB_USERICON | MB_SETFOREGROUND;
    options.lpszIcon = MAKEINTRESOURCEW(IDI_BUGTRAP_APP);
    int const answer = MessageBoxIndirectW(&options);
    if (answer == IDYES) {
        std::wstring const folder = std::filesystem::path(report).parent_path().wstring();
        ShellExecuteW(nullptr, L"explore", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return 0;
}
