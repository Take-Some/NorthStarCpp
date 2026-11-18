#include "../engine/northstarcpp/modules/viewport/viewport_model_preloader.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <optional>
#include <exception>
using northstarcpp::viewport_detail::NativeModelPreloader;
using northstarcpp::viewport_detail::safe_native_preload_reference;
int main() {
    using namespace std::chrono_literals;
    if(!safe_native_preload_reference("models/mahir_mt28.ydr",{}) ||
       !safe_native_preload_reference("models/mahir_mt28.ydd@mesh",{}) ||
       safe_native_preload_reference("../secrets.ydr",{}) ||
       safe_native_preload_reference("C:/bad.ydr",{}) ||
       safe_native_preload_reference("models/a.bin",{}))return 2;
    std::atomic<int> decoded{0};
    std::atomic<bool> gate{false};
    NativeModelPreloader loader([&](std::string const& key) {
        while(!gate.load())std::this_thread::sleep_for(1ms);
        decoded.fetch_add(1);
        if(key=="broken.ydr")throw std::runtime_error("invalid source");
        northstarcpp::NativeModelResource result;
        result.mesh.indices={0,1,2};
        result.mesh.sections.push_back({"original","",0,3,false});
        return result;
    });
    auto started=std::chrono::steady_clock::now();
    if(!loader.enqueue("model.ydr",true) ||
       !loader.enqueue("model.ydr",true) ||
       !loader.enqueue("broken.ydr"))return 3;
    auto elapsed=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-started).count();
    if(elapsed>100.)return 4; // no waiting on the slow decode
    std::optional<northstarcpp::NativeModelResource> model;
    std::exception_ptr failure;
    if(loader.take("model.ydr",model,failure)!=NativeModelPreloader::Status::pending)
        return 5;
    gate=true;
    bool loaded=false,failed=false;
    for(int i=0;i<400;++i) {
        auto status=loader.take("model.ydr",model,failure);
        if(status==NativeModelPreloader::Status::ready) {
            loaded=model && model->mesh.indices.size()==3;
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    if(!loaded)return 6; // second queued decode may already have completed
    for(int i=0;i<400;++i) {
        auto status=loader.take("broken.ydr",model,failure);
        if(status==NativeModelPreloader::Status::failed) {
            try {std::rethrow_exception(failure);}
            catch(std::runtime_error const&) {failed=true;}
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    if(!failed || decoded!=2)return 7;
    loader.shutdown();
    std::puts("Native model background preloader dedup, nonblocking, failure, shutdown: PASS");
}
