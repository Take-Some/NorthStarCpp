#include <northstarcpp/frame_clock.hpp>
#include "module_loader.hpp"

namespace northstarcpp::timing {
namespace {

struct TimeProvider {
    Module module{NORTHSTARCPP_CAPABILITY_TIME};
    NorthStarTimeApi const& api = *module.query<NorthStarTimeApi>(NORTHSTARCPP_TIME_ABI);
    std::uint64_t frequency = 0;

    TimeProvider()
    {
        if (!api.read_frequency(&frequency))
            throw module.error("Cannot read engine.time frequency");
        if (frequency == 0)
            throw std::runtime_error("Invalid engine.time frequency");
    }

    std::uint64_t counter() const
    {
        std::uint64_t tick = 0;
        if (!api.read_counter(&tick))
            throw module.error("Cannot read engine.time counter");
        return tick;
    }

    double elapsed(std::uint64_t first, std::uint64_t last) const
    {
        if (last < first)
            throw std::runtime_error("engine.time counter moved backwards");
        return static_cast<double>(last - first) / frequency;
    }
};

TimeProvider const& time_provider()
{
    static TimeProvider const provider;
    return provider;
}

} // namespace

void FrameClock::start()
{
    begin_ = time_provider().counter();
    started_ = true;
}

double FrameClock::elapsed_seconds() const
{
    if (!started_)
        return 0.0;
    auto const& provider = time_provider();
    return provider.elapsed(begin_, provider.counter());
}

double FrameClock::restart_seconds()
{
    auto const& provider = time_provider();
    auto const now = provider.counter();
    auto const elapsed = started_ ? provider.elapsed(begin_, now) : 0.0;
    begin_ = now;
    started_ = true;
    return elapsed;
}

} // namespace northstarcpp::timing
