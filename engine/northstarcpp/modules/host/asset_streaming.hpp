// NorthStarCpp
// Author: Kayla Verner
#pragma once

#include <northstarcpp/asset_streaming.hpp>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>


namespace northstarcpp::detail {

struct StreamValue {
    std::shared_ptr<void const> data;
    std::size_t bytes;
};

class AssetStreaming {
public:
    struct Record {
        std::promise<std::shared_ptr<void const>> completion;
        std::shared_future<std::shared_ptr<void const>> result = completion.get_future().share();
        std::function<StreamValue()> load;
        std::shared_ptr<std::atomic_bool const> generation;
        AssetPriority priority;
        std::uint64_t last_use;
        std::size_t bytes = 0;
        bool resident = false;
    };

    ~AssetStreaming();
    void stop();
    std::shared_ptr<Record> request(int kind, std::string path, AssetPriority priority,
                                    std::function<StreamValue()> load);
    void invalidate();
    void set_budget(std::size_t bytes);
    AssetStreamingStats stats();
    std::shared_mutex mounts;

private:
    using Key = std::pair<int, std::string>;
    using Records = std::map<Key, std::shared_ptr<Record>>;
    using Pending = std::list<Records::iterator>;

    void worker();
    void cancel_pending(std::exception_ptr error);
    void trim();

    mutable std::mutex mutex;
    std::condition_variable changed;
    Records records;
    Pending pending;
    std::thread thread;
    std::shared_ptr<std::atomic_bool> generation = std::make_shared<std::atomic_bool>(true);
    std::uint64_t sequence = 0;
    std::size_t loading = 0;
    std::size_t resident = 0;
    std::size_t resident_bytes = 0;
    std::size_t budget = 64u * 1024u * 1024u;
    bool stopping = false;
};

} // namespace northstarcpp::detail
