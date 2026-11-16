#include "nac_nef8_schema.hpp"
#include <set>
#include <stdexcept>

namespace northstarcpp::nac::schema {
std::vector<Record> parse_metadata(
    shared_assets::detail::Tree const& metadata,
    std::uint64_t expanded_bytes,std::uint64_t expected_count) {
    if(metadata.get<std::string>("schema","")!="northstar.nac.nef8.v1")
        throw std::runtime_error("NAC NEF8: unsupported audio schema");
    if(expected_count==0 || expected_count>max_tracks ||
       expanded_bytes==0 || expanded_bytes>max_body)
        throw std::runtime_error("NAC NEF8: invalid track count or size");
    auto const& array=metadata.get_child("tracks");
    if(array.size()!=expected_count)
        throw std::runtime_error("NAC NEF8: track table/count mismatch");
    std::set<std::uint32_t> ids;
    std::set<std::string> names;
    std::vector<Record> records;
    records.reserve(static_cast<std::size_t>(expected_count));
    std::uint64_t end_previous=0;
    for(auto const& node:array) {
        if(!node.first.empty())
            throw std::runtime_error("NAC NEF8: tracks must be array");
        auto const& t=node.second;
        Record r;
        r.info.id=t.get<std::uint32_t>("id");
        r.info.name=t.get<std::string>("name");
        auto const codec=t.get<std::uint32_t>("codec");
        r.info.sample_rate=t.get<std::uint32_t>("sample_rate");
        r.info.channels=t.get<std::uint16_t>("channels");
        r.info.frames=t.get<std::uint64_t>("frames");
        r.offset=t.get<std::uint64_t>("offset");
        r.info.payload_bytes=t.get<std::uint64_t>("bytes");
        r.checksum=t.get<std::uint32_t>("crc32");
        auto const loop=t.get<std::uint32_t>("looping");
        r.loop_start=t.get<std::uint64_t>("loop_start");
        r.loop_end=t.get<std::uint64_t>("loop_end");
        r.info.looping=loop==1;
        if(r.info.name.empty() || r.info.name.size()>47 ||
           r.info.name.find('\0')!=std::string::npos ||
           !names.insert(r.info.name).second ||
           !ids.insert(r.info.id).second || codec!=codec_pcm16 || loop>1 ||
           (r.info.channels!=1 && r.info.channels!=2) ||
           r.info.sample_rate<8000 || r.info.sample_rate>192000 ||
           !r.info.frames || r.info.frames>0x7fffffffu ||
           r.info.frames>max_body/(r.info.channels*2ull) ||
           r.info.payload_bytes!=r.info.frames*r.info.channels*2ull ||
           r.offset!=align16(end_previous) || r.offset>expanded_bytes ||
           r.info.payload_bytes>expanded_bytes-r.offset)
            throw std::runtime_error("NAC NEF8: corrupt track entry");
        auto const end=r.loop_end?r.loop_end:r.info.frames;
        if((r.info.looping && !(r.loop_start<end && end<=r.info.frames)) ||
           (!r.info.looping && (r.loop_start || r.loop_end)))
            throw std::runtime_error("NAC NEF8: invalid loop bounds");
        end_previous=r.offset+r.info.payload_bytes;
        records.push_back(std::move(r));
    }
    if(end_previous!=expanded_bytes)
        throw std::runtime_error("NAC NEF8: trailing PCM data");
    return records;
}
} // namespace northstarcpp::nac::schema
