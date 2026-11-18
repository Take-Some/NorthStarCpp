#include "../engine/northstarcpp/modules/editor/window_runtime_message_pump.hpp"
#include <stdexcept>
int main() {
    MSG message{};
    (void)PeekMessageW(&message,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    PostQuitMessage(42);
    if(northstarcpp::runtime_win32::run_message_loop(
           nullptr,nullptr,false)!=42)
        throw std::runtime_error("Win32 message pump lost exit code");
}
