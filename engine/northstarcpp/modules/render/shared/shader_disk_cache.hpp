// Internal on-disk cache for compiled shader bytecode and native PSO blobs.
// This is NOT a file format or plugin ABI. Stored objects are fully validated
// before use by their respective D3D provider; corrupt entries are ignored.
#pragma once
#include "../../shared/derived_data_file.hpp"
#include <cwchar>
#include <iomanip>
#include <sstream>
namespace northstarcpp::render::shader_disk_cache {
namespace fs=std::filesystem;
constexpr std::uint64_t max_payload=8ull*1024ull*1024ull;
using derived_data_file::hash;
inline bool disabled() {
    wchar_t flag[8]{};
    auto const n=GetEnvironmentVariableW(
        L"NORTHSTARCPP_SHADER_CACHE_DISABLE",flag,8);
    return n!=0 && n<8 && flag[0]==L'1';
}
inline std::optional<fs::path> root() {
    if(disabled())return {};
    if(auto path=derived_data_file::absolute_environment_path(L"NORTHSTARCPP_SHADER_CACHE_DIR"))
        return *path/"v1";
    if(auto path=derived_data_file::absolute_environment_path(L"LOCALAPPDATA"))
        return *path/"NorthStarCpp"/"ShaderCache"/"v1";
    return {};
}
inline fs::path entry_path(fs::path const& base,std::string const& backend,
                           std::string const& key) {
    std::ostringstream basename;
    basename<<std::hex<<std::setfill('0')<<std::setw(16)<<hash(key);
    return base/backend/(basename.str()+".bin");
}
inline std::optional<std::vector<std::uint8_t>> read(
    std::string const& backend,std::string const& key) {
    auto base=root();
    if(!base)return {};
    return derived_data_file::read(entry_path(*base,backend,key),key,max_payload);
}
inline void write(std::string const& backend,std::string const& key,
                  void const* raw,std::size_t length) noexcept {
    try {
        if(auto base=root())
            derived_data_file::write(entry_path(*base,backend,key),key,raw,length,max_payload);
    }catch(...) {}
}
inline void erase(std::string const& backend,std::string const& key) noexcept {
    try {
        auto base=root();
        if(!base)return;
        std::error_code error;
        fs::remove(entry_path(*base,backend,key),error);
    }catch(...) {}
}
} // namespace northstarcpp::render::shader_disk_cache
