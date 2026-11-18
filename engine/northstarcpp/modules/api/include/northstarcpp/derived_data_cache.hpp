#pragma once
// Shared original NorthStar derived-data keying and atomic blob IO contract.
// Legacy collision digests remain byte-for-byte compatible.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace northstarcpp::derived_data_cache {

class KeyBuilder {
public:
    void byte(std::uint8_t value) noexcept {
        a_=(a_^value)*1099511628211ull;
        b_=(b_^value)*14029467366897019727ull;
    }
    void bytes(std::string_view value) noexcept {
        for(unsigned char c:value)byte(c);
    }
    void u32(std::uint32_t value) noexcept {
        for(int shift=0;shift<32;shift+=8)
            byte(static_cast<std::uint8_t>(value>>shift));
    }
    void float32(float value) noexcept {
        std::uint32_t bits=0;
        static_assert(sizeof(bits)==sizeof(value),"Float32 required");
        std::memcpy(&bits,&value,sizeof(bits));
        u32(bits);
    }
    std::string hex() const {
        static constexpr char digits[]="0123456789abcdef";
        std::array<char,32> result{};
        for(std::size_t i=0;i<16;++i) {
            auto const shift=static_cast<unsigned>((15-i)*4);
            result[i]=digits[(a_>>shift)&15u];
            result[16+i]=digits[(b_>>shift)&15u];
        }
        return std::string(result.data(),result.size());
    }
private:
    std::uint64_t a_=14695981039346656037ull;
    std::uint64_t b_=7809847782465536322ull;
};

// Namespace, format revision and options all affect cached derivatives.
// Callers must validate decoded payload structure and source identity.
inline std::string content_key(std::string_view domain,std::uint32_t revision,
                               std::string_view source,std::string_view options) {
    if(domain.empty() || domain.size()>128 || !revision)
        throw std::invalid_argument("Invalid DDC domain or revision");
    if(source.size()>std::numeric_limits<std::uint32_t>::max() ||
       options.size()>std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("DDC input too large");
    KeyBuilder key;
    key.bytes("NorthStar/DDC/v1/");
    key.u32(static_cast<std::uint32_t>(domain.size()));
    key.bytes(domain);
    key.u32(revision);
    key.u32(static_cast<std::uint32_t>(options.size()));
    key.bytes(options);
    key.u32(static_cast<std::uint32_t>(source.size()));
    key.bytes(source);
    return key.hex();
}

// A read returns no data on an absent, too-large, truncated or unreadable blob.
// Its caller checks the domain-specific decoded content before using the hit.
inline std::optional<std::string> read_blob(
    std::filesystem::path const& file,std::uint64_t max_bytes) {
    std::error_code ec;
    if(!std::filesystem::is_regular_file(file,ec) || ec)return {};
    auto const size=std::filesystem::file_size(file,ec);
    if(ec || size>max_bytes ||
       size>static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return {};
    std::ifstream input(file,std::ios::binary);
    if(!input)return {};
    std::string bytes(static_cast<std::size_t>(size),'\0');
    if(size && !input.read(bytes.data(),static_cast<std::streamsize>(size)))
        return {};
    return bytes;
}

// Files are published via a unique sibling temporary path. A pre-existing
// cache entry is never truncated; concurrent writers may race to publish the
// same deterministic result. The decoder must validate hits before use.
inline void publish_blob(
    std::filesystem::path const& file,std::string_view payload,
    std::atomic<bool>* cancel=nullptr) {
    if(cancel && cancel->load(std::memory_order_relaxed))
        throw std::runtime_error("Derived data operation cancelled");
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(),ec);
    if(ec)throw std::runtime_error("Cannot create DDC directory");
    static std::atomic<std::uint64_t> next{0};
    auto const temp=file.string()+"."+
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()))+
        "."+std::to_string(next.fetch_add(1,std::memory_order_relaxed))+".tmp";
    {
        std::ofstream output(temp,std::ios::binary|std::ios::trunc);
        if(!output)throw std::runtime_error("Cannot write DDC");
        output.write(payload.data(),static_cast<std::streamsize>(payload.size()));
        output.flush();
        if(!output) {
            std::filesystem::remove(temp,ec);
            throw std::runtime_error("Incomplete DDC write");
        }
    }
    if(cancel && cancel->load(std::memory_order_relaxed)) {
        std::filesystem::remove(temp,ec);
        throw std::runtime_error("Derived data operation cancelled");
    }
    std::filesystem::rename(temp,file,ec);
    if(ec) {
        // Another thread/process may already have completed the same key.
        std::error_code exist_error;
        if(!std::filesystem::is_regular_file(file,exist_error)) {
            std::filesystem::remove(temp,exist_error);
            throw std::runtime_error("Cannot publish DDC");
        }
        std::filesystem::remove(temp,exist_error);
    }
}
} // namespace northstarcpp::derived_data_cache
