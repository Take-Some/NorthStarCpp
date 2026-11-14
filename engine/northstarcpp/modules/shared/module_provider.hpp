// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once
#include <northstarcpp/module.h>

#include <cstdio>
#include <cstring>
#include <exception>

namespace northstarcpp::module_provider {

inline thread_local char error[512]{};

inline char const* last_error() noexcept { return error; }

template<class Function>
int guarded(Function&& function) noexcept
{
    error[0] = '\0';
    try {
        function();
        return 1;
    } catch (std::exception const& exception) {
        std::snprintf(error, sizeof(error), "%s", exception.what());
    } catch (...) {
        std::snprintf(error, sizeof(error), "Unexpected provider exception");
    }
    return 0;
}

inline NorthStarModule const* match(NorthStarModule const& module, uint32_t host,
    char const* capability, uint32_t api) noexcept
{
    if (host != module.host_abi || api != module.api_version ||
        !capability || std::strcmp(capability, module.capability) != 0)
        return nullptr;
    return &module;
}

} // namespace northstarcpp::module_provider
