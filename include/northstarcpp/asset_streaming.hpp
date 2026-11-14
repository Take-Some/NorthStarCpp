// NorthStarCpp
// Author: Kayla Verner
#pragma once

#include <northstarcpp/shared_assets.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace northstarcpp {

class AssetStore;

enum class AssetPriority { background, normal, critical };

struct AssetStreamingStats {
    std::size_t queued = 0;
    std::size_t loading = 0;
    std::size_t resident = 0;
    std::size_t resident_bytes = 0;
};

// A request keeps its decoded resource alive. Release it when no longer needed.
template<class T>
class AssetRequest {
public:
    AssetRequest() = default;
    explicit operator bool() const noexcept { return result.valid(); }
    bool stale() const noexcept { return generation && !generation->load(std::memory_order_relaxed); }
    bool ready() const
    {
        return result.valid() &&
            result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    std::shared_ptr<T const> get() const
    {
        auto const& value = result.get();
        return std::shared_ptr<T const>(record, static_cast<T const*>(value.get()));
    }

private:
    friend class AssetStore;
    AssetRequest(std::shared_future<std::shared_ptr<void const>> completion,
                 std::shared_ptr<void const> pin, std::shared_ptr<std::atomic_bool const> epoch)
        : result(std::move(completion)), record(std::move(pin)), generation(std::move(epoch)) {}
    std::shared_future<std::shared_ptr<void const>> result;
    std::shared_ptr<void const> record;
    std::shared_ptr<std::atomic_bool const> generation;
};

struct AssetZone {
    std::array<float, 3> min{}, max{};
    std::vector<std::string> drawables;
};

std::vector<AssetZone> parse_asset_zones(std::string const& json);

// The store must outlive the zone set. update() changes requests without waiting for I/O.
class AssetZones {
public:
    AssetZones(AssetStore const& store, std::vector<AssetZone> zones, float prefetch_distance = 20.f);
    void update(std::array<float, 3> const& focus);
    AssetRequest<shared_assets::DrawableResources> find(std::string const& reference) const;
    // Diagnostics can wait for current zones. Keep this out of the frame loop.
    void wait() const;

private:
    struct Active {
        AssetPriority priority;
        AssetRequest<shared_assets::DrawableResources> request;
    };
    AssetStore const& assets;
    std::vector<AssetZone> zones;
    float prefetch;
    std::map<std::string, Active> active;
};

} // namespace northstarcpp
