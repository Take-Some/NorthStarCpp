#pragma once

#include <northstarcpp/render_frame_memory_api.h>
#include <cstddef>
#include <cstdint>

namespace northstarcpp::diagnostics::frame_memory {

inline thread_local NorthStarFrameMemoryCounters* counters = nullptr;

inline NorthStarFrameMemoryCounters* exchange(NorthStarFrameMemoryCounters* next) noexcept
{
    auto* previous = counters;
    counters = next;
    return previous;
}

inline const NorthStarRenderFrameMemoryApi api{NORTHSTARCPP_RENDER_FRAME_MEMORY_ABI, exchange};

class Scope {
public:
    explicit Scope(NorthStarFrameMemoryCounters* next,
                   NorthStarRenderFrameMemoryApi const* provider = &api) noexcept
        : provider_(provider), previous_(provider ? provider->exchange(next) : nullptr) {}
    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;
    ~Scope() { if (provider_) provider_->exchange(previous_); }
private:
    NorthStarRenderFrameMemoryApi const* provider_;
    NorthStarFrameMemoryCounters* previous_;
};

template <class Buffer, class Operation>
void track_capacity(Buffer& buffer, Operation operation)
{
    if (!counters) {
        operation();
        return;
    }
    const auto before = buffer.capacity();
    operation();
    if (buffer.capacity() > before) {
        ++counters->allocations;
        counters->allocation_bytes += buffer.capacity() * sizeof(typename Buffer::value_type);
    }
}

template <class Buffer>
void reserve(Buffer& buffer, std::size_t size)
{
    track_capacity(buffer, [&] { buffer.reserve(size); });
}

template <class Buffer>
void resize(Buffer& buffer, std::size_t size)
{
    track_capacity(buffer, [&] { buffer.resize(size); });
}

template <class Buffer, class T>
void assign(Buffer& buffer, T const* from, T const* to,
            uint64_t NorthStarFrameMemoryCounters::* category)
{
    track_capacity(buffer, [&] { buffer.assign(from, to); });
    if (counters) counters->*category += (to - from) * sizeof(T);
}

template <class Buffer, class T>
void append_geometry(Buffer& buffer, T const* from, T const* to)
{
    track_capacity(buffer, [&] { buffer.insert(buffer.end(), from, to); });
    if (counters) counters->geometry_copy_bytes += (to - from) * sizeof(T);
}

inline void copied(uint64_t NorthStarFrameMemoryCounters::* category,
                   std::uint64_t bytes) noexcept
{
    if (counters) counters->*category += bytes;
}

inline void device_allocated(std::uint64_t bytes) noexcept
{
    if (counters) {
        ++counters->device_allocations;
        counters->device_allocation_bytes += bytes;
    }
}

} // namespace northstarcpp::diagnostics::frame_memory
