#include "window_runtime_message_pump.hpp"
#include <stdexcept>
namespace northstarcpp::runtime_win32 {
int run_message_loop(HWND owner,HACCEL accelerators,bool editor) {
    MSG message{};
    for (BOOL next; (next=GetMessageW(&message,nullptr,0,0))!=0;) {
        if(next<0)
            throw std::runtime_error("Application message loop failed");
        if(editor && accelerators &&
           TranslateAcceleratorW(owner,accelerators,&message))
            continue;
        if(editor && IsDialogMessageW(owner,&message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
}
