// Native NorthStarCpp fatal-error capture (Windows).
// The GUI is deliberately out of process: never render UI from a faulted engine.
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
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

namespace northstarcpp::diagnostics {
namespace {
std::atomic_flag handling = ATOMIC_FLAG_INIT;
std::atomic_flag installed = ATOMIC_FLAG_INIT;
std::array<wchar_t, 4096> directory{};
std::array<wchar_t, 4096> reporter{};
std::array<wchar_t, 4096> executable{};

bool directory_exists(wchar_t const* path) noexcept {
    auto const attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// All filesystem work happens during normal initialization, never on a crashed thread.
void prepare_paths() noexcept {
    DWORD n = GetModuleFileNameW(nullptr, executable.data(),
        static_cast<DWORD>(executable.size()));
    if (n > 0 && n < executable.size()) {
        auto const* slash = wcsrchr(executable.data(), L'\\');
        if (slash) {
            size_t length = static_cast<size_t>(slash - executable.data());
            if (length + 13 < reporter.size()) {
                wmemcpy(reporter.data(), executable.data(), length);
                wcscpy_s(reporter.data() + length, reporter.size() - length,
                    L"\\bugtrap.exe");
            }
        }
    }
    std::array<wchar_t, 2048> base{};
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", base.data(),
        static_cast<DWORD>(base.size()));
    if (!n || n >= base.size()) {
        n = GetTempPathW(static_cast<DWORD>(base.size()), base.data());
        if (!n || n >= base.size()) return;
    }
    try {
        std::filesystem::path const path = std::filesystem::path(base.data()) /
            L"KaylasSystems" / L"NorthStarCpp" / L"Crashes";
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (!error && path.native().size() < directory.size())
            wcscpy_s(directory.data(), directory.size(), path.c_str());
    } catch (...) { /* Still allow application startup. */ }
}

void show_reporter(wchar_t const* report) noexcept {
    if (!reporter[0] || GetFileAttributesW(reporter.data()) == INVALID_FILE_ATTRIBUTES)
        return;
    wchar_t command[8192]{};
    wchar_t const* silent = L"";
    wchar_t setting[8]{};
    if (GetEnvironmentVariableW(L"NORTHSTARCPP_BUGTRAP_SILENT", setting, 8))
        silent = L" --silent";
    int const n = swprintf(command, sizeof(command) / sizeof(command[0]),
        L"\"%ls\" --report \"%ls\"%ls", reporter.data(), report, silent);
    if (n <= 0 || static_cast<size_t>(n) >= std::size(command)) return;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(reporter.data(), command, nullptr, nullptr,
        FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

void save_report(wchar_t const* path, SYSTEMTIME const& utc, DWORD code,
                 void const* address, bool dump_saved, DWORD dump_error) noexcept {
    HANDLE output = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) return;
    char filename[1024]{};
    WideCharToMultiByte(CP_UTF8, 0, executable.data(), -1, filename,
        static_cast<int>(sizeof(filename)), nullptr, nullptr);
    char text[2500]{};
    char const* const reason=code==0xE0000002u ?
        "SIGABRT / fatal native assertion or abort" :
        code==0xE0000001u ? "Unhandled C++ terminate" :
        "Native structured exception";
    int const length = std::snprintf(text, sizeof(text),
        "NorthStarCpp Crash Report\r\n"
        "UTC: %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n"
        "Application: %s\r\n"
        "Process ID: %lu\r\nThread ID: %lu\r\n"
        "Exception code: 0x%08lX\r\n"
        "Exception reason: %s\r\n"
        "Exception address: 0x%llX\r\n"
        "Minidump: %s\r\n"
        "Minidump error: %lu\r\n"
        "Privacy: Local only. No automatic upload.\r\n",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond,
        utc.wMilliseconds, filename,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()),
        static_cast<unsigned long>(code),reason,
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)),
        dump_saved ? "saved" : "not available",
        static_cast<unsigned long>(dump_error));
    if (length > 0) {
        DWORD written = 0;
        WriteFile(output, text,
            static_cast<DWORD>(length < static_cast<int>(sizeof(text)) ?
                length : sizeof(text) - 1), &written, nullptr);
        FlushFileBuffers(output);
    }
    CloseHandle(output);
}

