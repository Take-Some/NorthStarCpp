#pragma once
// NorthStarCpp — bounded, single-worker, full-fidelity native model preload.
// Never blocks the rendering thread waiting for a YDR/YDD/YMAT/YTD decode.
#include <northstarcpp/native_model.hpp>
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <string_view>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace northstarcpp::viewport_detail {

// Preloading must have at least the same path restrictions as the synchronous
// viewport loader. Do not send unvalidated user-authored paths to a worker.
inline bool safe_native_preload_reference(
    std::string_view ref,std::filesystem::path const& root) {
    namespace fs=std::filesystem;
    auto const at=ref.find('@');
    auto const asset=ref.substr(0,at);
    if(asset.empty() || asset.size()>256 || asset.front()=='/' ||
       asset.find("..")!=std::string_view::npos ||
       asset.find(':')!=std::string_view::npos ||
       asset.find('\\')!=std::string_view::npos ||
       asset.find("//")!=std::string_view::npos ||
       asset.find('\0')!=std::string_view::npos ||
       (asset.substr(asset.size()>4?asset.size()-4:0)!=".ydr" &&
        asset.substr(asset.size()>4?asset.size()-4:0)!=".ydd"))
        return false;
    if(root.empty())return true; // encrypted NSP provider resolves virtually
    try {
        auto relative=fs::u8path(std::string(asset));
        if(relative.is_absolute())return false;
        auto cursor=root;
        if(!fs::exists(cursor/relative))cursor/="assets";
        for(auto const& part:relative) {
            if(part=="." || part==".." || part.is_absolute())return false;
            cursor/=part;
            if(fs::is_symlink(cursor))return false;
        }
    } catch(...) {return false;}
    return true;
}

class NativeModelPreloader final {
public:
    using Decoder=std::function<NativeModelResource(std::string const&)>;
    enum class Status { missing, pending, ready, failed };
    explicit NativeModelPreloader(Decoder decoder):decode_(std::move(decoder)) {}
    NativeModelPreloader(NativeModelPreloader const&)=delete;
    NativeModelPreloader& operator=(NativeModelPreloader const&)=delete;
    ~NativeModelPreloader() { shutdown(); }

    // Idempotent, nonblocking; prioritized uploads are decoded first, but a
    // currently running decode is never forcefully interrupted.
    bool enqueue(std::string name,bool high_priority=false) {
        std::lock_guard<std::mutex> guard(mutex_);
        if(stopping_)return false;
        if(jobs_.count(name))return true;
        if(jobs_.size()>=128u)return false; // outstanding jobs, not mesh budget
        auto [it,inserted]=jobs_.try_emplace(name);
        if(!inserted)return true;
        try {
            if(high_priority)pending_.push_front(std::move(name));
            else pending_.push_back(std::move(name));
            if(!worker_.joinable())
                worker_=std::thread([this] {run();});
        } catch(...) {
            jobs_.erase(it);
            throw;
        }
        changed_.notify_one();
        return true;
    }

    // Never calls wait/get. Successful output moves the original full mesh
    // and decoded material data from the worker into the scene's normal cache.
    Status take(std::string const& name,std::optional<NativeModelResource>& output,
                std::exception_ptr& failure) {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it=jobs_.find(name);
        if(it==jobs_.end())return Status::missing;
        if(it->second.status==Status::pending)return Status::pending;
        auto const status=it->second.status;
        if(status==Status::ready)output=std::move(it->second.result);
        else failure=it->second.error;
        jobs_.erase(it);
        return status;
    }

    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            stopping_=true;
            pending_.clear();
        }
        changed_.notify_all();
        if(worker_.joinable())worker_.join();
        std::lock_guard<std::mutex> guard(mutex_);
        jobs_.clear();
    }
    std::size_t outstanding() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return jobs_.size();
    }
private:
    struct Job {
        Status status=Status::pending;
        std::optional<NativeModelResource> result;
        std::exception_ptr error;
    };
    void run() noexcept {
        for(;;) {
            std::string name;
            {
                std::unique_lock<std::mutex> guard(mutex_);
                changed_.wait(guard,[this]{
                    return stopping_ || !pending_.empty();
                });
                if(stopping_)return;
                name=std::move(pending_.front());
                pending_.pop_front();
            }
            std::optional<NativeModelResource> decoded;
            std::exception_ptr failure;
            auto const started=std::chrono::steady_clock::now();
            try {decoded.emplace(decode_(name));}
            catch(...) {failure=std::current_exception();}
            auto const elapsed=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-started).count();
            if(elapsed>100.0)
                std::fprintf(stderr,
                    "[PERF] [engine.assets] native_async_preload=%s ms=%.2f"
                    " status=%s\n",name.c_str(),elapsed,
                    failure?"failed":"ready");
            std::lock_guard<std::mutex> guard(mutex_);
            if(stopping_)return;
            auto it=jobs_.find(name);
            if(it==jobs_.end())continue;
            it->second.result=std::move(decoded);
            it->second.error=failure;
            it->second.status=failure?Status::failed:Status::ready;
        }
    }
    Decoder decode_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::string> pending_;
    std::unordered_map<std::string,Job> jobs_;
    std::thread worker_;
    bool stopping_=false;
};
} // namespace northstarcpp::viewport_detail
