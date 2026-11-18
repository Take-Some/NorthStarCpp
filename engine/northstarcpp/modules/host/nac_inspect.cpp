// Bounded NAC metadata inspection from the common NEF8 envelope.
// Reads only 128-byte header and JSON metadata, never expands audio samples.
#include <northstarcpp/nac.hpp>
#include "nac_nef8_schema.hpp"
#include "nac_wire.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace northstarcpp::nac {
bool inspect_file(std::filesystem::path const& path,
                  std::vector<TrackInfo>& tracks,std::string& error) {
    tracks.clear();error.clear();
    try {
        namespace fs=std::filesystem;
        std::error_code ec;
        auto const size=fs::file_size(path,ec);
        if(ec || size<schema::nef8_header || size>max_container_bytes)
            throw std::runtime_error("NAC NEF8: unreadable container size");
        std::ifstream input(path,std::ios::binary);
        std::array<std::uint8_t,schema::nef8_header> head{};
        if(!input || !input.read(reinterpret_cast<char*>(head.data()),
                                 static_cast<std::streamsize>(head.size())) ||
           std::memcmp(head.data(),"NEF8",4))
            throw std::runtime_error("NAC NEF8: invalid or truncated header");
        auto const get=[&](std::size_t offset,int length) {
            return wire::read(head.data()+offset,length);
        };
        auto const offset=get(16,8), compressed=get(24,8);
        auto const expanded=get(32,8), count=get(40,8);
        auto const meta_offset=get(48,8), meta_bytes=get(56,8);
        if(get(4,2)!=1 || get(6,2)!=schema::nef8_header ||
           get(8,2)!=schema::nef8_kind_audio || get(10,2)!=1 ||
           get(12,2)!=version || get(14,2)!=0 || get(112,8)!=1 ||
           !count || count>schema::max_tracks ||
           !expanded || expanded>schema::max_body ||
           meta_offset!=schema::nef8_header || !meta_bytes ||
           meta_bytes>8ull*1024*1024 || offset!=meta_offset+meta_bytes ||
           offset>size || compressed!=size-offset || !compressed)
            throw std::runtime_error("NAC NEF8: invalid audio envelope");
        std::string metadata(static_cast<std::size_t>(meta_bytes),'\0');
        if(!input.read(metadata.data(),static_cast<std::streamsize>(meta_bytes)))
            throw std::runtime_error("NAC NEF8: truncated metadata");
        auto tree=shared_assets::detail::parse_json(metadata);
        auto const records=schema::parse_metadata(tree,expanded,count);
        std::vector<TrackInfo> found;
        found.reserve(records.size());
        for(auto const& r:records)found.push_back(r.info);
        tracks=std::move(found);
        return true;
    } catch(std::exception const& e) {
        tracks.clear();error=e.what();return false;
    }
}
} // namespace northstarcpp::nac
