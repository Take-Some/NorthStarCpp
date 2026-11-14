// NorthStarCpp
// Author: Kayla Verner
#include "asset_streaming.hpp"

#include <algorithm>
#include <stdexcept>

namespace northstarcpp::detail {

AssetStreaming::~AssetStreaming() { stop(); }

void AssetStreaming::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping)
            return;
        stopping = true;
        cancel_pending(std::make_exception_ptr(std::runtime_error("Asset manager closed")));
    }
    changed.notify_all();
    if (thread.joinable())
        thread.join();
}

std::shared_ptr<AssetStreaming::Record> AssetStreaming::request(int kind, std::string path,
    AssetPriority priority, std::function<StreamValue()> load)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (stopping)
        throw std::runtime_error("Asset manager closed");
    auto key = Key(kind, std::move(path));
    auto const found = records.find(key);
    if (found != records.end()) {
        auto record = found->second;
        record->priority = std::max(record->priority, priority);
        record->last_use = ++sequence;
        trim();
        return record;
    }

    trim();
    if (pending.size() >= 1024)
        return {};
    if (!thread.joinable())
        thread = std::thread(&AssetStreaming::worker, this);

    auto record = std::make_shared<Record>();
    record->load = std::move(load);
    record->generation = generation;
    record->priority = priority;
    record->last_use = ++sequence;
    auto const inserted = records.emplace(std::move(key), record);
    try {
        pending.push_back(inserted.first);
    } catch (...) {
        records.erase(inserted.first);
        throw;
    }
    changed.notify_one();
    return record;
}

void AssetStreaming::cancel_pending(std::exception_ptr error)
{
    for (auto const& item : pending) {
        item->second->completion.set_exception(error);
        records.erase(item);
    }
    pending.clear();
}

void AssetStreaming::invalidate()
{
    std::lock_guard<std::mutex> lock(mutex);
    auto next = std::make_shared<std::atomic_bool>(true);
    generation->store(false, std::memory_order_relaxed);
    generation = std::move(next);
    cancel_pending(std::make_exception_ptr(std::runtime_error("Asset mounts changed before loading")));
    records.clear();
    resident = 0;
    resident_bytes = 0;
}

void AssetStreaming::set_budget(std::size_t bytes)
{
    std::lock_guard<std::mutex> lock(mutex);
    budget = bytes;
    trim();
}

AssetStreamingStats AssetStreaming::stats()
{
    std::lock_guard<std::mutex> lock(mutex);
    trim();
    return {pending.size(), loading, resident, resident_bytes};
}

void AssetStreaming::trim()
{
    while (resident_bytes > budget || resident > 1024) {
        auto oldest = records.end();
        for (auto item = records.begin(); item != records.end(); ++item) {
            auto const& record = item->second;
            if (record->resident && record.use_count() == 1 &&
                (oldest == records.end() || record->last_use < oldest->second->last_use))
                oldest = item;
        }
        if (oldest == records.end())
            return;
        resident_bytes -= oldest->second->bytes;
        --resident;
        records.erase(oldest);
    }
}

void AssetStreaming::worker()
{
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&] { return stopping || !pending.empty(); });
            if (stopping)
                return;
        }

        // Pin the VFS mount table while resolving all dependencies of this request.
        std::shared_lock<std::shared_mutex> mounted(mounts);
        std::shared_ptr<Record> record;
        Records::iterator item;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping || pending.empty())
                continue;
            auto const selected = std::max_element(pending.begin(), pending.end(),
                [](auto a, auto b) { return a->second->priority < b->second->priority; });
            item = *selected;
            pending.erase(selected);
            if (item->second.use_count() == 1) {
                records.erase(item); // All callers released the queued request.
                continue;
            }
            record = item->second;
            ++loading;
        }

        StreamValue value{};
        std::exception_ptr failure;
        try {
            value = record->load();
        } catch (...) {
            failure = std::current_exception();
        }

        std::lock_guard<std::mutex> lock(mutex);
        --loading;
        record->load = {};
        if (failure) {
            record->completion.set_exception(failure);
            records.erase(item); // A later request may retry.
        } else {
            record->bytes = value.bytes;
            record->resident = true;
            ++resident;
            resident_bytes += value.bytes;
            record->completion.set_value(std::move(value.data));
        }
        record.reset();
        trim();
    }
}

} // namespace northstarcpp::detail
