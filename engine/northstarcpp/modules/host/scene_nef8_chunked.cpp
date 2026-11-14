// NEF8 Scene ListFile v2: independent raw-DEFLATE blocks indexed by cell.
// Opaque component records and forward record versions are preserved.
#include "scene_nef8_records.hpp"
#include <northstarcpp/scene_chunk_index.hpp>
#include <northstarcpp/scene_world.hpp>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/assets.hpp>
#include <functional>
#include <blake3.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace northstarcpp {
namespace {
constexpr std::size_t head_size=128;
constexpr std::size_t max_size=16u*1024u*1024u;
constexpr std::size_t max_index=1048576;
constexpr std::size_t chunk_entry_size=60;
constexpr std::size_t rec_entry_size=28;
constexpr std::size_t max_count=65536;
using Record=scene_codec_detail::Record;
void need(bool ok,char const* description) {
    if(!ok)throw std::runtime_error(description);
}
std::uint64_t read(std::string const& bytes,std::size_t at,unsigned width) {
    need(width<=8 && at<=bytes.size() && width<=bytes.size()-at,
         "Invalid NEF8 chunked read range");
    std::uint64_t v=0;
    for(unsigned i=0;i<width;++i)
        v|=std::uint64_t(static_cast<unsigned char>(bytes[at+i]))<<(i*8);
    return v;
}
void append(std::string& bytes,std::uint64_t v,unsigned width) {
    for(unsigned i=0;i<width;++i)
        bytes.push_back(static_cast<char>((v>>(8*i))&255));
}
void write(std::string& bytes,std::size_t at,std::uint64_t v,unsigned width) {
    need(at<=bytes.size() && width<=bytes.size()-at,
         "NEF8 header write out of bounds");
    for(unsigned i=0;i<width;++i)
        bytes[at+i]=static_cast<char>((v>>(8*i))&255);
}
std::array<unsigned char,32> hash(std::string const& bytes) {
    std::array<unsigned char,32> out{};
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher,bytes.data(),bytes.size());
    blake3_hasher_finalize(&hasher,out.data(),out.size());
    return out;
}
std::uint32_t checksum(std::string const& bytes) {
    return static_cast<std::uint32_t>(crc32(
        0,reinterpret_cast<Bytef const*>(bytes.data()),
        static_cast<uInt>(bytes.size())));
}
std::string compress(std::string const& plain) {
    z_stream z{};
    need(deflateInit2(&z,Z_BEST_COMPRESSION,Z_DEFLATED,-MAX_WBITS,8,
                      Z_DEFAULT_STRATEGY)==Z_OK,"Chunk compressor unavailable");
    std::string result;
    try {
        result.resize(compressBound(static_cast<uLong>(plain.size())));
        z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(plain.data()));
        z.avail_in=static_cast<uInt>(plain.size());
        z.next_out=reinterpret_cast<Bytef*>(result.data());
        z.avail_out=static_cast<uInt>(result.size());
        int rc=deflate(&z,Z_FINISH);
        need(rc==Z_STREAM_END && z.total_in==plain.size(),
             "NEF8 chunk compression failed");
        result.resize(z.total_out);
    } catch(...) {deflateEnd(&z);throw;}
    deflateEnd(&z);
    return result;
}
std::string expand(std::string const& zipped,std::uint32_t size) {
    need(size>=16 && size<=max_size && !zipped.empty(),
         "Invalid NEF8 chunk expanded size");
    std::string out(size,'\0');
    z_stream z{};
    z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(zipped.data()));
    z.avail_in=static_cast<uInt>(zipped.size());
    z.next_out=reinterpret_cast<Bytef*>(out.data());
    z.avail_out=size;
    need(inflateInit2(&z,-MAX_WBITS)==Z_OK,"NEF8 chunk inflater unavailable");
    int rc=inflate(&z,Z_FINISH);
    bool valid=rc==Z_STREAM_END && z.total_in==zipped.size() && z.total_out==size;
    inflateEnd(&z);
    need(valid,"Damaged NEF8 compressed scene cell");
    return out;
}
struct Chunk {
    std::string name;
    std::vector<Record> records;
    std::string plain, zipped;
    std::array<unsigned char,32> digest{};
    std::uint64_t offset=0;
    std::uint32_t compressed_crc=0;
};
std::string pack_records(std::vector<Record> const& records) {
    need(!records.empty() && records.size()<=max_count,"Empty or oversized cell chunk");
    std::size_t sum=16+records.size()*rec_entry_size;
    for(auto const& record:records) {
        need(!record.bytes.empty() && record.bytes.size()<=max_size-sum,
             "Scene record too large");
        sum+=record.bytes.size();
    }
    std::string out;
    out.reserve(sum);
    out+="NSLR";
    append(out,2,2);append(out,0,2);
    append(out,records.size(),4);
    append(out,rec_entry_size,4);
    std::size_t next=16+records.size()*rec_entry_size;
    for(auto const& r:records) {
        append(out,static_cast<std::uint16_t>(r.kind),2);
        append(out,r.version,2);
        append(out,r.id,8);
        append(out,next,4);
        append(out,r.bytes.size(),4);
        append(out,checksum(r.bytes),4);
        append(out,r.ordinal,4);
        next+=r.bytes.size();
    }
    for(auto const& r:records)out+=r.bytes;
    need(out.size()==sum,"Scene cell directory assembly mismatch");
    return out;
}
std::vector<Record> unpack_records(std::string const& bytes) {
    need(bytes.size()>=16 && bytes.compare(0,4,"NSLR")==0 &&
         read(bytes,4,2)==2 && read(bytes,6,2)==0,
         "Unsupported chunk record schema");
    auto count=read(bytes,8,4);
    need(count>=1 && count<=max_count && read(bytes,12,4)==rec_entry_size &&
         count<=(bytes.size()-16)/rec_entry_size,
         "Invalid chunk record directory");
    std::size_t cursor=16+static_cast<std::size_t>(count)*rec_entry_size;
    std::vector<Record> records;
    records.reserve(count);
    for(std::size_t i=0;i<count;++i) {
        auto at=16+i*rec_entry_size;
        auto kind=read(bytes,at,2);
        auto version=read(bytes,at+2,2);
        auto id=read(bytes,at+4,8);
        auto offset=read(bytes,at+12,4);
        auto size=read(bytes,at+16,4);
        auto crc=read(bytes,at+20,4);
        auto ordinal=read(bytes,at+24,4);
        need(kind>=1 && version>0 && offset==cursor &&
             size>0 && size<=bytes.size()-cursor && ordinal<max_count,
             "Invalid NEF8 component version, ordinal or data range");
        auto payload=bytes.substr(cursor,size);
        need(checksum(payload)==crc,"NEF8 chunk record CRC mismatch");
        records.push_back({static_cast<scene_codec_detail::RecordKind>(kind),
                           id,std::move(payload),
                           static_cast<std::uint16_t>(version),
                           static_cast<std::uint32_t>(ordinal)});
        cursor+=size;
    }
    need(cursor==bytes.size(),"Trailing component data in Scene chunk");
    return records;
}
struct Index {
    std::uint32_t record_count=0;
    std::uint64_t expanded=0, compressed=0, payload_offset=0;
    std::array<unsigned char,32> all_hash{};
    struct Entry {
        std::string cell;
        std::uint32_t records=0;
        std::uint64_t offset=0;
        std::uint32_t compressed=0, plain=0;
        std::array<unsigned char,32> digest{};
        std::uint32_t crc=0;
    };
    std::vector<Entry> chunks;
};
Index parse_index(std::string const& header,std::string const& table,
                  std::uint64_t file_size) {
    need(header.size()==head_size && header.compare(0,4,"NEF8")==0 &&
         read(header,4,2)==1 && read(header,6,2)==128 &&
         read(header,8,2)==6 && read(header,10,2)==2 &&
         read(header,12,2)==2 && read(header,14,2)==0 &&
         read(header,112,8)==2,"Expected NEF8 scene chunk format v2");
    Index index;
    index.payload_offset=read(header,16,8);
    index.compressed=read(header,24,8);
    index.expanded=read(header,32,8);
    auto advertised_records=read(header,40,8);
    need(advertised_records>0 && advertised_records<=max_count,
         "Scene chunk record count exceeds bounds");
    index.record_count=static_cast<std::uint32_t>(advertised_records);
    auto metadata_start=read(header,48,8);
    auto metadata_size=read(header,56,8);
    need(index.record_count>0 && index.record_count<=max_count &&
         metadata_start==128 && metadata_size>=16 &&
         metadata_size<=max_index && metadata_size==table.size() &&
         index.payload_offset==128+metadata_size &&
         index.compressed<=max_size-index.payload_offset &&
         file_size==index.payload_offset+index.compressed &&
         index.expanded>=16 && index.expanded<=max_size,
         "Invalid NEF8 scene chunk offsets or counts");
    auto table_hash=hash(table);
    need(std::memcmp(header.data()+96,table_hash.data(),16)==0,
         "Scene chunk index checksum mismatch");
    std::memcpy(index.all_hash.data(),header.data()+64,32);
    need(table.compare(0,4,"NSCK")==0 &&
         read(table,4,2)==2 && read(table,6,2)==0 &&
         read(table,12,4)==chunk_entry_size,
         "Unsupported Scene chunk index");
    auto count=read(table,8,4);
    need(count>0 && count<=8193,"Invalid number of scene chunks");
    std::size_t pos=16;
    std::uint64_t next=index.payload_offset,total_plain=0,total_records=0;
    std::unordered_set<std::string> cells;
    index.chunks.reserve(static_cast<std::size_t>(count));
    for(std::size_t i=0;i<count;++i) {
        need(pos<=table.size() && chunk_entry_size<=table.size()-pos,
             "Truncated Scene chunk index entry");
        auto kind=read(table,pos,1);
        auto reserved=read(table,pos+1,1);
        auto namelen=read(table,pos+2,2);
        Index::Entry entry;
        entry.records=static_cast<std::uint32_t>(read(table,pos+4,4));
        entry.offset=read(table,pos+8,8);
        entry.compressed=static_cast<std::uint32_t>(read(table,pos+16,4));
        entry.plain=static_cast<std::uint32_t>(read(table,pos+20,4));
        std::memcpy(entry.digest.data(),table.data()+pos+24,32);
        entry.crc=static_cast<std::uint32_t>(read(table,pos+56,4));
        pos+=chunk_entry_size;
        need(namelen<=96 && pos<=table.size() &&
             namelen<=table.size()-pos,
             "Invalid Scene cell name length");
        entry.cell=table.substr(pos,namelen);pos+=namelen;
        need((i==0 && kind==0 && namelen==0) ||
             (i>0 && kind==1 && namelen>0),
             "Scene must have a global chunk followed by named cells");
        need(reserved==0 && entry.records>0 && entry.records<=max_count &&
             entry.offset==next && entry.compressed>0 && entry.plain>=16 &&
             entry.plain<=max_size-total_plain &&
             entry.compressed<=index.compressed &&
             entry.compressed<=file_size-next,
             "Invalid Scene cell chunk range");
        if(i>0) {
            need(entry.cell.find('\0')==std::string::npos &&
                 cells.insert(entry.cell).second,
                 "Duplicate or invalid Scene streaming cell");
        }
        next+=entry.compressed;
        total_plain+=entry.plain;total_records+=entry.records;
        index.chunks.push_back(std::move(entry));
    }
    need(pos==table.size() && next==file_size &&
         total_plain==index.expanded && total_records==index.record_count,
         "Scene chunk index totals mismatch");
    return index;
}
Index index_bytes(std::string const& file) {
    need(file.size()>128 && file.size()<=max_size,
         "Invalid scene chunked file size");
    std::string header=file.substr(0,128);
    auto start=read(header,48,8),size=read(header,56,8);
    need(start==128 && size>=16 && size<=max_index &&
         size<=file.size()-128,
         "Scene chunk table exceeds source file");
    return parse_index(header,file.substr(128,size),file.size());
}
std::string compressed_from_file(std::filesystem::path const& path,
    std::uint64_t offset,std::uint32_t length) {
    std::ifstream stream(path,std::ios::binary);
    need(bool(stream),"Cannot open indexed Scene ListFile");
    stream.seekg(static_cast<std::streamoff>(offset));
    need(bool(stream),"Cannot seek to indexed scene cell");
    std::string bytes(length,'\0');
    stream.read(bytes.data(),length);
    need(stream.gcount()==static_cast<std::streamsize>(length),
         "Truncated compressed Scene cell");
    return bytes;
}
std::string expanded_chunk(Index::Entry const& entry,
                           std::string const& compressed) {
    need(checksum(compressed)==entry.crc,
         "Compressed Scene cell CRC mismatch");
    auto inflated=expand(compressed,entry.plain);
    auto digest=hash(inflated);
    need(digest==entry.digest,"Scene cell BLAKE3 mismatch");
    return inflated;
}
std::vector<Record> decode_all(Index const& index,
    std::vector<std::string> const& compressed) {
    need(compressed.size()==index.chunks.size(),
         "Missing Scene cell compressed blocks");
    std::string concatenated;
    concatenated.reserve(static_cast<std::size_t>(index.expanded));
    std::vector<Record> records;
    records.reserve(index.record_count);
    for(std::size_t i=0;i<compressed.size();++i) {
        auto data=expanded_chunk(index.chunks[i],compressed[i]);
        auto part=unpack_records(data);
        need(part.size()==index.chunks[i].records,
             "Scene component count mismatch in chunk");
        concatenated+=data;
        for(auto& entry:part)records.push_back(std::move(entry));
    }
    need(hash(concatenated)==index.all_hash,
         "NEF8 scene global BLAKE3 mismatch");
    need(records.size()==index.record_count,
         "NEF8 Scene ListFile record count mismatch");
    std::sort(records.begin(),records.end(),[](Record const& a,Record const& b){
        return a.ordinal<b.ordinal;
    });
    for(std::size_t i=0;i<records.size();++i)
        need(records[i].ordinal==i,
             "Scene chunk component ordering has gaps or duplicates");
    return records;
}
} // namespace

