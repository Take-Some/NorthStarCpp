// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_INPUT_ABI 1u
#define NORTHSTARCPP_INPUT_MAX_KEYS 256u
#define NORTHSTARCPP_INPUT_MAX_GAMEPADS 4u
#define NORTHSTARCPP_INPUT_MAX_TEXT 32u

// Portable key identifiers. ASCII letters and Windows-compatible VK values.
// Other providers MUST map their platform keycodes to this public convention.
#define NORTHSTAR_INPUT_KEY_ESCAPE 0x1Bu
#define NORTHSTAR_INPUT_KEY_LEFT 0x25u
#define NORTHSTAR_INPUT_KEY_UP 0x26u
#define NORTHSTAR_INPUT_KEY_RIGHT 0x27u
#define NORTHSTAR_INPUT_KEY_DOWN 0x28u
#define NORTHSTAR_INPUT_PAD_DPAD_UP 0x0001u
#define NORTHSTAR_INPUT_PAD_DPAD_DOWN 0x0002u
#define NORTHSTAR_INPUT_PAD_DPAD_LEFT 0x0004u
#define NORTHSTAR_INPUT_PAD_DPAD_RIGHT 0x0008u
#define NORTHSTAR_INPUT_PAD_BACK 0x0020u
#define NORTHSTAR_INPUT_PAD_START 0x0010u
#define NORTHSTAR_INPUT_PAD_A 0x1000u
#define NORTHSTAR_INPUT_PAD_B 0x2000u
#define NORTHSTAR_INPUT_PAD_X 0x4000u
#define NORTHSTAR_INPUT_PAD_Y 0x8000u

typedef struct NorthStarInputGamepad {
    uint32_t connected;
    uint32_t buttons;
    float left_x, left_y, right_x, right_y;
    float left_trigger, right_trigger;
} NorthStarInputGamepad;

typedef struct NorthStarInputState {
    uint32_t size;             // sizeof(NorthStarInputState), checked by the provider
    uint32_t focused;
    uint8_t keys[NORTHSTARCPP_INPUT_MAX_KEYS];
    uint8_t mouse_buttons[5]; // left, right, middle, X1, X2
    int32_t mouse_x, mouse_y;  // window-client coordinates
    int32_t mouse_dx, mouse_dy;
    int32_t wheel_vertical, wheel_horizontal; // Win32 wheel units (120 / notch)
    uint32_t text_count;
    uint32_t text[NORTHSTARCPP_INPUT_MAX_TEXT]; // Unicode code points since previous poll
    NorthStarInputGamepad gamepads[NORTHSTARCPP_INPUT_MAX_GAMEPADS];
} NorthStarInputState;

// host_window is an opaque native top-level window handle, or zero for headless tests.
// feed_message forwards host OS messages as raw integer values (ignored if unsupported).
// poll returns a state snapshot and consumes per-frame deltas and text.
// 1 = success, 0 = error; diagnostic text via NorthStarModule::last_error.
typedef struct NorthStarInputApi {
    void* (*create)(uintptr_t host_window);
    void (*destroy)(void* context);
    int (*feed_message)(void* context, uint32_t message, uintptr_t wparam, intptr_t lparam);
    int (*poll)(void* context, NorthStarInputState* state);
    int (*vibrate)(void* context, uint32_t gamepad, float left, float right);
} NorthStarInputApi;

#ifdef __cplusplus
}
#endif
