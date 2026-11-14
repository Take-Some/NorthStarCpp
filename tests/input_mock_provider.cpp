// Alternative engine.input provider, deliberately independent of Win32/XInput.
#include <northstarcpp/input_plugin.h>
#include <cstring>
namespace {
thread_local const char* message = "";
char const* last_error() { return message; }
void* create(uintptr_t) { return new int(1); }
void destroy(void* ctx) { delete static_cast<int*>(ctx); }
int feed(void* ctx, uint32_t, uintptr_t, intptr_t) { return ctx ? 1 : 0; }
int poll(void* ctx, NorthStarInputState* state)
{
    if (!ctx || !state || state->size != sizeof(*state)) return 0;
    *state = {};
    state->size = sizeof(*state);
    state->focused = 1;
    state->keys['W'] = 1;
    state->gamepads[0].connected = 1;
    state->gamepads[0].left_x = 0.5f;
    return 1;
}
int vibrate(void* ctx, uint32_t, float, float) { return ctx ? 1 : 0; }
NorthStarInputApi const api{create, destroy, feed, poll, vibrate};
NorthStarModule const module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_INPUT, "Mock Input", "0.1.0",
    NORTHSTARCPP_INPUT_ABI, sizeof(api), &api, last_error};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule* northstar_module_query(
    uint32_t abi, char const* capability, uint32_t version)
{
    if (abi != NORTHSTARCPP_MODULE_ABI || !capability ||
        std::strcmp(capability, NORTHSTARCPP_CAPABILITY_INPUT) ||
        version != NORTHSTARCPP_INPUT_ABI) return nullptr;
    return &module;
}
