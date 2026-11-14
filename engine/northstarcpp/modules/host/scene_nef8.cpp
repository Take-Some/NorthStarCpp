// NSCN v2: specialized NEF8 ListFile with typed Scene V3 component records.
// Compatible with the existing NEF8 v1/raw-DEFLATE/BLAKE3 envelope.
// Copyright (C) 2026 Kayla's Systems — Kayla Verner.
#include "scene_nef8_records.hpp"
#include <northstarcpp/scene.hpp>
#include <blake3.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace northstarcpp {
namespace {
constexpr std::size_t header_size=128;
constexpr std::size_t max_bytes=16u*1024u*1024u;
constexpr std::uint16_t scene_kind=6;
constexpr std::size_t directory_header=16;
constexpr std::size_t directory_record=24;
constexpr std::uint32_t max_records=65536;
void require(bool condition,char const* text) {
    if(!condition)throw std::runtime_error(text);
}
void put(std::string& data,std::size_t offset,std::uint64_t value,unsigned width) {
    require(offset<=data.size() && width<=data.size()-offset,
            "NEF8 scene header write outside bounds");
    for(unsigned i=0;i<width;++i)
        data[offset+i]=static_cast<char>((value>>(i*8))&255u);
}
void append(std::string& output,std::uint64_t value,unsigned width) {
    for(unsigned i=0;i<width;++i)
        output.push_back(static_cast<char>((value>>(i*8))&255u));
}
std::uint64_t get(std::string const& data,std::size_t offset,unsigned width) {
    require(offset<=data.size() && width<=data.size()-offset,
            "Truncated NEF8 scene field");
    std::uint64_t value=0;
    for(unsigned i=0;i<width;++i)
        value|=std::uint64_t(static_cast<std::uint8_t>(data[offset+i]))<<(8*i);
    return value;
}
std::uint32_t record_crc(std::string const& value) {
    return static_cast<std::uint32_t>(::crc32(
        0,reinterpret_cast<Bytef const*>(value.data()),
        static_cast<uInt>(value.size())));
}
std::array<std::uint8_t,BLAKE3_OUT_LEN> digest(std::string const& body) {
    std::array<std::uint8_t,BLAKE3_OUT_LEN> hash{};
    blake3_hasher state;
    blake3_hasher_init(&state);
    blake3_hasher_update(&state,body.data(),body.size());
    blake3_hasher_finalize(&state,hash.data(),hash.size());
    return hash;
}
std::string deflate_raw(std::string const& body) {
    z_stream z{};
    require(deflateInit2(&z,Z_BEST_COMPRESSION,Z_DEFLATED,-MAX_WBITS,8,
                         Z_DEFAULT_STRATEGY)==Z_OK,
            "Cannot initialize NEF8 scene compressor");
    std::string output;
    try {
        output.resize(static_cast<std::size_t>(compressBound(
            static_cast<uLong>(body.size()))));
        z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(body.data()));
        z.avail_in=static_cast<uInt>(body.size());
        z.next_out=reinterpret_cast<Bytef*>(output.data());
        z.avail_out=static_cast<uInt>(output.size());
        auto status=deflate(&z,Z_FINISH);
        require(status==Z_STREAM_END && z.total_in==body.size(),
                "NEF8 scene compressor failed");
        output.resize(static_cast<std::size_t>(z.total_out));
    } catch(...) {
        deflateEnd(&z);throw;
    }
    deflateEnd(&z);
    return output;
}
std::string inflate_raw(std::string const& source,std::size_t offset,
                        std::size_t compressed,std::size_t expanded) {
    require(expanded>0 && expanded<=max_bytes && compressed>0 &&
            offset<=source.size() && compressed==source.size()-offset,
            "Invalid NEF8 scene size or payload range");
    std::string output(expanded,'\0');
    z_stream z{};
    z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(source.data()+offset));
    z.avail_in=static_cast<uInt>(compressed);
    z.next_out=reinterpret_cast<Bytef*>(output.data());
    z.avail_out=static_cast<uInt>(expanded);
    require(inflateInit2(&z,-MAX_WBITS)==Z_OK,
            "Cannot initialize NEF8 scene decompressor");
    int status=inflate(&z,Z_FINISH);
    bool good=status==Z_STREAM_END && z.total_in==compressed &&
              z.total_out==expanded;
    inflateEnd(&z);
    require(good,"Truncated or invalid NEF8 scene DEFLATE stream");
    return output;
}
std::string assemble_body(std::vector<scene_codec_detail::Record> const& records) {
    require(!records.empty() && records.size()<=max_records,
            "NEF8 scene has invalid record count");
    auto const table_end=directory_header+records.size()*directory_record;
    std::size_t bytes=table_end;
    for(auto const& record:records) {
        require(!record.bytes.empty() && record.bytes.size()<=max_bytes-bytes,
                "NEF8 scene exceeds expanded size limit");
        bytes+=record.bytes.size();
    }
    std::string body;
    body.reserve(bytes);
    body+="NSLR";
    append(body,1,2);append(body,0,2); // record schema 1.0
    append(body,records.size(),4);
    append(body,directory_record,4);
    std::size_t offset=table_end;
    for(auto const& record:records) {
        append(body,static_cast<std::uint16_t>(record.kind),2);
        append(body,1,2);
        append(body,record.id,8);
        append(body,offset,4);
        append(body,record.bytes.size(),4);
        append(body,record_crc(record.bytes),4);
        offset+=record.bytes.size();
    }
    for(auto const& record:records)body+=record.bytes;
    require(body.size()==bytes,"NEF8 scene directory layout mismatch");
    return body;
}
std::vector<scene_codec_detail::Record> decode_body(
    std::string const& body,std::uint64_t advertised) {
    require(body.size()>=directory_header && body.compare(0,4,"NSLR")==0 &&
            get(body,4,2)==1 && get(body,6,2)==0,
            "Unsupported NEF8 Scene ListFile body version");
    auto const count=get(body,8,4);
    require(count==advertised && count>0 && count<=max_records &&
            get(body,12,4)==directory_record &&
            count<=(body.size()-directory_header)/directory_record,
            "Invalid NEF8 scene list directory");
    std::size_t const end=directory_header+
        static_cast<std::size_t>(count)*directory_record;
    std::size_t next=end;
    std::vector<scene_codec_detail::Record> records;
    records.reserve(static_cast<std::size_t>(count));
    for(std::size_t i=0;i<count;++i) {
        auto base=directory_header+i*directory_record;
        auto kind=get(body,base,2);
        auto version=get(body,base+2,2);
        auto id=get(body,base+4,8);
        auto offset=get(body,base+12,4);
        auto size=get(body,base+16,4);
        auto checksum=get(body,base+20,4);
        require(version==1 && kind>=1 && kind<=6,
                "Unsupported Scene ListFile component type/version");
        require(offset==next && size>0 && size<=body.size()-next,
                "Invalid NEF8 scene component offset or length");
        auto payload=body.substr(next,static_cast<std::size_t>(size));
        require(record_crc(payload)==checksum,
                "NEF8 Scene ListFile component checksum mismatch");
        records.push_back({static_cast<scene_codec_detail::RecordKind>(kind),
                           id,std::move(payload)});
        next+=size;
    }
    require(next==body.size(),"Trailing bytes in NEF8 Scene ListFile");
    return records;
}
}

