// Derived-data cache for reproducible, versioned V-HACD collision hulls.
// Key includes exact source mesh bytes, split count, settings and algorithm id.
#pragma once
#include <northstarcpp/collision_hulls.hpp>
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
    std::uint64_t a=14695981039346656037ull,b=7809847782465536322ull;
    auto add=[&](std::uint8_t byte) {
        a=(a^byte)*1099511628211ull;
        b=(b^byte)*14029467366897019727ull;
    };
    auto u32=[&](std::uint32_t n) {
        for(int shift=0;shift<32;shift+=8)add(std::uint8_t(n>>shift));
    };
    for(char c:std::string("NorthStar/VHACD/4.1/NCH1/maxverts64/vol1"))
        add(static_cast<std::uint8_t>(c));
    u32(splits);u32(resolution);
    for(char c:mesh.source)add(static_cast<std::uint8_t>(c));
    u32(std::uint32_t(mesh.xyz.size()));
    for(float value:mesh.xyz) {
        std::uint32_t bits;
        std::memcpy(&bits,&value,sizeof(bits));
        u32(bits);
    }
    u32(std::uint32_t(mesh.indices.size()));
    for(auto index:mesh.indices)u32(index);
    std::ostringstream hex;
    hex<<std::hex<<std::setfill('0')<<std::setw(16)<<a
       <<std::setw(16)<<b;
    return hex.str();
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
    {
        std::error_code error;
        if(std::filesystem::exists(file,error) &&
           std::filesystem::is_regular_file(file,error)) {
            auto const size=std::filesystem::file_size(file,error);
            if(!error && size<=collision_hulls::max_bytes) {
                std::ifstream input(file,std::ios::binary);
                if(input) {
                    std::string bytes((std::istreambuf_iterator<char>(input)),
                                      std::istreambuf_iterator<char>());
                    auto decoded=collision_hulls::decode(bytes);
                    if(decoded && decoded->source==mesh.source &&
                       decoded->splits_num==splits) {
                        if(progress)progress->store(100);
                        return {std::move(*decoded),true};
                    }
                }
            }
        }
    }
    auto cooked=collision_hulls::cook(mesh,splits,resolution,progress,cancel);
    if(cancel&&cancel->load())throw std::runtime_error("Collision cook cancelled");
    auto bytes=collision_hulls::encode(cooked);
    std::error_code error;
    std::filesystem::create_directories(cache_dir,error);
    if(error)throw std::runtime_error("Cannot create collision DDC directory");
    static std::atomic<std::uint64_t> sequence{0};
    auto const tmp=cache_dir/
        (cache_key+"."+std::to_string(
            std::hash<std::thread::id>{}(std::this_thread::get_id()))+
         "."+std::to_string(sequence.fetch_add(1))+".tmp");
    {
        std::ofstream output(tmp,std::ios::binary|std::ios::trunc);
        if(!output)throw std::runtime_error("Cannot write collision DDC");
        output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if(!output) {
            std::filesystem::remove(tmp,error);
            throw std::runtime_error("Incomplete collision DDC write");
        }
    }
    if(cancel&&cancel->load()) {
        std::filesystem::remove(tmp,error);
        throw std::runtime_error("Collision cook cancelled");
    }
    std::filesystem::rename(tmp,file,error);
    if(error) {
        // Concurrent writer may have published the identical content first.
        if(!std::filesystem::exists(file)) {
            std::filesystem::remove(tmp,error);
            throw std::runtime_error("Cannot publish collision DDC");
        }
        std::filesystem::remove(tmp,error);
    }
    if(progress)progress->store(100);
    return {std::move(cooked),false};
}
} // namespace northstarcpp::collision_cook_cache
