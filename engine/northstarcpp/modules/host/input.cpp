// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#include <northstarcpp/input.hpp>
#include "module_loader.hpp"

namespace northstarcpp {

struct InputDevice::State {
    Module module{NORTHSTARCPP_CAPABILITY_INPUT};
    NorthStarInputApi const& api = *module.query<NorthStarInputApi>(NORTHSTARCPP_INPUT_ABI);
    void* context = nullptr;

    explicit State(uintptr_t native_window)
    {
        context = api.create(native_window);
        if (!context)
            throw module.error("Cannot create input context");
    }
    ~State() { api.destroy(context); }
};

InputDevice::InputDevice(uintptr_t window) : state(std::make_unique<State>(window)) {}
InputDevice::~InputDevice() = default;
std::string InputDevice::provider_name() const { return state->module.provider(); }
std::string InputDevice::provider_version() const { return state->module.version(); }

void InputDevice::feed_message(uint32_t msg, uintptr_t wp, intptr_t lp)
{
    if (!state->api.feed_message(state->context, msg, wp, lp))
        throw state->module.error("Input message rejected");
}
NorthStarInputState InputDevice::poll()
{
    NorthStarInputState sample{};
    sample.size = sizeof(sample);
    if (!state->api.poll(state->context, &sample) || sample.size != sizeof(sample) ||
        sample.text_count > NORTHSTARCPP_INPUT_MAX_TEXT)
        throw state->module.error("Cannot read input snapshot");
    return sample;
}
void InputDevice::vibrate(uint32_t player, float left, float right)
{
    if (!state->api.vibrate(state->context, player, left, right))
        throw state->module.error("Cannot update controller vibration");
}

} // namespace northstarcpp
