// NAC reads the standard NEF8 ListFile envelope and validates audio records.
#include <northstarcpp/nac.hpp>
#include "nac_nef8_schema.hpp"
#include "nac_wire.hpp"
#include <stdexcept>
#include <utility>

namespace northstarcpp::nac {
bool decode(std::uint8_t const* data,std::size_t size,
            std::vector<Track>& tracks,std::string& error) {
    tracks.clear();error.clear();
    try {
        if(!data || size<schema::nef8_header || size>max_container_bytes)
            throw std::runtime_error("NAC NEF8: missing or oversized file");
        // Shared validated NEF8 reader: bounded raw inflate and BLAKE3 hash.
        std::vector<std::uint8_t> const input(data,data+size);
        auto const file=shared_assets::detail::parse_list_file(input);
        if(file.kind!=schema::nef8_kind_audio)
            throw std::runtime_error("NAC NEF8: not an audio container");
        auto const records=schema::parse_metadata(
            file.metadata,file.body.size(),file.entries);
        std::vector<Track> decoded;
        decoded.reserve(records.size());
        for(auto const& rec:records) {
            auto const* payload=file.body.data()+rec.offset;
            if(wire::crc(payload,rec.info.payload_bytes)!=rec.checksum)
                throw std::runtime_error("NAC NEF8: audio CRC32 mismatch");
            Track t;
            t.id=rec.info.id;
            t.name=rec.info.name;
            t.sample_rate=rec.info.sample_rate;
            t.channels=rec.info.channels;
            t.looping=rec.info.looping;
            t.loop_start=rec.loop_start;
            t.loop_end=rec.loop_end;
            t.samples.resize(static_cast<std::size_t>(
                rec.info.frames*rec.info.channels));
            for(std::size_t i=0;i<t.samples.size();++i)
                t.samples[i]=static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(wire::read(payload+i*2,2)));
            wire::validate(t);
            decoded.push_back(std::move(t));
        }
        tracks=std::move(decoded);
        return true;
    } catch(std::exception const& e) {
        tracks.clear();error=e.what();return false;
    }
}
} // namespace northstarcpp::nac
