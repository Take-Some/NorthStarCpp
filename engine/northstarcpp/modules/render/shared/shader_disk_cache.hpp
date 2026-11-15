// Internal on-disk cache for compiled shader bytecode and native PSO blobs.
// This is NOT a file format or plugin ABI. Stored objects are fully validated
// before use by their respective D3D provider; corrupt entries are ignored.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
namespace northstarcpp::render::shader_disk_cache {
namespace fs=std::filesystem;
constexpr std::uint64_t max_payload=8ull*1024ull*1024ull;
inline std::uint64_t hash(void const* raw,std::size_t length) noexcept {
    std::uint64_t result=14695981039346656037ull;
    auto const* data=static_cast<std::uint8_t const*>(raw);
    for(std::size_t i=0;i<length;++i)
        result=(result^data[i])*1099511628211ull;
    return result;
}
inline std::uint64_t hash(std::string const& bytes) noexcept {
    return hash(bytes.data(),bytes.size());
}
inline bool disabled() {
    wchar_t flag[8]{};
    auto const n=GetEnvironmentVariableW(
        L"NORTHSTARCPP_SHADER_CACHE_DISABLE",flag,8);
    return n!=0 && n<8 && flag[0]==L'1';
}
inline std::optional<fs::path> root() {
    if(disabled())return {};
    std::wstring path;
    for(auto const* key:{L"NORTHSTARCPP_SHADER_CACHE_DIR",L"LOCALAPPDATA"}) {
        DWORD const needed=GetEnvironmentVariableW(key,nullptr,0);
        if(needed<2||needed>32767)continue;
        std::wstring buffer(needed,L'\0');
        DWORD const count=GetEnvironmentVariableW(key,buffer.data(),needed);
        if(!count||count>=needed)continue;
        buffer.resize(count);
        fs::path base(buffer);
        if(!base.is_absolute())continue;
        if(std::wcscmp(key,L"LOCALAPPDATA")==0) {
            base/="NorthStarCpp";
            base/="ShaderCache";
        }
        return base/"v1";
    }
    return {};
}
inline fs::path entry_path(fs::path const& base,std::string const& backend,
                           std::string const& key) {
    std::ostringstream basename;
    basename<<std::hex<<std::setfill('0')<<std::setw(16)<<hash(key);
    return base/backend/(basename.str()+".bin");
}
struct Header {
    std::array<char,8> magic{'N','S','P','S','O','0','0','1'};
    std::uint32_t version=1;
    std::uint32_t size=0;
    std::uint64_t key_hash=0;
    std::uint64_t data_hash=0;
};
inline std::optional<std::vector<std::uint8_t>> read(
    std::string const& backend,std::string const& key) {
    auto base=root();
    if(!base)return {};
    try {
        auto path=entry_path(*base,backend,key);
        std::error_code ec;
        auto const size=fs::file_size(path,ec);
        if(ec||size<sizeof(Header)||size>sizeof(Header)+max_payload)return {};
        std::ifstream in(path,std::ios::binary);
        if(!in)return {};
        Header header{};
        if(!in.read(reinterpret_cast<char*>(&header),sizeof(header)))
            return {};
        Header const expected{};
        if(header.magic!=expected.magic||header.version!=expected.version ||
           header.size!=size-sizeof(Header)||header.key_hash!=hash(key) ||
           header.size>max_payload)return {};
        std::vector<std::uint8_t> content(header.size);
        if(!in.read(reinterpret_cast<char*>(content.data()),content.size()) ||
           hash(content.data(),content.size())!=header.data_hash)
            return {};
        return content;
    }catch(...) {return {};}
}
inline void write(std::string const& backend,std::string const& key,
                  void const* raw,std::size_t length) noexcept {
    if(!raw||!length||length>max_payload)return;
    try {
        auto base=root();
        if(!base)return;
        auto target=entry_path(*base,backend,key);
        std::error_code ec;
        fs::create_directories(target.parent_path(),ec);
        if(ec)return;
        auto tmp=target;
        tmp+=std::wstring(L".")+
            std::to_wstring(GetCurrentProcessId())+L"."+
            std::to_wstring(GetCurrentThreadId())+L".tmp";
        Header header{};
        header.key_hash=hash(key);
        header.size=static_cast<std::uint32_t>(length);
        header.data_hash=hash(raw,length);
        {
            std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
            if(!out)return;
            out.write(reinterpret_cast<char const*>(&header),sizeof(header));
            out.write(reinterpret_cast<char const*>(raw),length);
            out.flush();
            if(!out.good()) {out.close();fs::remove(tmp,ec);return;}
        }
        if(!MoveFileExW(tmp.c_str(),target.c_str(),
                        MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            fs::remove(tmp,ec);
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
