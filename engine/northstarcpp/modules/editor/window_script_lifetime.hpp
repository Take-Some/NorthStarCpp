#pragma once
// A callback does not own its host session. Revocation invalidates bindings
// after successful hot reload, stop, and project transitions.
#include <atomic>

namespace northstarcpp::window_app_internal {
struct ScriptBindingLease final {
    std::atomic_bool active{true};
    void revoke() noexcept {active.store(false,std::memory_order_release);}
    bool valid() const noexcept {return active.load(std::memory_order_acquire);}
};
} // namespace northstarcpp::window_app_internal
