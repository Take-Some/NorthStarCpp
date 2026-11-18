// Regression and isolated CPU benchmark for transient Viewport mesh residency.
#include "../engine/northstarcpp/modules/viewport/viewport_mesh_residency.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using northstarcpp::viewport_detail::ActiveMeshRefs;
namespace {
bool is_asset(std::string const& path) {
    auto const extension=path.substr(0,path.find('@'));
    return extension.size()>=4 &&
        (extension.compare(extension.size()-4,4,".obj")==0 ||
         extension.compare(extension.size()-4,4,".ydd")==0 ||
         extension.compare(extension.size()-4,4,".ydr")==0);
}
void run_checks() {
    std::vector<std::string> names={
        "",".obj","dir/a.obj","dir/a.obj@part0",
        "dir/a.ydd@model_0","dir/a.ydr","dir/a.ydr@part1",
        "dir/a.ymt","dir/a.ycd","dir/a.objx",
        "dir/a.OBJ","dir/a.obj@part1","dir/a.obj",
        "@.obj","dir/a.obj@part0"};
    std::unordered_set<std::string> previous;
    ActiveMeshRefs next;
    next.begin(names.size());
    for(auto const& s:names) {
        if(is_asset(s))previous.insert(s);
        next.include(s);
    }
    next.finish();
    if(next.size()!=previous.size())
        throw std::runtime_error("Duplicate removal / extension mismatch");
    for(auto const& s:names)
        if(next.contains(s)!=(previous.count(s)>0))
            throw std::runtime_error("Viewport residency outcome changed");
    next.clear();
    if(next.size()!=0 || next.capacity()<names.size())
        throw std::runtime_error("Scratch storage was not retained");
    next.begin(names.size());
    next.include(names.front());
    next.finish();
    if(next.size()!=0)
        throw std::runtime_error("Stale mesh residency escaped reset");
}
}
int main() {
    run_checks();
    std::vector<std::string> assets;
    for(int i=0;i<10000;++i) {
        auto path="models/district_"+std::to_string(i%70)+"/asset_"+
            std::to_string(i)+((i%9==0)?".ymat":(i%2==0)?".ydr@lod0":".obj");
        assets.push_back(path);
    }
    constexpr int passes=90;
    using clock=std::chrono::steady_clock;
    std::unordered_set<std::string> old;
    ActiveMeshRefs optimized;
    volatile std::uint64_t old_total=0,new_total=0;
    auto bench_old=[&]{
        auto start=clock::now();
        for(int rep=0;rep<passes;++rep) {
            std::unordered_set<std::string> active;
            for(auto const& path:assets)
                if(is_asset(path))active.insert(path);
            for(auto const& path:assets)
                old_total+=active.count(path);
        }
        return std::chrono::duration<double,std::milli>(clock::now()-start).count();
    };
    auto bench_new=[&]{
        auto start=clock::now();
        for(int rep=0;rep<passes;++rep) {
            optimized.begin(assets.size());
            for(auto const& path:assets)optimized.include(path);
            optimized.finish();
            for(auto const& path:assets)
                new_total+=optimized.contains(path);
            optimized.clear();
        }
        return std::chrono::duration<double,std::milli>(clock::now()-start).count();
    };
    auto const old_ms=bench_old();
    auto const new_ms=bench_new();
    if(old_total!=new_total)
        throw std::runtime_error("Benchmark behavior diverged");
    std::printf("Viewport mesh residency: assets=%zu passes=%d old=%.3fms new=%.3fms speedup=%.2fx equal=%llu\n",
        assets.size(),passes,old_ms,new_ms,old_ms/new_ms,
        static_cast<unsigned long long>(new_total));
    return 0;
}