LONG WINAPI crash_filter(EXCEPTION_POINTERS* exception) noexcept {
    if (handling.test_and_set(std::memory_order_acq_rel))
        return EXCEPTION_EXECUTE_HANDLER;
    if (!directory_exists(directory.data()))
        return EXCEPTION_EXECUTE_HANDLER;

    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    wchar_t stem[128]{};
    swprintf(stem, std::size(stem),
        L"crash-%04u%02u%02u-%02u%02u%02u-%03u-%lu-%lu",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute,
        utc.wSecond, utc.wMilliseconds,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()));
    wchar_t report[4096]{};
    wchar_t dump[4096]{};
    if (swprintf(report, std::size(report), L"%ls\\%ls.txt", directory.data(), stem) < 0 ||
        swprintf(dump, std::size(dump), L"%ls\\%ls.dmp", directory.data(), stem) < 0)
        return EXCEPTION_EXECUTE_HANDLER;

    bool saved = false;
    DWORD dump_error = 0;
    HANDLE file = CreateFileW(dump, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = exception;
        info.ClientPointers = FALSE;
        // Preserve a synthetic ExceptionStream for SIGABRT/std::terminate too.
        // Previously passing nullptr discarded the very context required to
        // diagnose fatal V8/native callbacks from the captured minidump.
        // The synthetic RIP points at our signal handler (not the original
        // abort caller); unwind its thread stack to find the initiating call.
        saved = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
            static_cast<MINIDUMP_TYPE>(
                MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
            exception ? &info : nullptr, nullptr, nullptr) != FALSE;
        if (!saved) dump_error = GetLastError();
        CloseHandle(file);
        if (!saved) DeleteFileW(dump);
    } else {
        dump_error = GetLastError();
    }
    DWORD const code = exception && exception->ExceptionRecord ?
        exception->ExceptionRecord->ExceptionCode : 0xE0000001u;
    void const* const address = exception && exception->ExceptionRecord ?
        exception->ExceptionRecord->ExceptionAddress : nullptr;
    save_report(report, utc, code, address, saved, dump_error);
    // CreateProcessW is the last operation performed by the faulted process.
    show_reporter(report);
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void on_terminate() noexcept {
    // Do not rely on the OS dispatching our unhandled filter after std::terminate.
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = 0xE0000001u; // Unhandled C++ exception / terminate.
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
#if defined(_M_X64) || defined(__x86_64__)
    record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
#elif defined(_M_IX86) || defined(__i386__)
    record.ExceptionAddress = reinterpret_cast<void*>(context.Eip);
#else
    record.ExceptionAddress = nullptr;
#endif
    EXCEPTION_POINTERS pointers{&record, &context};
    crash_filter(&pointers);
    TerminateProcess(GetCurrentProcess(), 3);
    std::abort();
}
} // namespace

static void on_abort(int) noexcept {
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = 0xE0000002u;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
#if defined(_M_X64) || defined(__x86_64__)
    record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
#elif defined(_M_IX86) || defined(__i386__)
    record.ExceptionAddress = reinterpret_cast<void*>(context.Eip);
#else
    record.ExceptionAddress = nullptr;
#endif
    EXCEPTION_POINTERS pointers{&record, &context};
    crash_filter(&pointers);
    TerminateProcess(GetCurrentProcess(), 134);
    std::_Exit(134);
}

void install_crash_handler() noexcept {
    if (installed.test_and_set(std::memory_order_acq_rel)) return;
    prepare_paths();
    SetUnhandledExceptionFilter(crash_filter);
    std::set_terminate(on_terminate);
    std::signal(SIGABRT, on_abort);
}
} // namespace northstarcpp::diagnostics
