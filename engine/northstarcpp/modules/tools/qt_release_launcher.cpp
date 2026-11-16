// Copyright (C) 2026 Kayla's Systems
// Native Windows launcher for Qt tools in a private DLL directory.
// The public bin/<name>.exe is a small standalone GUI executable; the actual
// Qt-linked application lives in bin/qt_tools/<name>.exe.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace fs = std::filesystem;

std::wstring quote(std::wstring_view argument)
{
    std::wstring out=L"\"";
    std::size_t slashes=0;
    for(auto const ch:argument) {
        if(ch==L'\\') {
            ++slashes;
        }else if(ch==L'"') {
            out.append(slashes*2+1,L'\\');
            out.push_back(L'"');
            slashes=0;
        }else{
            out.append(slashes,L'\\');
            slashes=0;
            out.push_back(ch);
        }
    }
    out.append(slashes*2,L'\\');
    out.push_back(L'"');
    return out;
}

int fail(wchar_t const* message)
{
    OutputDebugStringW(message);
    MessageBoxW(nullptr,message,L"NorthStar Qt Launcher",
                MB_ICONERROR|MB_OK|MB_TASKMODAL);
    return 127;
}

fs::path location()
{
    std::array<wchar_t,32768> path{};
    DWORD const count=GetModuleFileNameW(nullptr,path.data(),
                                         static_cast<DWORD>(path.size()));
    if(count==0 || count>=path.size())return {};
    return fs::path(path.data());
}

bool permitted_name(fs::path const& path)
{
    auto const name=path.filename().wstring();
    return name==L"northstarcpp_project_manager.exe" ||
           name==L"northstarcpp_asset_browser.exe" ||
           name==L"northstarcpp_camera_timeline_editor.exe" ||
           name==L"northstarcpp_editor_qt.exe";
}

void extend_path(fs::path const& base,fs::path const& qt)
{
    auto const old_length=GetEnvironmentVariableW(L"PATH",nullptr,0);
    std::wstring previous;
    if(old_length && old_length<32768) {
        std::vector<wchar_t> buffer(old_length);
        auto const copied=GetEnvironmentVariableW(L"PATH",buffer.data(),old_length);
        if(copied && copied<old_length)
            previous.assign(buffer.data(),copied);
    }
    // Private process-level PATH only; never alter the user's global PATH.
    auto value=qt.wstring()+L";"+base.wstring();
    if(!previous.empty())value+=L";"+previous;
    SetEnvironmentVariableW(L"PATH",value.c_str());
}

} // namespace

int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int)
{
    try {
        auto const own=location();
        if(own.empty() || !permitted_name(own))
            return fail(L"Unknown NorthStar Qt launcher executable.");
        auto const base=own.parent_path();
        auto const qt=base/L"qt_tools";
        auto const child=qt/own.filename();
        if(!fs::is_regular_file(child))
            return fail(L"Qt application is missing from bin\\qt_tools.");
        auto const plugin=qt/L"platforms";
        if(!fs::is_regular_file(plugin/L"qwindows.dll"))
            return fail(L"Qt platform plugin is missing from bin\\qt_tools\\platforms.");
        extend_path(base,qt);
        SetEnvironmentVariableW(L"NORTHSTARCPP_RUNTIME_BIN",base.c_str());
        SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH",plugin.c_str());
        int argc=0;
        auto const raw=GetCommandLineW();
        auto* argv=CommandLineToArgvW(raw,&argc);
        if(!argv || argc==0)
            return fail(L"Unable to parse Windows command-line arguments.");
        std::wstring command=quote(child.wstring());
        for(int i=1;i<argc;++i) {
            command.push_back(L' ');
            command+=quote(argv[i]);
        }
        LocalFree(argv);
        std::vector<wchar_t> writable(command.begin(),command.end());
        writable.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb=sizeof(startup);
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(child.c_str(),writable.data(),nullptr,nullptr,
                           FALSE,0,nullptr,nullptr,&startup,&process))
            return fail(L"Unable to start the NorthStar Qt application.");
        CloseHandle(process.hThread);
        WaitForSingleObject(process.hProcess,INFINITE);
        DWORD status=1;
        if(!GetExitCodeProcess(process.hProcess,&status))
            status=1;
        CloseHandle(process.hProcess);
        return static_cast<int>(status);
    } catch (...) {
        return fail(L"NorthStar Qt launcher initialization failed.");
    }
}
