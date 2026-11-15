// Bounded NEF8 metadata cache: avoids repeated disk reads + NEF8
// decompression across refreshes without retaining image/mesh payloads.
#include "asset_catalog_nef8_cache.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace northstarcpp::editor::detail {
namespace fs=std::filesystem;
namespace {
using EntryList=std::vector<shared_assets::DictionaryEntry>;
struct Entry {
    fs::file_time_type modified;
    std::uintmax_t byte_size=0;
    std::shared_ptr<const EntryList> decoded;
    std::uint64_t last_used=0;
};
struct Cache {
    std::mutex mutex;
    std::unordered_map<std::string,Entry> values;
    std::uint64_t sequence=0;
    std::size_t total_children=0;
};
Cache& process_cache() {
    static Cache result;
    return result;
}
constexpr std::size_t max_records=128;
constexpr std::size_t max_children=32768;
} // namespace

IndexedDictionary inspect_dictionary(fs::path const& source,
                                     std::string const& relative,
                                     std::uintmax_t byte_size) {
    if(byte_size<128 || byte_size>64ull*1024ull*1024ull)
        throw std::runtime_error("NEF8 source size outside catalog limits");
    std::error_code error;
    auto const stamp=fs::last_write_time(source,error);
    bool const stable=!error;
    auto const key=source.lexically_normal().generic_u8string();
    auto& cache=process_cache();
    if(stable) {
        std::lock_guard<std::mutex> lock(cache.mutex);
        auto entry=cache.values.find(key);
        if(entry!=cache.values.end() &&
           entry->second.byte_size==byte_size &&
           entry->second.modified==stamp) {
            entry->second.last_used=++cache.sequence;
            return {entry->second.decoded,true};
        }
    }
    std::ifstream input(source,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read NEF8 dictionary");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byte_size));
    if(!input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Incomplete NEF8 dictionary");
    auto decoded=std::make_shared<const EntryList>(
        shared_assets::list_dictionary_entries(bytes,relative));
    // Only cache a complete decode of a stable file. Never cache a parse
    // error; a corrupt source remains visible and can be repaired/reloaded.
    const auto updated=fs::last_write_time(source,error);
    if(stable && !error && updated==stamp &&
       decoded->size()<=max_children) {
        std::lock_guard<std::mutex> lock(cache.mutex);
        auto previous=cache.values.find(key);
        if(previous!=cache.values.end()) {
            cache.total_children-=previous->second.decoded->size();
            cache.values.erase(previous);
        }
        while(!cache.values.empty() &&
              (cache.values.size()>=max_records ||
               cache.total_children+decoded->size()>max_children)) {
            auto oldest=std::min_element(cache.values.begin(),cache.values.end(),
                [](auto const& a,auto const& b) {
                    return a.second.last_used<b.second.last_used;
                });
            cache.total_children-=oldest->second.decoded->size();
            cache.values.erase(oldest);
        }
        cache.total_children+=decoded->size();
        cache.values.emplace(key,Entry{stamp,byte_size,decoded,++cache.sequence});
    }
    return {std::move(decoded),false};
}
} // namespace northstarcpp::editor::detail
