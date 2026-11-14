// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#include <northstarcpp/input_plugin.h>
#include "module_provider.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <xinput.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace northstarcpp::module_provider;
using GetPadState = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
using SetPadState = DWORD (WINAPI*)(DWORD, XINPUT_VIBRATION*);

void normalize_stick(SHORT sx, SHORT sy, float deadzone, float& x, float& y)
{
    const float fx = sx < 0 ? static_cast<float>(sx) / 32768.f :
        static_cast<float>(sx) / 32767.f;
    const float fy = sy < 0 ? static_cast<float>(sy) / 32768.f :
        static_cast<float>(sy) / 32767.f;
    const float length = std::hypot(fx, fy);
    if (length <= deadzone) { x = 0.f; y = 0.f; return; }
    const float scaled = std::clamp((length - deadzone) / (1.f - deadzone), 0.f, 1.f);
    x = scaled * fx / length;
    y = scaled * fy / length;
}

struct Context {
    explicit Context(uintptr_t window_handle) : window(reinterpret_cast<HWND>(window_handle))
    {
        for (auto const* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
            library = LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!library) continue;
            get_state = reinterpret_cast<GetPadState>(GetProcAddress(library, "XInputGetState"));
            set_state = reinterpret_cast<SetPadState>(GetProcAddress(library, "XInputSetState"));
            if (get_state && set_state) break;
            FreeLibrary(library);
            library = nullptr;
        }
        // Windows RAWINPUT provides relative mouse deltas independent of screen edges.
        // The host forwards WM_INPUT via the provider-neutral engine.input contract.
        if (window) {
            RAWINPUTDEVICE device{};
            device.usUsagePage = 0x01;
            device.usUsage = 0x02; // generic mouse
            device.hwndTarget = window;
            raw_mouse = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
        }
        // Keyboard and mouse remain usable even if the OS has no XInput runtime.
    }
    ~Context()
    {
        if (raw_mouse) {
            RAWINPUTDEVICE device{};
            device.usUsagePage = 0x01;
            device.usUsage = 0x02;
            device.dwFlags = RIDEV_REMOVE;
            RegisterRawInputDevices(&device, 1, sizeof(device));
        }
        if (library) FreeLibrary(library);
    }

    void clear()
    {
        keys.fill(0);
        buttons.fill(0);
        mouse_dx = mouse_dy = wheel_v = wheel_h = 0;
        text_count = 0;
        lead_surrogate = 0;
        mouse_valid = false;
    }

    void append_text(uint32_t codepoint)
    {
        if (codepoint <= 0x10ffffu && !(codepoint >= 0xd800u && codepoint <= 0xdfffu) &&
            text_count < text.size())
            text[text_count++] = codepoint;
    }

    void message(UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg) {
        case WM_ACTIVATEAPP:
            focused = wp != 0;
            if (!focused) clear();
            break;
        case WM_SETFOCUS: focused = true; break;
        case WM_KILLFOCUS: focused = false; clear(); break;
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            if (focused && wp < keys.size()) keys[wp] = 1;
            break;
        case WM_KEYUP: case WM_SYSKEYUP:
            if (wp < keys.size()) keys[wp] = 0;
            break;
        case WM_CHAR: {
            const auto ch = static_cast<uint32_t>(wp);
            if (ch >= 0xd800u && ch <= 0xdbffu) { lead_surrogate = ch; break; }
            if (ch >= 0xdc00u && ch <= 0xdfffu && lead_surrogate) {
                append_text(0x10000u + ((lead_surrogate - 0xd800u) << 10) + ch - 0xdc00u);
            } else if (ch < 0xd800u || ch > 0xdfffu) append_text(ch);
            lead_surrogate = 0;
            break;
        }
        case WM_INPUT: {
            if (!raw_mouse) break;
            UINT length = 0;
            auto handle = reinterpret_cast<HRAWINPUT>(lp);
            if (GetRawInputData(handle, RID_INPUT, nullptr, &length,
                                sizeof(RAWINPUTHEADER)) != 0 ||
                length < sizeof(RAWINPUTHEADER) || length > 4096)
                break;
            std::vector<BYTE> bytes(length);
            if (GetRawInputData(handle, RID_INPUT, bytes.data(), &length,
                                sizeof(RAWINPUTHEADER)) != length)
                break;
            auto const* event = reinterpret_cast<RAWINPUT const*>(bytes.data());
            if (event->header.dwType == RIM_TYPEMOUSE &&
                (event->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                mouse_dx += event->data.mouse.lLastX;
                mouse_dy += event->data.mouse.lLastY;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            int const x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (mouse_valid && !raw_mouse) { mouse_dx += x - mouse_x; mouse_dy += y - mouse_y; }
            mouse_x = x; mouse_y = y; mouse_valid = true;
            break;
        }
        case WM_LBUTTONDOWN: buttons[0] = 1; break;
        case WM_LBUTTONUP: buttons[0] = 0; break;
        case WM_RBUTTONDOWN: buttons[1] = 1; break;
        case WM_RBUTTONUP: buttons[1] = 0; break;
        case WM_MBUTTONDOWN: buttons[2] = 1; break;
        case WM_MBUTTONUP: buttons[2] = 0; break;
        case WM_XBUTTONDOWN:
            buttons[HIWORD(wp) == XBUTTON1 ? 3 : 4] = 1; break;
        case WM_XBUTTONUP:
            buttons[HIWORD(wp) == XBUTTON1 ? 3 : 4] = 0; break;
        case WM_MOUSEWHEEL: wheel_v += GET_WHEEL_DELTA_WPARAM(wp); break;
        case WM_MOUSEHWHEEL: wheel_h += GET_WHEEL_DELTA_WPARAM(wp); break;
        }
    }

    void sample(NorthStarInputState& out)
    {
        out = {};
        out.size = sizeof(out);
        out.focused = focused ? 1u : 0u;
        std::copy(keys.begin(), keys.end(), out.keys);
        std::copy(buttons.begin(), buttons.end(), out.mouse_buttons);
        out.mouse_x = mouse_x; out.mouse_y = mouse_y;
        out.mouse_dx = std::exchange(mouse_dx, 0);
        out.mouse_dy = std::exchange(mouse_dy, 0);
        out.wheel_vertical = std::exchange(wheel_v, 0);
        out.wheel_horizontal = std::exchange(wheel_h, 0);
        out.text_count = text_count;
        std::copy_n(text.data(), text_count, out.text);
        text_count = 0;
        if (!get_state) return;
        for (DWORD i = 0; i < NORTHSTARCPP_INPUT_MAX_GAMEPADS; ++i) {
            XINPUT_STATE state{};
            if (get_state(i, &state) != ERROR_SUCCESS) continue;
            auto& pad = out.gamepads[i];
            pad.connected = 1;
            pad.buttons = state.Gamepad.wButtons;
            normalize_stick(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY,
                XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.f, pad.left_x, pad.left_y);
            normalize_stick(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY,
                XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32767.f, pad.right_x, pad.right_y);
            auto trigger = [](BYTE v) { return v <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 0.f :
                (v - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
                (255.f - XINPUT_GAMEPAD_TRIGGER_THRESHOLD); };
            pad.left_trigger = trigger(state.Gamepad.bLeftTrigger);
            pad.right_trigger = trigger(state.Gamepad.bRightTrigger);
        }
    }

    void rumble(uint32_t pad, float left, float right)
    {
        if (!set_state || pad >= NORTHSTARCPP_INPUT_MAX_GAMEPADS ||
            !std::isfinite(left) || !std::isfinite(right) ||
            left < 0.f || left > 1.f || right < 0.f || right > 1.f)
            throw std::runtime_error("Invalid or unavailable XInput vibration request");
        XINPUT_VIBRATION values{};
        values.wLeftMotorSpeed = static_cast<WORD>(std::lround(left * 65535.f));
        values.wRightMotorSpeed = static_cast<WORD>(std::lround(right * 65535.f));
        if (set_state(pad, &values) != ERROR_SUCCESS)
            throw std::runtime_error("XInput controller is disconnected");
    }

    HWND window = nullptr;
    HMODULE library = nullptr;
    GetPadState get_state = nullptr;
    SetPadState set_state = nullptr;
    bool focused = true, mouse_valid = false, raw_mouse = false;
    std::array<uint8_t, NORTHSTARCPP_INPUT_MAX_KEYS> keys{};
    std::array<uint8_t, 5> buttons{};
    std::array<uint32_t, NORTHSTARCPP_INPUT_MAX_TEXT> text{};
    uint32_t text_count = 0, lead_surrogate = 0;
    int32_t mouse_x = 0, mouse_y = 0, mouse_dx = 0, mouse_dy = 0;
    int32_t wheel_v = 0, wheel_h = 0;
};

void* create(uintptr_t window)
{
    Context* result = nullptr;
    guarded([&] { result = new Context(window); });
    return result;
}
void destroy(void* ctx) { delete static_cast<Context*>(ctx); }
int feed_message(void* ctx, uint32_t msg, uintptr_t wp, intptr_t lp)
{
    return guarded([&] {
        if (!ctx) throw std::runtime_error("Missing input context");
        static_cast<Context*>(ctx)->message(msg, static_cast<WPARAM>(wp), static_cast<LPARAM>(lp));
    });
}
int poll(void* ctx, NorthStarInputState* output)
{
    return guarded([&] {
        if (!ctx || !output || output->size != sizeof(NorthStarInputState))
            throw std::runtime_error("Invalid input snapshot buffer");
        static_cast<Context*>(ctx)->sample(*output);
    });
}
int vibrate(void* ctx, uint32_t player, float left, float right)
{
    return guarded([&] {
        if (!ctx) throw std::runtime_error("Missing input context");
        static_cast<Context*>(ctx)->rumble(player, left, right);
    });
}

NorthStarInputApi const api{create, destroy, feed_message, poll, vibrate};
NorthStarModule const module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_INPUT, "Win32/XInput", "1.0.0",
    NORTHSTARCPP_INPUT_ABI, sizeof(api), &api, last_error};
} // namespace

extern "C" const NorthStarModule* northstar_module_query(
    uint32_t host, char const* capability, uint32_t version)
{
    return northstarcpp::module_provider::match(module, host, capability, version);
}
