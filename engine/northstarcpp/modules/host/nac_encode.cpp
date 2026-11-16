// NAC v1 writes an audio-kind NEF8 ListFile, never a competing NAC1 envelope.
#include <northstarcpp/nac.hpp>
#include "nac_nef8_schema.hpp"
#include "nac_wire.hpp"
#include <stdexcept>
#include <unordered_set>

namespace northstarcpp::nac {
bool encode(std::vector<Track> const& tracks,std::vector<std::uint8_t>& bytes,
            std::string& error) {
    bytes.clear();error.clear();
    try {
        if(tracks.empty() || tracks.size()>schema::max_tracks)
            throw std::runtime_error("NAC NEF8: invalid track count");
        std::unordered_set<std::uint32_t> ids;
        std::unordered_set<std::string> names;
        std::vector<std::uint8_t> pcm;
        std::vector<schema::Record> records;
        records.reserve(tracks.size());
        for(auto const& track:tracks) {
            wire::validate(track);
            if(!ids.insert(track.id).second ||
               !names.insert(track.name).second)
                throw std::runtime_error("NAC NEF8: duplicate ID or track name");
            auto const position=schema::align16(pcm.size());
            auto const length=track.samples.size()*sizeof(std::int16_t);
            if(position+length>schema::max_body)
                throw std::runtime_error("NAC NEF8: expanded body exceeds 64 MiB");
            pcm.resize(static_cast<std::size_t>(position+length),0);
            for(std::size_t s=0;s<track.samples.size();++s)
                wire::write(pcm.data()+position+s*2,
                            static_cast<std::uint16_t>(track.samples[s]),2);
            schema::Record record;
            record.info.id=track.id;
            record.info.name=track.name;
            record.info.sample_rate=track.sample_rate;
            record.info.channels=track.channels;
            record.info.frames=track.samples.size()/track.channels;
            record.info.payload_bytes=length;
            record.info.looping=track.looping;
            record.offset=position;
            record.loop_start=track.loop_start;
            record.loop_end=track.loop_end;
            record.checksum=wire::crc(pcm.data()+position,length);
            records.push_back(std::move(record));
        }
        auto const metadata=schema::serialize_metadata(records);
        bytes=shared_assets::detail::serialize_list_file(
            schema::nef8_kind_audio,version,records.size(),metadata,pcm);
        return true;
    } catch(std::exception const& e) {
        bytes.clear();error=e.what();return false;
    }
}
} // namespace northstarcpp::nac
