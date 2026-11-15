// Regression coverage for the internal asynchronous asset cache.
// Runs without renderer, VFS provider, GPU or project dependencies.
#include "../engine/northstarcpp/modules/host/asset_streaming.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using northstarcpp::AssetPriority;
using northstarcpp::detail::AssetStreaming;
using northstarcpp::detail::StreamValue;
using namespace std::chrono_literals;

void require(bool value, char const* message)
{
    if (!value)
        throw std::runtime_error(message);
}

std::shared_ptr<AssetStreaming::Record> request(
    AssetStreaming& streaming, std::string name, AssetPriority priority,
    std::atomic<int>& calls)
{
    return streaming.request(1, name, priority, [&calls, name] {
        ++calls;
        return StreamValue{std::make_shared<std::string const>(name), 32};
    });
}

void wait(std::shared_ptr<AssetStreaming::Record> const& record)
{
    require(record && record->result.wait_for(5s) == std::future_status::ready,
        "Asset request did not finish");
    require(static_cast<bool>(record->result.get()), "Asset request returned null");
}

void test_lru_and_cache()
{
    AssetStreaming streaming;
    std::atomic<int> calls{0};
    for (auto const* name : {"A", "B", "C", "D"}) {
        auto item = request(streaming, name, AssetPriority::normal, calls);
        wait(item);
    }
    require(streaming.stats().resident == 4, "Expected four cached assets");
    // A is the oldest of four 32-byte entries.
    streaming.set_budget(96);
    require(streaming.stats().resident == 3, "Failed to evict the oldest cache entry");
    require(streaming.stats().resident_bytes == 96, "Cache byte accounting incorrect");

    streaming.set_budget(1024);
    {
        auto b = request(streaming, "B", AssetPriority::normal, calls);
        wait(b);
        require(calls == 4, "Cache hit re-ran loader");
    }
    {
        auto a = request(streaming, "A", AssetPriority::normal, calls);
        wait(a);
        require(calls == 5, "Evicted resource was not reloaded");
    }
    auto previous = request(streaming, "D", AssetPriority::normal, calls);
    wait(previous);
    streaming.invalidate();
    require(!previous->generation->load(), "Mount invalidation did not mark prior request stale");
    require(streaming.stats().resident == 0, "Mount invalidation retained stale cache");
    auto current = request(streaming, "D", AssetPriority::normal, calls);
    wait(current);
    require(calls == 6, "New mount generation reused stale asset");
}

void test_priority_and_dedup()
{
    AssetStreaming streaming;
    std::promise<void> entered, resume;
    auto gate = resume.get_future().share();
    std::mutex order_mutex;
    std::vector<std::string> order;
    auto job = [&](std::string name, bool block) {
        return streaming.request(2, name, AssetPriority::background,
            [&, name, block] {
                if (block) {
                    entered.set_value();
                    if (gate.wait_for(5s) != std::future_status::ready)
                        throw std::runtime_error("Worker gate timed out");
                }
                std::lock_guard<std::mutex> lock(order_mutex);
                order.push_back(name);
                return StreamValue{std::make_shared<std::string const>(name), 8};
            });
    };
    auto first = job("first", true);
    require(entered.get_future().wait_for(5s) == std::future_status::ready,
        "First job did not enter worker");
    auto low = job("low", false);
    auto promoted = job("promoted", false);
    auto duplicate = streaming.request(2, "promoted", AssetPriority::critical,
        []() -> StreamValue { throw std::runtime_error("Duplicate loader executed"); });
    require(promoted == duplicate, "Identical pending loads were not deduplicated");
    resume.set_value();
    wait(first);
    wait(low);
    wait(promoted);
    require(order == std::vector<std::string>({"first", "promoted", "low"}),
        "Critical request was not scheduled before background request");
}

void test_pinning_and_failed_retry()
{
    AssetStreaming streaming;
    streaming.set_budget(0);
    std::atomic<int> calls{0};
    auto pinned = request(streaming, "pinned", AssetPriority::normal, calls);
    wait(pinned);
    require(streaming.stats().resident == 1,
        "Active handle was prematurely evicted under cache pressure");
    pinned.reset();
    require(streaming.stats().resident == 0,
        "Released handle stayed resident after a trim");

    std::atomic<int> attempts{0};
    auto failing = streaming.request(3, "recoverable", AssetPriority::normal, [&] {
        ++attempts;
        throw std::runtime_error("decode error");
        return StreamValue{};
    });
    require(failing->result.wait_for(5s) == std::future_status::ready,
        "Failed load did not finish");
    bool failed = false;
    try {
        (void)failing->result.get();
    } catch (std::runtime_error const& e) {
        failed = std::string(e.what()) == "decode error";
    }
    require(failed, "Loader error was not propagated");
    (void)streaming.stats(); // Wait for worker to remove the failed cache record.
    auto recovered = streaming.request(3, "recoverable", AssetPriority::critical,
        [&] {
            ++attempts;
            return StreamValue{std::make_shared<std::string const>("retry"), 8};
        });
    wait(recovered);
    require(attempts == 2, "Failed cache entry prevented a successful retry");
}

void test_shutdown_cancels_pending()
{
    AssetStreaming streaming;
    std::promise<void> entered, resume;
    auto gate = resume.get_future().share();
    auto running = streaming.request(4, "running", AssetPriority::normal, [&] {
        entered.set_value();
        if (gate.wait_for(5s) != std::future_status::ready)
            throw std::runtime_error("Worker gate timed out");
        return StreamValue{std::make_shared<std::string const>("running"), 8};
    });
    require(entered.get_future().wait_for(5s) == std::future_status::ready,
        "Shutdown fixture did not start");
    auto queued = streaming.request(4, "queued", AssetPriority::normal, [] {
        return StreamValue{std::make_shared<std::string const>("queued"), 8};
    });
    std::thread closer([&] { streaming.stop(); });
    auto const queue_status = queued->result.wait_for(3s);
    resume.set_value(); // Always unblock worker before joining the shutdown thread.
    closer.join();
    require(queue_status == std::future_status::ready,
        "Shutdown did not cancel the queued request");
    bool cancelled = false;
    try {
        (void)queued->result.get();
    } catch (std::runtime_error const& e) {
        cancelled = std::string(e.what()) == "Asset manager closed";
    }
    require(cancelled, "Queued request did not receive shutdown error");
    wait(running);
}

void test_bulk_eviction()
{
    AssetStreaming streaming;
    streaming.set_budget(1024u * 64u);
    std::atomic<int> calls{0};
    constexpr int total = 1024;
    for (int i = 0; i < total; ++i) {
        auto item = request(streaming, "asset_" + std::to_string(i), AssetPriority::normal, calls);
        wait(item);
    }
    auto stats = streaming.stats();
    require(stats.resident == total && stats.resident_bytes == total * 32,
        "Bulk cache residency counters incorrect");
    auto const begin = std::chrono::steady_clock::now();
    streaming.set_budget(0);
    auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - begin).count();
    stats = streaming.stats();
    require(stats.resident == 0 && stats.resident_bytes == 0,
        "Bulk LRU trim did not free all unpinned assets");
    std::cout << "evicted=" << total << " trim_us=" << elapsed << '\n';
}
}

int main()
{
    try {
        test_lru_and_cache();
        test_priority_and_dedup();
        test_pinning_and_failed_retry();
        test_shutdown_cancels_pending();
        test_bulk_eviction();
        std::cout << "asset streaming regressions: PASS\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "asset streaming regressions: FAIL: " << error.what() << '\n';
        return 1;
    }
}
