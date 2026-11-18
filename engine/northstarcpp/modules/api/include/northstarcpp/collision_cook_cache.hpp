// Derived-data cache for reproducible, versioned V-HACD collision hulls.
// Key includes exact source mesh bytes, split count, settings and algorithm id.
#pragma once
#include <northstarcpp/collision_hulls.hpp>
#include <northstarcpp/derived_data_cache.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
namespace northstarcpp::collision_cook_cache {
struct Result {
    collision_hulls::Set hulls;
    bool hit=false;
};
inline std::string key(collision_mesh::Mesh const& mesh,std::uint32_t splits,
                       std::uint32_t resolution) {
    if(!collision_mesh::valid(mesh)||splits<1||splits>16||
       resolution<10000||resolution>1000000)
        throw std::invalid_argument("Invalid collision cache request");
    derived_data_cache::KeyBuilder digest;
    digest.bytes("NorthStar/VHACD/4.1/NCH1/maxverts64/vol1");
    digest.u32(splits);
    digest.u32(resolution);
    digest.bytes(mesh.source);
    digest.u32(static_cast<std::uint32_t>(mesh.xyz.size()));
    for(float value:mesh.xyz)digest.float32(value);
    digest.u32(static_cast<std::uint32_t>(mesh.indices.size()));
    for(auto index:mesh.indices)digest.u32(index);
    return digest.hex();
}
inline Result load_or_cook(collision_mesh::Mesh const& mesh,
                           std::uint32_t splits,
                           std::filesystem::path const& cache_dir,
                           std::uint32_t resolution=100000,
                           std::atomic<int>* progress=nullptr,
                           std::atomic<bool>* cancel=nullptr) {
    if(cancel&&cancel->load())throw std::runtime_error("Collision cook cancelled");
    if(cache_dir.empty())throw std::invalid_argument("Collision cache path empty");
    auto const cache_key=key(mesh,splits,resolution);
    auto const file=cache_dir/(cache_key+".nch");
    if(auto bytes=derived_data_cache::read_blob(
           file,collision_hulls::max_bytes)) {
        auto decoded=collision_hulls::decode(*bytes);
        if(decoded && decoded->source==mesh.source &&
           decoded->splits_num==splits) {
            if(progress)progress->store(100);
            return {std::move(*decoded),true};
        }
    }
    auto cooked=collision_hulls::cook(mesh,splits,resolution,progress,cancel);
    if(cancel&&cancel->load())throw std::runtime_error("Collision cook cancelled");
    auto bytes=collision_hulls::encode(cooked);
    derived_data_cache::publish_blob(file,bytes,cancel);
    if(progress)progress->store(100);
    return {std::move(cooked),false};
}
} // namespace northstarcpp::collision_cook_cache
