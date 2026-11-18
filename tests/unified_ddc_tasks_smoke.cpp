#include <northstarcpp/derived_data_cache.hpp>
#include <northstarcpp/task_scheduler.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(c) do {if(!(c))throw std::runtime_error(#c);}while(false)

int main() {
    using namespace northstarcpp;
    using namespace std::chrono_literals;

    auto a=derived_data_cache::content_key(
        "NEF8/drawable",2,"hello-nef8","import=full;lod=cinematic");
    auto b=derived_data_cache::content_key(
        "NEF8/drawable",3,"hello-nef8","import=full;lod=cinematic");
    auto c=derived_data_cache::content_key(
        "shader/spirv",2,"hello-nef8","import=full;lod=cinematic");
    CHECK(a.size()==32 && a!=b && a!=c);
    CHECK(a==derived_data_cache::content_key(
        "NEF8/drawable",2,"hello-nef8","import=full;lod=cinematic"));

    auto const directory=std::filesystem::temp_directory_path()/
        ("northstar-unified-ddc-"+a);
    auto const file=directory/(a+".blob");
    std::error_code ec;
    std::filesystem::remove_all(directory,ec);
    CHECK(!derived_data_cache::read_blob(file,64));
    derived_data_cache::publish_blob(file,"payload");
    auto hit=derived_data_cache::read_blob(file,64);
    CHECK(hit && *hit=="payload");
    CHECK(!derived_data_cache::read_blob(file,6));
    std::atomic<bool> cancelled{true};
    bool refused=false;
    try {derived_data_cache::publish_blob(directory/"cancelled.blob","x",&cancelled);}
    catch(std::runtime_error const&) {refused=true;}
    CHECK(refused);
    CHECK(!std::filesystem::exists(directory/"cancelled.blob"));
    std::filesystem::remove_all(directory,ec);

    // One occupied worker, two queued priorities, deterministic scheduling.
    task_scheduler::TaskScheduler pool(1,2);
    std::promise<void> started;
    std::promise<void> release;
    auto unblock=release.get_future().share();
    auto first=pool.submit(task_scheduler::Priority::normal,{},[&]{
        started.set_value();
        unblock.wait();
        return 5;
    });
    started.get_future().wait();
    std::vector<int> order;
    auto low=pool.submit(task_scheduler::Priority::background,{},[&]{
        order.push_back(1);return 1;
    });
    auto high=pool.submit(task_scheduler::Priority::high,{},[&]{
        order.push_back(2);return 2;
    });
    CHECK(pool.pending()==2);
    bool saturated=false;
    try {
        (void)pool.submit(task_scheduler::Priority::high,{},[]{return 9;});
    } catch(std::runtime_error const&) {saturated=true;}
    CHECK(saturated);
    release.set_value();
    CHECK(first.get()==5);
    CHECK(high.get()==2);
    CHECK(low.get()==1);
    CHECK(order.size()==2 && order[0]==2 && order[1]==1);

    refused=false;
    auto token=std::make_shared<std::atomic<bool>>(true);
    auto canceled_task=pool.submit(task_scheduler::Priority::normal,token,[]{return 17;});
    try {(void)canceled_task.get();}catch(task_scheduler::Cancelled const&) {refused=true;}
    CHECK(refused);

    auto no_result=pool.submit(task_scheduler::Priority::normal,{},[]{});
    no_result.get();
}
