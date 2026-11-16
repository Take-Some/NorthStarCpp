#include "nac_nef8_schema.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <sstream>

namespace northstarcpp::nac::schema {
std::string serialize_metadata(std::vector<Record> const& records) {
    shared_assets::detail::Tree root, array;
    root.put("schema","northstar.nac.nef8.v1");
    for(auto const& r:records) {
        shared_assets::detail::Tree item;
        item.put("id",r.info.id);
        item.put("name",r.info.name);
        item.put("codec",codec_pcm16);
        item.put("sample_rate",r.info.sample_rate);
        item.put("channels",r.info.channels);
        item.put("frames",r.info.frames);
        item.put("offset",r.offset);
        item.put("bytes",r.info.payload_bytes);
        item.put("crc32",r.checksum);
        item.put("looping",r.info.looping?1:0);
        item.put("loop_start",r.loop_start);
        item.put("loop_end",r.loop_end);
        array.push_back({"",std::move(item)});
    }
    root.add_child("tracks",array);
    std::ostringstream output;
    boost::property_tree::write_json(output,root,false);
    return output.str();
}
} // namespace northstarcpp::nac::schema