namespace scene_codec_detail {
std::string encode_nef8_scene(Scene const& scene) {
    auto records=encode_records(scene);
    auto body=assemble_body(records);
    auto compressed=deflate_raw(body);
    require(header_size+compressed.size()<=max_bytes,
            "Compressed NEF8 Scene ListFile exceeds file size limit");
    std::string result(header_size,'\0');
    std::memcpy(result.data(),"NEF8",4);
    put(result,4,1,2);                   // envelope major version
    put(result,6,header_size,2);
    put(result,8,scene_kind,2);
    put(result,10,1,2);                  // DEFLATE compression
    put(result,12,1,2);                  // schema
    put(result,14,0,2);
    put(result,16,header_size,8);
    put(result,24,compressed.size(),8);
    put(result,32,body.size(),8);
    put(result,40,records.size(),8);
    // No arbitrary string metadata: typed document component replaces it.
    auto hash=digest(body);
    std::memcpy(result.data()+64,hash.data(),hash.size());
    put(result,112,1,8);
    result+=compressed;
    return result;
}
Scene decode_nef8_scene(std::string const& bytes) {
    require(bytes.size()>header_size && bytes.size()<=max_bytes &&
            bytes.compare(0,4,"NEF8")==0,
            "Expected NEF8 Scene ListFile");
    require(get(bytes,4,2)==1 && get(bytes,6,2)==header_size &&
            get(bytes,8,2)==scene_kind &&
            get(bytes,10,2)==1 && get(bytes,12,2)==1 &&
            get(bytes,14,2)==0 && get(bytes,112,8)==1,
            "Invalid NEF8 Scene ListFile kind, version or compression");
    auto offset=get(bytes,16,8),compressed=get(bytes,24,8);
    auto expanded=get(bytes,32,8),count=get(bytes,40,8);
    require(offset==header_size && count>=1 && count<=max_records &&
            get(bytes,48,8)==0 && get(bytes,56,8)==0,
            "Invalid NEF8 Scene ListFile directory/metadata");
    auto body=inflate_raw(bytes,static_cast<std::size_t>(offset),
                          static_cast<std::size_t>(compressed),
                          static_cast<std::size_t>(expanded));
    auto hash=digest(body);
    require(std::memcmp(bytes.data()+64,hash.data(),hash.size())==0,
            "NEF8 Scene ListFile BLAKE3 mismatch");
    return decode_records(decode_body(body,count));
}
} // namespace scene_codec_detail

// Existing public API now emits the shared ListFile container by default.
// Older NSCN binary scenes remain decodable for existing projects and archives.
std::string serialize_scene_binary(Scene const& scene) {
    return scene_codec_detail::encode_nef8_chunked_scene(scene);
}
Scene parse_scene_binary(std::string const& bytes) {
    if(bytes.size()>=4 && bytes.compare(0,4,"NSCN")==0)
        return scene_codec_detail::decode_legacy_nscn(bytes);
    if(bytes.size()>=14 &&
       static_cast<unsigned char>(bytes[12])==2 &&
       static_cast<unsigned char>(bytes[13])==0)
        return scene_codec_detail::decode_nef8_chunked_scene(bytes);
    return scene_codec_detail::decode_nef8_scene(bytes);
}
} // namespace northstarcpp
