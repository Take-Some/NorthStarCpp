#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace northstarcpp::runtime_win32 {
// Runs exactly one Win32 message loop. Does not own the HWND/accelerators.
// App remains responsible for resource teardown after this function returns.
int run_message_loop(HWND owner,HACCEL accelerators,bool editor);
}
