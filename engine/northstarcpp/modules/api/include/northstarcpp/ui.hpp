// Provider-neutral C++ helpers for engine.ui ABI 1.
#pragma once
#include <northstarcpp/ui_plugin.h>
#include <string_view>

namespace northstarcpp::ui {

inline NorthStarUIString string(std::string_view value) noexcept {
    return {value.data(), static_cast<uint64_t>(value.size())};
}

inline NorthStarUIElement element(uint32_t kind, NorthStarUIHandle parent,
                                  std::string_view name) noexcept {
    return {sizeof(NorthStarUIElement), kind, parent, string(name), 0u, 0u};
}

inline NorthStarUIValue text(std::string_view value) noexcept {
    return {sizeof(NorthStarUIValue), NORTHSTAR_UI_VALUE_STRING,
            0.0, 0u, 0u, string(value)};
}

inline NorthStarUIValue number(double value) noexcept {
    return {sizeof(NorthStarUIValue), NORTHSTAR_UI_VALUE_NUMBER,
            value, 0u, 0u, {nullptr, 0u}};
}

inline NorthStarUIValue boolean(bool value) noexcept {
    return {sizeof(NorthStarUIValue), NORTHSTAR_UI_VALUE_BOOL,
            0.0, 0u, value ? 1u : 0u, {nullptr, 0u}};
}

} // namespace northstarcpp::ui
