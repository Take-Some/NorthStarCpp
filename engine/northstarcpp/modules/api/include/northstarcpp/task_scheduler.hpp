#pragma once
// Pure C++ bounded background tasks. No Qt, GPU or provider ABI dependency.
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace northstarcpp::task_scheduler {
enum class Priority : int { background=0, normal=1, high=2 };
struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("Background task cancelled") {}
};

class TaskScheduler final {
public:
    explicit TaskScheduler(std::size_t workers=2,std::size_t queue_capacity=64)
        : capacity_(queue_capacity) {
        if(!workers || workers>32 || !capacity_ || capacity_>65536)
            throw std::invalid_argument("Invalid scheduler limits");
        queue_.reserve(capacity_);
        try {
            for(std::size_t i=0;i<workers;++i)
                workers_.emplace_back([this]{worker_loop();});
        } catch(...) {
            stop_and_join();
            throw;
        }
    }
    ~TaskScheduler() {stop_and_join();}
    TaskScheduler(TaskScheduler const&)=delete;
    TaskScheduler& operator=(TaskScheduler const&)=delete;

    template<class Fn>
    auto submit(Priority priority,
                std::shared_ptr<std::atomic<bool>> cancelled,
                Fn&& fn)->std::future<std::invoke_result_t<Fn>> {
        using Result=std::invoke_result_t<Fn>;
        auto task=std::make_shared<std::packaged_task<Result()>>(
            [token=std::move(cancelled),work=std::forward<Fn>(fn)]() mutable -> Result {
                if(token && token->load(std::memory_order_relaxed))throw Cancelled{};
                if constexpr(std::is_void_v<Result>) {
                    work();
                } else {
                    return work();
                }
            });
        auto future=task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_)throw std::runtime_error("Task scheduler is stopping");
            if(queue_.size()>=capacity_)throw std::runtime_error("Task scheduler queue full");
            queue_.push_back({priority,next_sequence_++,
                [task=std::move(task)]{(*task)();}});
        }
        ready_.notify_one();
        return future;
    }
    std::size_t pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
private:
    struct Task {
        Priority priority;
        std::uint64_t sequence;
        std::function<void()> invoke;
    };
    void worker_loop() {
        for(;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock,[this]{return stopping_ || !queue_.empty();});
                if(queue_.empty()) {
                    if(stopping_)return;
                    continue;
                }
                // Highest priority first; FIFO within the same priority.
                auto it=std::max_element(queue_.begin(),queue_.end(),
                    [](Task const& a,Task const& b) {
                        if(a.priority!=b.priority)return a.priority<b.priority;
                        return a.sequence>b.sequence;
                    });
                task=std::move(*it);
                if(it!=queue_.end()-1)*it=std::move(queue_.back());
                queue_.pop_back();
            }
            task.invoke(); // packaged_task captures worker exceptions in future
        }
    }
    void stop_and_join() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_=true;
        }
        ready_.notify_all();
        for(auto& worker:workers_)
            if(worker.joinable())worker.join();
    }
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::vector<Task> queue_;
    std::vector<std::thread> workers_;
    std::uint64_t next_sequence_=0;
    bool stopping_=false;
};

// Heavy import and collision jobs share a bounded executor instead of
// relying on an unbounded number of independent worker threads.
inline TaskScheduler& asset_tasks() {
    static TaskScheduler scheduler(2,64);
    return scheduler;
}
} // namespace northstarcpp::task_scheduler
