#include <northstarcpp/input.hpp>
#include <windows.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    try {
        northstarcpp::InputDevice input;
        auto const mode = std::string(argv[1]);
        if (mode == "mock") {
            if (input.provider_name() != "Mock Input") return 3;
            auto sample = input.poll();
            if (!sample.keys['W'] || !sample.gamepads[0].connected ||
                std::abs(sample.gamepads[0].left_x - 0.5f) > 0.0001f) return 4;
            input.vibrate(0, 0.5f, 0.3f);
            return 0;
        }
        if (mode != "default" || input.provider_name() != "Win32/XInput") return 5;
        input.feed_message(WM_KEYDOWN, 'W', 0);
        auto pressed = input.poll();
        if (!pressed.keys['W']) return 6;
        input.feed_message(WM_KEYUP, 'W', 0);
        if (input.poll().keys['W']) return 7;
        input.feed_message(WM_MOUSEMOVE, 0, MAKELPARAM(10, 15));
        input.feed_message(WM_MOUSEMOVE, 0, MAKELPARAM(13, 23));
        auto moved = input.poll();
        if (moved.mouse_x != 13 || moved.mouse_y != 23 ||
            moved.mouse_dx != 3 || moved.mouse_dy != 8) return 8;
        if (input.poll().mouse_dx != 0) return 9;
        input.feed_message(WM_CHAR, 'Z', 0);
        auto typed = input.poll();
        if (typed.text_count != 1 || typed.text[0] != 'Z' ||
            input.poll().text_count != 0) return 10;
        input.feed_message(WM_KEYDOWN, 'A', 0);
        input.feed_message(WM_KILLFOCUS, 0, 0);
        if (input.poll().keys['A'] || input.poll().focused) return 11;
        return 0;
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        return argc == 2 && std::string(argv[1]) == "reject" &&
            std::strstr(e.what(), "Incompatible module for engine.input") ? 0 : 12;
    }
}