namespace scene_codec_detail {
std::string encode_nef8_chunked_scene(Scene const& scene) {
    auto records=encode_records(scene);
    need(!records.empty() && records.size()<=max_count,
         "Too many Scene component records");
    std::unordered_map<std::uint64_t,std::string> entity_cells;
    for(auto const& entity:scene.objects)
        entity_cells.emplace(entity.node.id,entity.node.cell);
    std::vector<Chunk> chunks(1);
    std::unordered_map<std::string,std::size_t> lookup;
    for(auto const& cell:scene.cells) {
        lookup.emplace(cell.name,chunks.size());
        chunks.push_back({cell.name});
    }
    for(std::size_t i=0;i<records.size();++i) {
        records[i].ordinal=static_cast<std::uint32_t>(i);
        std::size_t index=0;
        // Entity-owned plugin/opaque components follow their entity cell.
        auto const kind=static_cast<std::uint16_t>(records[i].kind);
        if((kind==2 || kind==7 || kind>7) && records[i].id) {
            auto it=entity_cells.find(records[i].id);
            need(it!=entity_cells.end(),
                 "Scene component refers to an entity outside the document");
            if(!it->second.empty()) {
                auto target=lookup.find(it->second);
                need(target!=lookup.end(),"Scene references unknown chunk");
                index=target->second;
            }
        }
        chunks[index].records.push_back(std::move(records[i]));
    }
    chunks.erase(std::remove_if(chunks.begin()+1,chunks.end(),
        [](Chunk const& c){return c.records.empty();}),chunks.end());
    need(chunks.size()<=8193,"Too many independent scene chunks");
    std::uint64_t total_expanded=0,total_zipped=0;
    std::string full_plain;
    for(auto& chunk:chunks) {
        chunk.plain=pack_records(chunk.records);
        total_expanded+=chunk.plain.size();
        need(total_expanded<=max_size,"Scene expanded blocks exceed 16 MiB");
        chunk.digest=hash(chunk.plain);
        chunk.zipped=compress(chunk.plain);
        chunk.compressed_crc=checksum(chunk.zipped);
        total_zipped+=chunk.zipped.size();
        full_plain+=chunk.plain;
    }
    std::string table="NSCK";
    append(table,2,2);append(table,0,2);
    append(table,chunks.size(),4);
    append(table,chunk_entry_size,4);
    // The NSCK index has a 16-byte preamble before its directory entries.
    // Include it when calculating the first compressed chunk offset.
    std::uint64_t next=128+16;
    for(auto const& chunk:chunks)next+=chunk_entry_size+chunk.name.size();
    need(next<=max_index+128,"Scene chunk table too large");
    std::uint64_t payload_offset=next;
    for(auto& chunk:chunks) {
        chunk.offset=next;
        append(table,chunk.name.empty()?0:1,1);
        append(table,0,1);
        append(table,chunk.name.size(),2);
        append(table,chunk.records.size(),4);
        append(table,chunk.offset,8);
        append(table,chunk.zipped.size(),4);
        append(table,chunk.plain.size(),4);
        table.append(reinterpret_cast<char const*>(chunk.digest.data()),32);
        append(table,chunk.compressed_crc,4);
        table+=chunk.name;
        next+=chunk.zipped.size();
    }
    need(table.size()==payload_offset-128 && next<=max_size,
         "Compressed Scene ListFile exceeds 16 MiB");
    std::string file(128,'\0');
    file.replace(0,4,"NEF8");
    write(file,4,1,2);write(file,6,128,2);
    write(file,8,6,2);write(file,10,2,2);
    write(file,12,2,2);write(file,14,0,2);
    write(file,16,payload_offset,8);write(file,24,total_zipped,8);
    write(file,32,total_expanded,8);write(file,40,records.size(),8);
    write(file,48,128,8);write(file,56,table.size(),8);
    auto all_hash=hash(full_plain), table_hash=hash(table);
    std::memcpy(file.data()+64,all_hash.data(),32);
    std::memcpy(file.data()+96,table_hash.data(),16);
    write(file,112,2,8);
    file+=table;
    for(auto const& chunk:chunks)file+=chunk.zipped;
    need(file.size()==next,"Serialized Scene chunk layout mismatch");
    return file;
}
Scene decode_nef8_chunked_scene(std::string const& bytes) {
    auto index=index_bytes(bytes);
    std::vector<std::string> payloads;
    for(auto const& entry:index.chunks)
        payloads.push_back(bytes.substr(
            static_cast<std::size_t>(entry.offset),entry.compressed));
    auto records=decode_all(index,payloads);
    // Cell grouping in index must match the entity's semantic cell metadata.
    Scene result=decode_records(records);
    std::unordered_map<std::uint64_t,std::string> attached;
    for(auto const& object:result.objects)
        attached.emplace(object.node.id,object.node.cell);
    for(std::size_t i=0;i<index.chunks.size();++i) {
        auto part=unpack_records(expanded_chunk(index.chunks[i],payloads[i]));
        for(auto const& rec:part) {
            auto kind=static_cast<std::uint16_t>(rec.kind);
            if((kind==2 || kind>=7) && rec.id) {
                auto found=attached.find(rec.id);
                need(found!=attached.end() &&
                     found->second==index.chunks[i].cell,
                     "Scene record located in incorrect streaming cell");
            } else {
                need(i==0,"Scene-global record found in non-global cell");
            }
        }
    }
    return result;
}
} // namespace scene_codec_detail

