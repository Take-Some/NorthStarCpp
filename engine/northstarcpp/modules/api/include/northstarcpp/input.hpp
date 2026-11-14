// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#pragma once
#include <memory>
#include <string>
#include <northstarcpp/input_plugin.h>

namespace northstarcpp {

class InputDevice {
public:
    explicit InputDevice(uintptr_t native_window = 0);
    ~InputDevice();
    InputDevice(InputDevice const&) = delete;
    InputDevice& operator=(InputDevice const&) = delete;

    std::string provider_name() const;
    std::string provider_version() const;
    void feed_message(uint32_t message, uintptr_t wparam, intptr_t lparam);
    NorthStarInputState poll();
    void vibrate(uint32_t gamepad, float left, float right);
private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace northstarcpp
