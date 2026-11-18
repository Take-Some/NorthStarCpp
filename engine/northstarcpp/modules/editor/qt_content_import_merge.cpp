#include "qt_content_import_merge.hpp"
#include "../host/asset_list_file.hpp"

#include <northstarcpp/nac.hpp>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace northstarcpp::editor::content_import {
namespace {
using shared_assets::detail::Bytes;
using shared_assets::detail::ListFile;
using shared_assets::detail::require;
using Wire=std::vector<std::uint8_t>;
using Offset=std::uint64_t;

void put(Wire& data,Offset offset,Offset value,unsigned width) {
    require(offset<=data.size() && width<=data.size()-offset,
            "Import dictionary write outside buffer");
    for(unsigned i=0;i<width;++i)data[std::size_t(offset+i)]=std::uint8_t(value>>(8*i));
}

void copy(Bytes const& input,Offset from,Offset length,Wire& data,Offset to) {
    require(to<=data.size() && length<=data.size()-to,
            "Import dictionary copy outside buffer");
    auto const* src=input.at(from,length);
    std::copy_n(src,static_cast<std::size_t>(length),
                data.begin()+static_cast<std::ptrdiff_t>(to));
}

std::string metadata(Wire const& wire) {
    Bytes const bytes(wire);
    if(bytes.integer(4,1)==2) {
        auto const prefix=bytes.integer(5,1)>=6?64u:32u;
        auto const offset=bytes.size()-bytes.integer(16,8);
        return bytes.string(prefix,offset-prefix);
    }
    return bytes.string(bytes.integer(48,8),bytes.integer(56,8));
}

template<class Entry>
void requireUnique(std::vector<Entry> const& before,std::vector<Entry> const& added) {
    std::set<std::string> names;
    for(auto const& entry:before)names.insert(entry.name);
    for(auto const& entry:added)
        if(!names.insert(entry.name).second)
            throw std::invalid_argument("Entry already exists: "+entry.name);
}

struct TextureLayout {
    Offset entries,mips,names,nameBytes,payload,payloadBytes,mipCount;
};

TextureLayout textureLayout(Bytes const& bytes) {
    // The texture reader validates both indexes before relocating their records.
    auto const table=bytes.integer(16,8);
    return {table+bytes.integer(table+20,4),table+bytes.integer(table+24,4),
        table+bytes.integer(table+28,4),bytes.integer(table+32,4),
        bytes.integer(32,8),bytes.integer(40,8),bytes.integer(table+16,4)};
}

Wire mergeTextures(ListFile const& oldFile,ListFile const& newFile) {
    Bytes const old(oldFile.body),added(newFile.body);
    auto const a=textureLayout(old),b=textureLayout(added);
    Offset const count=oldFile.entries+newFile.entries;
    Offset const mipCount=a.mipCount+b.mipCount;
    Offset const nameBytes=a.nameBytes+b.nameBytes;
    constexpr Offset table=64,entryTable=40;
    Offset const mipTable=entryTable+count*64;
    Offset const nameTable=mipTable+mipCount*32;
    Offset const tableSize=nameTable+nameBytes;
    Offset const payloadStart=table+tableSize;
    Offset const payloadBytes=a.payloadBytes+b.payloadBytes;
    require(payloadStart+payloadBytes<=256ull*1024*1024,"Combined YTD exceeds 256 MiB.");
    Wire body(std::size_t(payloadStart+payloadBytes),0);

    copy(old,0,table,body,0);
    copy(old,old.integer(16,8),entryTable,body,table);
    put(body,12,count,4);put(body,16,table,8);put(body,24,tableSize,8);
    put(body,32,payloadStart,8);put(body,40,payloadBytes,8);
    put(body,table+12,count,4);put(body,table+16,mipCount,4);
    put(body,table+20,entryTable,4);put(body,table+24,mipTable,4);
    put(body,table+28,nameTable,4);put(body,table+32,nameBytes,4);

    copy(old,a.entries,oldFile.entries*64,body,table+entryTable);
    copy(old,a.mips,a.mipCount*32,body,table+mipTable);
    copy(old,a.names,a.nameBytes,body,table+nameTable);
    copy(old,a.payload,a.payloadBytes,body,payloadStart);
    auto const newEntries=table+entryTable+oldFile.entries*64;
    auto const newMips=table+mipTable+a.mipCount*32;
    copy(added,b.entries,newFile.entries*64,body,newEntries);
    copy(added,b.mips,b.mipCount*32,body,newMips);
    copy(added,b.names,b.nameBytes,body,table+nameTable+a.nameBytes);
    copy(added,b.payload,b.payloadBytes,body,payloadStart+a.payloadBytes);
    for(Offset i=0;i<newFile.entries;++i) {
        auto const from=b.entries+i*64,to=newEntries+i*64;
        put(body,to+8,added.integer(from+8,8)+a.payloadBytes,8);
        put(body,to+24,added.integer(from+24,4)+a.nameBytes,4);
        put(body,to+40,added.integer(from+40,4)+a.mipCount,4);
    }
    for(Offset i=0;i<b.mipCount;++i)
        put(body,newMips+i*32,added.integer(b.mips+i*32,8)+a.payloadBytes,8);
    return body;
}

Wire mergeDrawables(ListFile const& oldFile,ListFile const& newFile) {
    Bytes const old(oldFile.body),added(newFile.body);
    auto const version=old.integer(0,4);
    require(oldFile.kind==2 && newFile.kind==2 && (version==4 || version==5) &&
        added.integer(0,4)==version,
        "This YDD layout cannot be extended by the installed model packer. Import a new YDD beside it.");
    constexpr Offset header=40,recordSize=80;
    auto const count=oldFile.entries+newFile.entries;
    auto const oldStrings=old.integer(24,8),newStrings=added.integer(24,8);
    auto const oldFloor=old.integer(32,8),newFloor=added.integer(32,8);
    auto const combinedStrings=header+count*recordSize;
    auto const combinedFloor=combinedStrings+oldStrings+newStrings;
    auto const oldPayload=old.size()-oldFloor;
    auto const newPayload=added.size()-newFloor;
    require(combinedFloor+oldPayload+newPayload<=64ull*1024*1024,
            "Combined YDD exceeds 64 MiB.");
    Wire body(std::size_t(combinedFloor+oldPayload+newPayload),0);
    put(body,0,version,4);put(body,4,count,4);put(body,8,header,8);
    put(body,16,combinedStrings,8);put(body,24,oldStrings+newStrings,8);
    put(body,32,combinedFloor,8);
    copy(old,old.integer(16,8),oldStrings,body,combinedStrings);
    copy(added,added.integer(16,8),newStrings,body,combinedStrings+oldStrings);
    copy(old,oldFloor,oldPayload,body,combinedFloor);
    copy(added,newFloor,newPayload,body,combinedFloor+oldPayload);

    auto copyEntries=[&](ListFile const& file,Bytes const& source,
                          Offset first,Offset payload,Offset stringShift) {
        auto const sourceFloor=source.integer(32,8);
        for(Offset i=0;i<file.entries;++i) {
            auto const from=header+i*recordSize,to=header+(first+i)*recordSize;
            copy(source,from,recordSize,body,to);
            auto const oldOffset=source.integer(from+64,8);
            auto const newOffset=payload+oldOffset-sourceFloor;
            put(body,to+64,newOffset,8);
            if(!stringShift)continue;
            auto adjustString=[&](Offset from,Offset to) {
                auto const value=source.integer(from,4);
                if(value!=0xffffffffu)put(body,to,value+stringShift,4);
            };
            for(auto field:{8u,12u,32u})adjustString(from+field,to+field);

            auto const flags=source.integer(oldOffset+4,4);
            auto cursor=oldOffset+(flags?72u:8u);
            auto const meshes=source.integer(oldOffset,4);
            for(Offset mesh=0;mesh<meshes;++mesh) {
                auto const dest=newOffset+cursor-oldOffset;
                adjustString(cursor,dest);
                adjustString(cursor+4,dest+4);
                if(version==5)adjustString(cursor+48,dest+48);
                auto const vertices=source.integer(cursor+8,4);
                auto const indices=source.integer(cursor+12,4);
                auto const stride=source.integer(cursor+44,4);
                cursor+=(version==5?52u:48u)+vertices*(32+stride)+indices*4;
            }
        }
    };
    copyEntries(oldFile,old,0,combinedFloor,0);
    copyEntries(newFile,added,oldFile.entries,combinedFloor+oldPayload,oldStrings);
    return body;
}

Wire mergeAudio(Wire const& existing,Wire const& incoming) {
    std::vector<nac::Track> tracks,added;
    std::string error;
    if(!nac::decode(existing,tracks,error) || !nac::decode(incoming,added,error))
        throw std::invalid_argument(error);
    requireUnique(tracks,added);
    std::set<std::uint32_t> ids;
    for(auto const& track:tracks)ids.insert(track.id);
    std::uint32_t next=1;
    for(auto& track:added) {
        while(ids.count(next))++next;
        track.id=next++;
        tracks.push_back(std::move(track));
    }
    Wire result;
    if(!nac::encode(tracks,result,error))throw std::invalid_argument(error);
    std::vector<nac::Track> verified;
    if(!nac::decode(result,verified,error) || verified.size()!=tracks.size())
        throw std::invalid_argument("Merged NAC validation failed. "+error);
    return result;
}
} // namespace