std::vector<SceneChunkInfo> index_scene_chunks(std::filesystem::path const& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    need(bool(f),"Cannot inspect scene chunk index");
    auto end=f.tellg();
    need(end>128 && static_cast<std::uint64_t>(end)<=max_size,
         "Scene chunked file size is invalid");
    f.seekg(0);
    std::string header(128,'\0');
    f.read(header.data(),128);
    need(f.gcount()==128,"Truncated indexed scene header");
    auto start=read(header,48,8),size=read(header,56,8);
    need(start==128 && size>=16 && size<=max_index &&
         size<=static_cast<std::uint64_t>(end)-128,
         "Scene chunk table size invalid");
    std::string table(size,'\0');
    f.read(table.data(),static_cast<std::streamsize>(size));
    need(f.gcount()==static_cast<std::streamsize>(size),
         "Truncated Scene chunk index");
    auto parsed=parse_index(header,table,static_cast<std::uint64_t>(end));
    std::vector<SceneChunkInfo> result;
    result.reserve(parsed.chunks.size());
    for(auto const& chunk:parsed.chunks)
        result.push_back({chunk.cell,chunk.offset,chunk.compressed,
                          chunk.plain,chunk.records});
    return result;
}
std::vector<SceneChunkRecord> load_scene_chunk(
    std::filesystem::path const& path,std::string const& cell) {
    // Read the small index once. A streaming service may cache the index;
    // this standalone API favors correct validated reads over caching.
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    need(bool(f),"Cannot open chunked scene");
    auto end=f.tellg();
    need(end>128 && static_cast<std::uint64_t>(end)<=max_size,
         "Invalid chunked Scene file length");
    f.seekg(0);
    std::string header(128,'\0');
    f.read(header.data(),128);
    need(f.gcount()==128,"Truncated NEF8 chunk header");
    auto size=read(header,56,8);
    need(read(header,48,8)==128 && size>=16 && size<=max_index &&
         size<=static_cast<std::uint64_t>(end)-128,
         "Invalid NEF8 chunk table length");
    std::string table(size,'\0');
    f.read(table.data(),static_cast<std::streamsize>(size));
    need(f.gcount()==static_cast<std::streamsize>(size),
         "Cannot read Scene chunk index");
    auto index=parse_index(header,table,static_cast<std::uint64_t>(end));
    for(auto const& entry:index.chunks) {
        if(entry.cell!=cell)continue;
        auto compressed=compressed_from_file(path,entry.offset,entry.compressed);
        auto records=unpack_records(expanded_chunk(entry,compressed));
        need(records.size()==entry.records,"Indexed cell count mismatch");
        std::vector<SceneChunkRecord> output;
        output.reserve(records.size());
        for(auto& record:records)
            output.push_back({static_cast<std::uint16_t>(record.kind),
                record.version,record.id,record.ordinal,std::move(record.bytes)});
        return output;
    }
    return {};
}
SceneCellDiskResidency::SceneCellDiskResidency(std::filesystem::path file)
    : reader_(std::move(file)) {
    // Decode the header/index once; cell bodies remain on disk.
    resident_.emplace("",reader_.load(""));
}
void SceneCellDiskResidency::apply(SceneWorld const& world) {
    // Stage all necessary reads before committing residency state, so
    // a corrupt chunk cannot partially mutate the cache.
    std::unordered_map<std::string,std::vector<SceneChunkRecord>> pending;
    for(auto const& transition:world.cell_transitions()) {
        if(!transition.loaded)continue;
        if(!resident_.count(transition.name))
            pending.emplace(transition.name,reader_.load(transition.name));
    }
    for(auto const& transition:world.cell_transitions()) {
        if(!transition.loaded)resident_.erase(transition.name);
    }
    for(auto& item:pending)
        resident_.insert_or_assign(item.first,std::move(item.second));
}
bool SceneCellDiskResidency::loaded(std::string const& cell) const {
    return resident_.find(cell)!=resident_.end();
}
std::vector<SceneChunkRecord> const*
SceneCellDiskResidency::records(std::string const& cell) const {
    auto at=resident_.find(cell);
    return at==resident_.end()?nullptr:&at->second;
}

