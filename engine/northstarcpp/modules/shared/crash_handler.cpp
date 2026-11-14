// NorthStarCpp native crash capture; 2026 Kayla's Systems — Kayla Verner.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include "crash_handler.hpp"
#include <windows.h>
#include <dbghelp.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

namespace northstarcpp::diagnostics {
namespace {

std::atomic_flag handling = ATOMIC_FLAG_INIT;

std::wstring crash_directory()
{
    std::array<wchar_t, 32768> value{};
    DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(),
        static_cast<DWORD>(value.size()));
    if (!count || count >= value.size()) {
        count = GetTempPathW(static_cast<DWORD>(value.size()), value.data());
        if (!count || count >= value.size())
            return {};
    }
    std::filesystem::path const path = std::filesystem::path(value.data()) /
        L"KaylasSystems" / L"NorthStarCpp" / L"Crashes";
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return ec ? std::wstring{} : path.wstring();
}

void save_text(std::wstring const& file, char const* text) noexcept
{
    HANDLE output = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(output, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
    CloseHandle(output);
}

void open_reporter(std::wstring const& report) noexcept
{
    std::array<wchar_t, 32768> executable{};
    DWORD const length = GetModuleFileNameW(nullptr, executable.data(),
        static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size())
        return;
    std::filesystem::path const reporter =
        std::filesystem::path(executable.data()).parent_path() / L"bugtrap.exe";
    if (GetFileAttributesW(reporter.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;

    std::wstring command = L"\"" + reporter.wstring() + L"\" --report \"" +
        report + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(reporter.c_str(), command.data(), nullptr, nullptr,
        FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

LONG WINAPI crash_filter(EXCEPTION_POINTERS* exception) noexcept
{
    if (handling.test_and_set())
        return EXCEPTION_EXECUTE_HANDLER;

    try {
        std::wstring const directory = crash_directory();
        if (!directory.empty()) {
            SYSTEMTIME utc{};
            GetSystemTime(&utc);
            wchar_t stem[128]{};
            swprintf(stem, 128, L"crash-%04u%02u%02u-%02u%02u%02u-%03u-%lu-%lu",
                utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute,
                utc.wSecond, utc.wMilliseconds,
                static_cast<unsigned long>(GetCurrentProcessId()),
                static_cast<unsigned long>(GetCurrentThreadId()));

            std::filesystem::path const root(directory);
            std::wstring const dump = (root / (std::wstring(stem) + L".dmp")).wstring();
            std::wstring const report = (root / (std::wstring(stem) + L".txt")).wstring();
            bool dump_written = false;
            HANDLE file = CreateFileW(dump.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION details{};
                details.ThreadId = GetCurrentThreadId();
                details.ExceptionPointers = exception;
                details.ClientPointers = FALSE;
                dump_written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                    file, MiniDumpNormal, exception ? &details : nullptr,
                    nullptr, nullptr) != FALSE;
                CloseHandle(file);
                if (!dump_written)
                    DeleteFileW(dump.c_str());
            }

            unsigned long code = exception && exception->ExceptionRecord
                ? exception->ExceptionRecord->ExceptionCode : 0;
            char buffer[1024]{};
            std::snprintf(buffer, sizeof(buffer),
                "NorthStarCpp native crash report\r\n"
                "UTC: %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n"
                "Process ID: %lu\r\nThread ID: %lu\r\n"
                "Exception code: 0x%08lX\r\n"
                "Minidump: %s\r\n"
                "Files are stored locally; no automatic upload.\r\n",
                utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute,
                utc.wSecond, utc.wMilliseconds,
                static_cast<unsigned long>(GetCurrentProcessId()),
                static_cast<unsigned long>(GetCurrentThreadId()),
                code, dump_written ? "saved" : "not available");
            save_text(report, buffer);
            open_reporter(report);
        }
    } catch (...) {
        // Do not throw a second exception while processing the first crash.
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void on_terminate() noexcept
{
    // Uncaught C++ exceptions must also reach the native crash reporter.
    RaiseException(0xE0000001u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    TerminateProcess(GetCurrentProcess(), 3);
    std::abort();
}

} // namespace

void install_crash_handler() noexcept
{
    SetUnhandledExceptionFilter(crash_filter);
    std::set_terminate(on_terminate);
}

} // namespace northstarcpp::diagnostics
