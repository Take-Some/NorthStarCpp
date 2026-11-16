// Intentional child-process crashes: NEVER run in the editor/player process.
#include "../engine/northstarcpp/modules/shared/crash_handler.hpp"
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <cstdlib>
#include <exception>

int main(int argc, char** argv) {
    northstarcpp::diagnostics::install_crash_handler();
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "terminate") == 0) {
        std::terminate();
    }
    if (std::strcmp(argv[1], "abort") == 0) {
        std::abort();
    }
    if (std::strcmp(argv[1], "seh") == 0) {
        RaiseException(0xE000B007u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 99;
    }
    return 2;
}
