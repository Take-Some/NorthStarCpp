// NorthStarCpp
// Author: Kayla Verner
#include <northstarcpp/assets.hpp>
#include "asset_list_file.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace northstarcpp {
namespace {

using Asset = shared_assets::DrawableResources;
using shared_assets::detail::require;

std::array<float, 3> point(shared_assets::detail::Tree const& tree)
{
    require(tree.size() == 3, "Asset zone requires three coordinates");
    std::array<float, 3> result{};
    std::size_t i = 0;
    for (auto const& component : tree) {
        auto const value = component.second.get_value<float>();
        require(component.first.empty() && std::isfinite(value), "Invalid asset zone coordinate");
        result[i++] = value;
    }
    return result;
}

float distance_to_zone(std::array<float, 3> const& position, AssetZone const& zone)
{
    float distance = 0.f;
    for (std::size_t i = 0; i < position.size(); ++i)
        distance = std::max(distance, std::max(zone.min[i] - position[i], position[i] - zone.max[i]));
    return distance;
}

} // namespace

std::vector<AssetZone> parse_asset_zones(std::string const& json)
{
    require(json.size() <= 1024 * 1024, "Asset zone manifest exceeds 1 MiB");
    auto const tree = shared_assets::detail::parse_json(json);
    require(tree.get<unsigned>("version") == 1, "Unsupported asset zone version");
    auto const& list = tree.get_child("zones");
    require(list.size() <= 512, "Too many asset zones");
    std::vector<AssetZone> zones;
    for (auto const& entry : list) {
        require(entry.first.empty(), "Expected an array of asset zones");
        AssetZone zone;
        zone.min = point(entry.second.get_child("min"));
        zone.max = point(entry.second.get_child("max"));
        for (std::size_t i = 0; i < zone.min.size(); ++i)
            require(zone.min[i] <= zone.max[i], "Inverted asset zone bounds");
        auto const& assets = entry.second.get_child("drawables");
        require(assets.size() <= 128, "Too many drawable references in an asset zone");
        for (auto const& asset : assets) {
            require(asset.first.empty(), "Expected an array of drawable references");
            auto reference = asset.second.get_value<std::string>();
            shared_assets::detail::validate_reference(reference);
            auto const path = reference.substr(0, reference.find('@'));
            require(shared_assets::detail::extension(path) == ".ydd", "Asset zone requires YDD drawable references");
            zone.drawables.push_back(std::move(reference));
        }
        zones.push_back(std::move(zone));
    }
    return zones;
}

AssetZones::AssetZones(AssetStore const& store, std::vector<AssetZone> zones, float prefetch_distance)
    : assets(store), zones(std::move(zones)), prefetch(prefetch_distance)
{
    require(std::isfinite(prefetch) && prefetch >= 0.f, "Invalid asset zone prefetch distance");
}

void AssetZones::update(std::array<float, 3> const& focus)
{
    require(std::all_of(focus.begin(), focus.end(), [](float value) { return std::isfinite(value); }),
        "Invalid asset streaming focus");
    std::map<std::string, AssetPriority> desired;
    for (auto const& zone : zones) {
        auto const distance = distance_to_zone(focus, zone);
        auto const priority = distance == 0.f ? AssetPriority::critical : AssetPriority::background;
        if (distance > prefetch + 5.f)
            continue;
        for (auto const& reference : zone.drawables) {
            auto const previous = active.find(reference);
            // A small exit margin avoids repeated loads near a zone boundary.
            auto const limit = prefetch + (previous == active.end() ? 0.f : 5.f);
            if (distance <= limit) {
                auto result = desired.emplace(reference, priority);
                result.first->second = std::max(result.first->second, priority);
            }
        }
    }
    auto const previous_count = active.size();
    for (auto item = active.begin(); item != active.end();) {
        if (desired.count(item->first))
            ++item;
        else
            item = active.erase(item);
    }
    if (active.size() != previous_count)
        assets.streaming_stats();
    for (auto priority : {AssetPriority::critical, AssetPriority::background}) {
        for (auto const& item : desired) {
            if (item.second != priority)
                continue;
            auto const previous = active.find(item.first);
            if (previous != active.end() && !previous->second.request.stale() &&
                item.second <= previous->second.priority)
                continue;
            auto request = assets.request_drawable_resources(item.first, item.second);
            if (request)
                active.insert_or_assign(item.first, Active{item.second, std::move(request)});
        }
    }
}

AssetRequest<Asset> AssetZones::find(std::string const& reference) const
{
    auto const found = active.find(reference);
    return found == active.end() ? AssetRequest<Asset>{} : found->second.request;
}

void AssetZones::wait() const
{
    for (auto const& item : active) {
        try {
            item.second.request.get();
        } catch (std::exception const& error) {
            throw std::runtime_error(item.first + ": " + error.what());
        }
    }
}

} // namespace northstarcpp
