// NorthStar Pack reader. The layout is documented in docs/NORTHSTAR_PAK.md.
// Little-endian index, no host pointer serialization or implicit decompression.
#include "nsp_archive.hpp"
#include "asset_paths.hpp"
#include <boost/crc.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace northstarcpp::vfs {
namespace {
constexpr std::array<std::uint8_t,8> magic{{'N','S','P','K','\r','\n',0x1a,'\n'}};
constexpr std::uint64_t header_bytes=40;
constexpr std::uint64_t max_index_bytes=64ull*1024*1024;
constexpr std::uint32_t max_entries=1'000'000;
constexpr std::uint64_t record_bytes=28;

[[noreturn]] void invalid(char const* message) {
    throw std::runtime_error(std::string("Invalid NSP archive: ")+message);
}
bool fits(std::uint64_t total,std::uint64_t start,std::uint64_t length) {
    return start<=total && length<=total-start;
}
std::uint64_t number(std::uint8_t const* bytes,std::size_t count) {
    std::uint64_t value=0;
    for(std::size_t i=count;i>0;--i) value=(value<<8)|bytes[i-1];
    return value;
}
void read_at(std::ifstream& file,std::uint64_t offset,
             std::uint8_t* output,std::size_t size) {
    if(!size)return;
    if(offset>static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
        invalid("offset out of range");
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    if(!file || !file.read(reinterpret_cast<char*>(output),
                          static_cast<std::streamsize>(size)))
        invalid("truncated asset");
}
} // namespace

NspArchive::NspArchive(std::filesystem::path const& path)
    : input(path,std::ios::binary) {
    if(!input) invalid("cannot open file");
    input.seekg(0,std::ios::end);
    auto const end=input.tellg();
    if(end<0)invalid("cannot measure archive");
    auto const total=static_cast<std::uint64_t>(end);
    if(total<header_bytes)invalid("truncated header");
    std::array<std::uint8_t,header_bytes> h{};
    read_at(input,0,h.data(),h.size());
    if(!std::equal(magic.begin(),magic.end(),h.begin()))
        invalid("bad magic");
    if(number(h.data()+8,4)!=1 || number(h.data()+12,4)!=0 ||
       number(h.data()+36,4)!=0)
        invalid("unsupported version or flags");
    auto const index_bytes=number(h.data()+16,8);
    auto const payload=number(h.data()+24,8);
    auto const count=number(h.data()+32,4);
    if(count>max_entries || index_bytes>max_index_bytes ||
       !fits(total,header_bytes,index_bytes) ||
       payload<header_bytes+index_bytes || payload>total ||
       index_bytes<count*record_bytes)
        invalid("invalid directory bounds");
    std::vector<std::uint8_t> table(static_cast<std::size_t>(index_bytes));
    read_at(input,header_bytes,table.data(),table.size());
    std::uint64_t cursor=0;
    std::string previous;
    std::uint64_t previous_end=payload;
    for(std::uint32_t i=0;i<count;++i) {
        if(!fits(index_bytes,cursor,record_bytes))invalid("truncated entry");
        auto const* entry=table.data()+cursor;
        auto const name_length=number(entry,2);
        auto const flags=number(entry+2,2);
        auto const offset=number(entry+4,8);
        auto const size=number(entry+12,8);
        auto const checksum=number(entry+20,4);
        auto const reserved=number(entry+24,4);
        cursor+=record_bytes;
        if(!name_length || flags || reserved ||
           !fits(index_bytes,cursor,name_length))
            invalid("invalid index entry");
        std::string name(reinterpret_cast<char const*>(table.data()+cursor),
                         static_cast<std::size_t>(name_length));
        cursor+=name_length;
        try {validate_path(name,false);}
        catch(std::exception const&) {invalid("unsafe virtual asset path");}
        if(i && !(previous<name))invalid("unordered or duplicate asset path");
        if(offset<previous_end || !fits(total,offset,size) ||
           size>max_asset_bytes)
            invalid("file outside payload or unsorted overlap");
        previous_end=offset+size;
        previous=name;
        files.emplace(std::move(name),Entry{offset,size,static_cast<std::uint32_t>(checksum)});
    }
    if(cursor!=index_bytes)invalid("unexpected trailing index bytes");
}

NspArchive::Entry const* NspArchive::find(std::string_view name) const {
    auto const it=files.find(name);
    return it==files.end()?nullptr:&it->second;
}
void NspArchive::read_range(Entry const& entry,std::uint64_t offset,
                            std::uint8_t* output,std::size_t length) const {
    if(offset>entry.size || length>entry.size-offset ||
       (length && !output))invalid("range outside file");
    std::lock_guard<std::mutex> guard(mutex);
    read_at(input,entry.offset+offset,output,length);
}
void NspArchive::read(Entry const& entry,std::uint8_t* output) const {
    if(entry.size>max_asset_bytes)invalid("file too large");
    std::lock_guard<std::mutex> guard(mutex);
    read_at(input,entry.offset,output,static_cast<std::size_t>(entry.size));
    boost::crc_32_type crc;
    if(entry.size)crc.process_bytes(output,static_cast<std::size_t>(entry.size));
    if(crc.checksum()!=entry.crc32)invalid("CRC32 mismatch");
}
} // namespace northstarcpp::vfs
