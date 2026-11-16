// NorthStar shared NEF8 v1 envelope writer (all typed ListFiles).
#include "asset_list_file.hpp"
#include <blake3.h>
#include <zlib.h>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace northstarcpp::shared_assets::detail {
namespace {
void put(std::uint8_t* bytes,std::uint64_t value,unsigned width) {
    for(unsigned n=0;n<width;++n)bytes[n]=std::uint8_t(value>>(n*8));
}
std::vector<std::uint8_t> deflate_raw(std::vector<std::uint8_t> const& body) {
    z_stream stream{};
    if(deflateInit2(&stream,Z_BEST_COMPRESSION,Z_DEFLATED,-MAX_WBITS,
                    8,Z_DEFAULT_STRATEGY)!=Z_OK)
        throw std::runtime_error("NEF8: cannot initialize DEFLATE");
    struct Cleanup { z_stream* z; ~Cleanup(){deflateEnd(z);} } cleanup{&stream};
    std::vector<std::uint8_t> compressed(
        static_cast<std::size_t>(compressBound(static_cast<uLong>(body.size()))));
    stream.next_in=const_cast<Bytef*>(body.data());
    stream.avail_in=static_cast<uInt>(body.size());
    stream.next_out=compressed.data();
    stream.avail_out=static_cast<uInt>(compressed.size());
    auto const code=deflate(&stream,Z_FINISH);
    if(code!=Z_STREAM_END || stream.total_in!=body.size())
        throw std::runtime_error("NEF8: raw DEFLATE failed");
    compressed.resize(static_cast<std::size_t>(stream.total_out));
    return compressed;
}
} // namespace

std::vector<std::uint8_t> serialize_list_file(
    std::uint16_t kind,std::uint16_t schema,std::uint64_t entries,
    std::string const& metadata_json,std::vector<std::uint8_t> const& body)
{
    constexpr std::size_t header=128;
    constexpr std::size_t max_body=64u*1024u*1024u;
    if(kind==0 || schema==0 || !entries || entries>65536 ||
       body.empty() || body.size()>max_body ||
       metadata_json.size()>8u*1024u*1024u)
        throw std::runtime_error("NEF8: invalid kind, schema or size");
    if(!metadata_json.empty())
        (void)parse_json(metadata_json); // metadata must be valid JSON
    auto compressed=deflate_raw(body);
    auto const offset=header+metadata_json.size();
    if(compressed.empty() || offset+compressed.size()>256ull*1024*1024)
        throw std::runtime_error("NEF8: compressed container too large");
    std::vector<std::uint8_t> output(offset+compressed.size(),0);
    auto* h=output.data();
    std::memcpy(h,"NEF8",4);
    put(h+4,1,2);put(h+6,header,2);
    put(h+8,kind,2);put(h+10,1,2); // raw DEFLATE
    put(h+12,schema,2);put(h+14,0,2);
    put(h+16,offset,8);
    put(h+24,compressed.size(),8);
    put(h+32,body.size(),8);
    put(h+40,entries,8);
    if(!metadata_json.empty()) {
        put(h+48,header,8);
        put(h+56,metadata_json.size(),8);
        std::memcpy(h+header,metadata_json.data(),metadata_json.size());
    }
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher,body.data(),body.size());
    blake3_hasher_finalize(&hasher,h+64,BLAKE3_OUT_LEN);
    put(h+112,1,8);
    std::memcpy(h+offset,compressed.data(),compressed.size());
    return output;
}
} // namespace northstarcpp::shared_assets::detail