// Indexed scene reader with cached verified chunk table. This path never
// materializes the whole scene, including for archive-backed VFS resources.
struct SceneChunkReader::Impl {
    Index index;
    std::function<std::string(std::uint64_t,std::uint32_t)> fetch;
    bool native_ranges=true;
};

SceneChunkReader::SceneChunkReader(std::filesystem::path file)
    : impl_(std::make_shared<Impl>())
{
    auto const size=std::filesystem::file_size(file);
    need(size>128 && size<=max_size,"Scene source file size outside NEF8 limits");
    impl_->fetch=[file=std::move(file)](std::uint64_t offset,std::uint32_t length) {
        return compressed_from_file(file,offset,length);
    };
    auto header=impl_->fetch(0,128);
    auto const start=read(header,48,8),bytes=read(header,56,8);
    need(start==128 && bytes>=16 && bytes<=max_index &&
         bytes<=size-128,"Invalid Scene ListFile index size");
    impl_->index=parse_index(header,impl_->fetch(128,static_cast<std::uint32_t>(bytes)),size);
}

SceneChunkReader::SceneChunkReader(AssetStore const& assets,std::string virtual_path)
    : impl_(std::make_shared<Impl>())
{
    auto const size=assets.size(virtual_path);
    need(size>128 && size<=max_size,"VFS Scene source size outside NEF8 limits");
    impl_->native_ranges=assets.native_range_reads();
    impl_->fetch=[&assets,path=std::move(virtual_path)](
        std::uint64_t offset,std::uint32_t length) {
        auto data=assets.read_range(path,offset,length);
        return std::string(data.begin(),data.end());
    };
    auto header=impl_->fetch(0,128);
    auto const start=read(header,48,8),bytes=read(header,56,8);
    need(start==128 && bytes>=16 && bytes<=max_index &&
         bytes<=size-128,"Invalid VFS Scene ListFile index size");
    impl_->index=parse_index(header,impl_->fetch(128,static_cast<std::uint32_t>(bytes)),size);
}

bool SceneChunkReader::native_range_reads() const noexcept {
    return impl_->native_ranges;
}

std::vector<SceneChunkInfo> SceneChunkReader::chunks() const {
    std::vector<SceneChunkInfo> output;
    output.reserve(impl_->index.chunks.size());
    for(auto const& chunk:impl_->index.chunks)
        output.push_back({chunk.cell,chunk.offset,chunk.compressed,
                          chunk.plain,chunk.records});
    return output;
}

std::vector<SceneChunkRecord> SceneChunkReader::load(std::string const& cell) const {
    auto const& items=impl_->index.chunks;
    auto found=std::find_if(items.begin(),items.end(),
        [&](auto const& value){return value.cell==cell;});
    if(found==items.end())return {};
    auto packed=impl_->fetch(found->offset,found->compressed);
    auto records=unpack_records(expanded_chunk(*found,packed));
    need(records.size()==found->records,"Scene indexed cell count mismatch");
    std::vector<SceneChunkRecord> output;
    output.reserve(records.size());
    for(auto& record:records)
        output.push_back({static_cast<std::uint16_t>(record.kind),
            record.version,record.id,record.ordinal,std::move(record.bytes)});
    return output;
}

} // namespace northstarcpp
