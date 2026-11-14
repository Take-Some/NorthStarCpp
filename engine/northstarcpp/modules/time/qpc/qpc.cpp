#include <northstarcpp/time_plugin.h>
#define NORTHSTARCPP_STATIC_LIBRARIES
#include <northstarcpp/macro_platform.h>
#include <northstarcpp/macro_building.h>
#include <northstarcpp/type_extensions.h>
#include <northstarcpp/timing_functions.h>
#include "module_provider.hpp"
#include <stdexcept>

namespace {
namespace timing = northstarcpp::timing;
namespace provider = northstarcpp::module_provider;

int read_counter(uint64_t* ticks)
{
    return provider::guarded([&] {
        if (!ticks)
            throw std::invalid_argument("Missing time counter output");
        *ticks = timing::query_performance_counter();
    });
}

int read_frequency(uint64_t* frequency)
{
    return provider::guarded([&] {
        if (!frequency)
            throw std::invalid_argument("Missing time frequency output");
        if (!timing::g_qpc_per_second)
            throw std::runtime_error("QueryPerformanceFrequency failed");
        *frequency = timing::g_qpc_per_second;
    });
}

NorthStarTimeApi const api{read_counter, read_frequency};
NorthStarModule const descriptor{
    sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_TIME, "qpc", "1.0.0",
    NORTHSTARCPP_TIME_ABI, sizeof(api), &api, provider::last_error
};
} // namespace

extern "C" NORTHSTARCPP_MODULE_EXPORT NorthStarModule const* northstar_module_query(
    uint32_t host, char const* capability, uint32_t version)
{
    return provider::match(descriptor, host, capability, version);
}