std::vector<std::uint8_t> appendDictionary(Wire const& existing,Wire const& incoming,
                                            std::string const& path) {
    using namespace shared_assets;
    auto const ext=detail::extension(path);
    if(ext==".nac")return mergeAudio(existing,incoming);

    auto const old=detail::parse_list_file(existing);
    auto const added=detail::parse_list_file(incoming);
    Wire body;
    if(ext==".ytd") {
        requireUnique(load_textures_from_bytes(existing,path),
                      load_textures_from_bytes(incoming,path));
        body=mergeTextures(old,added);
    } else if(ext==".ydd") {
        requireUnique(load_drawables_from_bytes(existing,path),
                      load_drawables_from_bytes(incoming,path));
        body=mergeDrawables(old,added);
    } else {
        throw std::invalid_argument("This asset type does not support adding entries.");
    }
    auto const count=static_cast<std::uint32_t>(old.entries+added.entries);
    auto const header=Bytes(existing);
    auto const schema=static_cast<std::uint16_t>(
        header.integer(4,1)==2?header.integer(10,2):header.integer(12,2));
    auto result=detail::serialize_list_file_v2(old.kind,schema,count,metadata(existing),body);
    auto const actual=ext==".ytd"?load_textures_from_bytes(result,path).size():
                                 load_drawables_from_bytes(result,path).size();
    require(actual==count,"Merged dictionary entry count mismatch.");
    return result;
}
} // namespace northstarcpp::editor::content_import
