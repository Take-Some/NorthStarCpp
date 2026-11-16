// NorthStarCpp NSCN binary scene codec v1.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
// Portable little-endian fields, explicit sizes, checksum and bounded decoding.
#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_component_registry.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include "scene_nef8_records.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace northstarcpp {
namespace {

constexpr std::size_t maximum_document_bytes=16u*1024u*1024u;
constexpr std::size_t header_bytes=16;
constexpr std::uint16_t codec_major=1;
constexpr std::uint16_t codec_minor=0;

std::uint32_t crc32(std::string const& bytes) {
    std::uint32_t crc=0xffffffffu;
    for (unsigned char b:bytes) {
        crc^=b;
        for (int k=0;k<8;++k)
            crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}

struct Writer {
    std::string bytes;
    void u8(std::uint8_t n) { bytes.push_back(static_cast<char>(n)); }
    void u16(std::uint16_t n) {
        for (int i=0;i<2;++i)u8(static_cast<std::uint8_t>(n>>(8*i)));
    }
    void u32(std::uint32_t n) {
        for (int i=0;i<4;++i)u8(static_cast<std::uint8_t>(n>>(8*i)));
    }
    void u64(std::uint64_t n) {
        for (int i=0;i<8;++i)u8(static_cast<std::uint8_t>(n>>(8*i)));
    }
    void f32(float v) {
        static_assert(sizeof(float)==4,"NSCN requires IEEE-754 float32");
        std::uint32_t bits=0;
        std::memcpy(&bits,&v,sizeof(bits));
        u32(bits);
    }
    void vector(Vector3 v) {f32(v.x);f32(v.y);f32(v.z);}
    void text(std::string const& v) {
        if(v.size()>std::numeric_limits<std::uint16_t>::max())
            throw std::runtime_error("NSCN string too large");
        u16(static_cast<std::uint16_t>(v.size()));
        bytes.append(v);
    }
    void boolean(bool value) {u8(value?1:0);}
};

struct Reader {
    std::string const& bytes;
    std::size_t offset=0;
    std::size_t limit=0;
    explicit Reader(std::string const& b,std::size_t start,std::size_t end)
        :bytes(b),offset(start),limit(end){}
    void need(std::size_t count) {
        if (count>limit-offset)
            throw std::runtime_error("Truncated NSCN binary scene");
    }
    std::uint8_t u8() {
        need(1);
        return static_cast<std::uint8_t>(bytes[offset++]);
    }
    std::uint16_t u16() {
        std::uint16_t n=0;
        for (int i=0;i<2;++i)n|=std::uint16_t(u8())<<(8*i);
        return n;
    }
    std::uint32_t u32() {
        std::uint32_t n=0;
        for (int i=0;i<4;++i)n|=std::uint32_t(u8())<<(8*i);
        return n;
    }
    std::uint64_t u64() {
        std::uint64_t n=0;
        for (int i=0;i<8;++i)n|=std::uint64_t(u8())<<(8*i);
        return n;
    }
    float f32() {
        auto bits=u32();
        float value=0;
        std::memcpy(&value,&bits,sizeof(value));
        return value;
    }
    Vector3 vector() {return {f32(),f32(),f32()};}
    std::string text(std::size_t max_bytes) {
        auto const length=u16();
        if(length>max_bytes)
            throw std::runtime_error("NSCN string exceeds field limit");
        need(length);
        std::string result(bytes.data()+offset,length);
        offset+=length;
        return result;
    }
    bool boolean() {
        auto const value=u8();
        if(value>1)
            throw std::runtime_error("Invalid NSCN boolean");
        return value==1;
    }
    std::uint32_t count(std::uint32_t bound) {
        auto const value=u32();
        if(value>bound)
            throw std::runtime_error("NSCN section count exceeds limit");
        return value;
    }
};

void write_entity(Writer& w, SceneObject const& e,bool legacy_wire=false) {
    w.text(e.name);
    w.vector(e.position);
    w.f32(e.size);
    w.u32(e.color);
    if(legacy_wire)w.boolean(false); // NSCN-v1 reserved field, never authoritative
    w.f32(e.mass);
    auto const& n=e.node;
    w.u64(n.id);
    w.u64(n.parent);
    w.text(n.layer);
    w.text(n.cell);
    w.text(n.room);
    w.text(n.asset);
    w.text(n.script);
    w.vector(n.rotation);
    w.vector(n.scale);
    w.f32(n.lod_near);
    w.f32(n.lod_far);
    w.boolean(n.visible);
    w.boolean(n.collision);
}

SceneObject read_entity(Reader& r,bool legacy_wire,std::uint32_t* imported_flag=nullptr) {
    SceneObject e;
    e.name=r.text(96);
    e.position=r.vector();
    e.size=r.f32();
    e.color=r.u32();
    auto const old_owned=legacy_wire?r.boolean():false;
    if(imported_flag)*imported_flag=old_owned?1u:0u;
    e.mass=r.f32();
    auto& n=e.node;
    n.id=r.u64();
    n.parent=r.u64();
    n.layer=r.text(96);
    n.cell=r.text(96);
    n.room=r.text(96);
    n.asset=r.text(512);
    n.script=r.text(512);
    n.rotation=r.vector();
    n.scale=r.vector();
    n.lod_near=r.f32();
    n.lod_far=r.f32();
    n.visible=r.boolean();
    n.collision=r.boolean();
    return e;
}

} // namespace

std::string scene_codec_detail::encode_legacy_nscn(Scene const& scene) {
    if(scene.version!=3)
        throw std::invalid_argument("NSCN codec only accepts Scene V3");
    validate_scene(scene);

    Writer payload;
    payload.text(scene.name);
    auto const& s=scene.streaming;
    payload.f32(s.load_radius);
    payload.f32(s.unload_radius);
    payload.f32(s.lookahead_seconds);
    payload.u32(s.max_cell_loads_per_update);
    payload.u32(s.max_cell_unloads_per_update);
    payload.u32(static_cast<std::uint32_t>(scene.objects.size()));
    payload.u32(static_cast<std::uint32_t>(scene.layers.size()));
    payload.u32(static_cast<std::uint32_t>(scene.cells.size()));
    payload.u32(static_cast<std::uint32_t>(scene.volumes.size()));
    payload.u32(static_cast<std::uint32_t>(scene.portals.size()));
    for(auto const& entity:scene.objects)write_entity(payload,entity,true);
    for(auto const& layer:scene.layers) {
        payload.text(layer.name);payload.boolean(layer.visible);
    }
    for(auto const& cell:scene.cells) {
        payload.text(cell.name);payload.vector(cell.minimum);
        payload.vector(cell.maximum);payload.boolean(cell.always_loaded);
    }
    for(auto const& volume:scene.volumes) {
        payload.text(volume.name);
        payload.u8(volume.shape==SceneVolume::Shape::box?1:0);
        payload.vector(volume.minimum);payload.vector(volume.maximum);
        payload.f32(volume.radius);payload.text(volume.tag);
        payload.boolean(volume.enabled);
    }
    for(auto const& portal:scene.portals) {
        payload.text(portal.name);
        payload.text(portal.room_a);payload.text(portal.room_b);
        payload.vector(portal.minimum);payload.vector(portal.maximum);
        payload.boolean(portal.open);
    }
    if(payload.bytes.size()>maximum_document_bytes-header_bytes)
        throw std::length_error("NSCN scene exceeds 16 MiB");
    Writer file;
    file.bytes="NSCN";
    file.u16(codec_major);
    file.u16(codec_minor);
    file.u32(static_cast<std::uint32_t>(payload.bytes.size()));
    file.u32(crc32(payload.bytes));
    file.bytes+=payload.bytes;
    return file.bytes;
}

Scene scene_codec_detail::decode_legacy_nscn(std::string const& bytes) {
    if(bytes.size()<header_bytes || bytes.size()>maximum_document_bytes ||
        bytes.compare(0,4,"NSCN")!=0)
        throw std::runtime_error("Invalid NSCN header or file size");
    Reader header(bytes,4,header_bytes);
    auto const major=header.u16();
    auto const minor=header.u16();
    if(major!=codec_major || minor>codec_minor)
        throw std::runtime_error("Unsupported NSCN binary scene version");
    auto const length=header.u32();
    auto const checksum=header.u32();
    if(length!=bytes.size()-header_bytes)
        throw std::runtime_error("NSCN payload length mismatch");
    if(crc32(bytes.substr(header_bytes))!=checksum)
        throw std::runtime_error("NSCN checksum mismatch");
    Reader r(bytes,header_bytes,bytes.size());
    Scene scene;
    scene.version=3;
    scene.name=r.text(128);
    auto& s=scene.streaming;
    s.load_radius=r.f32();
    s.unload_radius=r.f32();
    s.lookahead_seconds=r.f32();
    s.max_cell_loads_per_update=r.u32();
    s.max_cell_unloads_per_update=r.u32();
    auto const objects=r.count(static_cast<std::uint32_t>(max_scene_objects));
    auto const layers=r.count(256);
    auto const cells=r.count(8192);
    auto const volumes=r.count(8192);
    auto const portals=r.count(8192);
    scene.objects.reserve(objects);
    scene.layers.reserve(layers);
    scene.cells.reserve(cells);
    scene.volumes.reserve(volumes);
    scene.portals.reserve(portals);
    std::vector<std::uint64_t> old_owned_ids;
    for(std::uint32_t i=0;i<objects;++i) {
        std::uint32_t was_owned=0;
        auto entity=read_entity(r,true,&was_owned);
        if(was_owned)old_owned_ids.push_back(entity.node.id);
        scene.objects.push_back(std::move(entity));
    }
    for(std::uint32_t i=0;i<layers;++i) {
        SceneLayer l;
        l.name=r.text(96);l.visible=r.boolean();
        scene.layers.push_back(std::move(l));
    }
    for(std::uint32_t i=0;i<cells;++i) {
        SceneCell c;
        c.name=r.text(96);c.minimum=r.vector();c.maximum=r.vector();
        c.always_loaded=r.boolean();
        scene.cells.push_back(std::move(c));
    }
    for(std::uint32_t i=0;i<volumes;++i) {
        SceneVolume v;
        v.name=r.text(96);
        auto const shape=r.u8();
        if(shape>1)throw std::runtime_error("Invalid NSCN volume shape");
        v.shape=shape?SceneVolume::Shape::box:SceneVolume::Shape::sphere;
        v.minimum=r.vector();v.maximum=r.vector();
        v.radius=r.f32();v.tag=r.text(96);v.enabled=r.boolean();
        scene.volumes.push_back(std::move(v));
    }
    for(std::uint32_t i=0;i<portals;++i) {
        ScenePortal p;
        p.name=r.text(96);p.room_a=r.text(96);p.room_b=r.text(96);
        p.minimum=r.vector();p.maximum=r.vector();p.open=r.boolean();
        scene.portals.push_back(std::move(p));
    }
    if(r.offset!=bytes.size())
        throw std::runtime_error("NSCN contains trailing unrecognized data");
    for(auto id:old_owned_ids)scene_pawn::migrate_imported_local(scene,id);
    validate_scene(scene);
    return scene;
}


// Reuse the same portable field encoders for both old NSCN v1 and new
// NEF8 per-component records. Do not serialize raw C++ struct padding.
namespace scene_codec_detail {
std::vector<Record> encode_records(Scene const& scene) {
    if(scene.version!=3)throw std::invalid_argument("ListFile scene requires V3");
    validate_scene(scene);
    std::vector<Record> records;
    records.reserve(1+scene.objects.size()+scene.layers.size()+scene.cells.size()+
                    scene.volumes.size()+scene.portals.size());
    Writer header;
    header.text(scene.name);
    auto const& s=scene.streaming;
    header.f32(s.load_radius);header.f32(s.unload_radius);
    header.f32(s.lookahead_seconds);
    header.u32(s.max_cell_loads_per_update);
    header.u32(s.max_cell_unloads_per_update);
    records.push_back({RecordKind::document,0,std::move(header.bytes)});
    for(auto const& entity:scene.objects) {
        Writer w;write_entity(w,entity);
        records.push_back({RecordKind::entity,entity.node.id,std::move(w.bytes),2});
    }
    for(auto const& layer:scene.layers) {
        Writer w;w.text(layer.name);w.boolean(layer.visible);
        records.push_back({RecordKind::layer,0,std::move(w.bytes)});
    }
    for(auto const& cell:scene.cells) {
        Writer w;w.text(cell.name);w.vector(cell.minimum);w.vector(cell.maximum);
        w.boolean(cell.always_loaded);
        records.push_back({RecordKind::cell,0,std::move(w.bytes)});
    }
    for(auto const& volume:scene.volumes) {
        Writer w;w.text(volume.name);
        w.u8(volume.shape==SceneVolume::Shape::box?1:0);
        w.vector(volume.minimum);w.vector(volume.maximum);
        w.f32(volume.radius);w.text(volume.tag);w.boolean(volume.enabled);
        records.push_back({RecordKind::volume,0,std::move(w.bytes)});
    }
    for(auto const& portal:scene.portals) {
        Writer w;w.text(portal.name);w.text(portal.room_a);w.text(portal.room_b);
        w.vector(portal.minimum);w.vector(portal.maximum);w.boolean(portal.open);
        records.push_back({RecordKind::portal,0,std::move(w.bytes)});
    }
    for(auto const& c:scene.components) {
        Writer w;
        w.text(c.type);
        w.u32(c.version);
        w.u32(static_cast<std::uint32_t>(c.payload.size()));
        w.bytes+=c.payload;
        records.push_back({RecordKind::plugin_component,c.entity_id,std::move(w.bytes)});
    }
    for(auto const& opaque:scene.opaque_records)
        records.push_back({static_cast<RecordKind>(opaque.kind),
                           opaque.entity_id,opaque.payload,opaque.version});
    return records;
}
Scene decode_records(std::vector<Record> const& records) {
    if(records.empty() || records.front().kind!=RecordKind::document ||
       records.front().id!=0)
        throw std::runtime_error("Scene listfile is missing its document record");
    Scene scene;
    scene.version=3;
    bool header_seen=false;
    std::vector<std::uint64_t> old_owned_ids;
    for(auto const& record:records) {
        Reader r(record.bytes,0,record.bytes.size());
        if(record.kind==RecordKind::document && record.version==1) {
            if(header_seen || record.id!=0)
                throw std::runtime_error("Duplicate or invalid scene document header");
            header_seen=true;
            scene.name=r.text(128);
            auto& s=scene.streaming;
            s.load_radius=r.f32();s.unload_radius=r.f32();
            s.lookahead_seconds=r.f32();
            s.max_cell_loads_per_update=r.u32();
            s.max_cell_unloads_per_update=r.u32();
        } else if(record.kind==RecordKind::entity && (record.version==1 || record.version==2)) {
            if(scene.objects.size()>=max_scene_objects)
                throw std::runtime_error("Scene ListFile entity count exceeds limit");
            std::uint32_t was_owned=0;
            auto entity=read_entity(r,record.version==1,&was_owned);
            if(was_owned)old_owned_ids.push_back(entity.node.id);
            if(!record.id || entity.node.id!=record.id)
                throw std::runtime_error("Scene ListFile entity ID does not match record");
            scene.objects.push_back(std::move(entity));
        } else if(record.kind==RecordKind::layer && record.version==1) {
            if(record.id || scene.layers.size()>=256)
                throw std::runtime_error("Invalid ListFile layer entry");
            SceneLayer layer{r.text(96),r.boolean()};
            scene.layers.push_back(std::move(layer));
        } else if(record.kind==RecordKind::cell && record.version==1) {
            if(record.id || scene.cells.size()>=8192)
                throw std::runtime_error("Invalid ListFile cell entry");
            SceneCell cell;
            cell.name=r.text(96);cell.minimum=r.vector();cell.maximum=r.vector();
            cell.always_loaded=r.boolean();
            scene.cells.push_back(std::move(cell));
        } else if(record.kind==RecordKind::volume && record.version==1) {
            if(record.id || scene.volumes.size()>=8192)
                throw std::runtime_error("Invalid ListFile volume entry");
            SceneVolume v;
            v.name=r.text(96);
            auto shape=r.u8();
            if(shape>1)throw std::runtime_error("Invalid ListFile volume shape");
            v.shape=shape?SceneVolume::Shape::box:SceneVolume::Shape::sphere;
            v.minimum=r.vector();v.maximum=r.vector();v.radius=r.f32();
            v.tag=r.text(96);v.enabled=r.boolean();
            scene.volumes.push_back(std::move(v));
        } else if(record.kind==RecordKind::portal && record.version==1) {
            if(record.id || scene.portals.size()>=8192)
                throw std::runtime_error("Invalid ListFile portal entry");
            ScenePortal p;
            p.name=r.text(96);p.room_a=r.text(96);p.room_b=r.text(96);
            p.minimum=r.vector();p.maximum=r.vector();p.open=r.boolean();
            scene.portals.push_back(std::move(p));
        } else if(record.kind==RecordKind::plugin_component &&
                  record.version==1) {
            SceneComponentBlob blob;
            blob.entity_id=record.id;
            blob.type=r.text(128);
            blob.version=r.u32();
            auto length=r.count(1048576);
            r.need(length);
            blob.payload=r.bytes.substr(r.offset,length);
            r.offset+=length;
            if(!validate_scene_component_blob(blob))
                throw std::runtime_error("Scene ECS component failed provider validation");
            scene.components.push_back(std::move(blob));
        } else {
            // Unknown (kind, version) records are not decoded, but survive
            // repeated load/edit/save cycles without requiring the plugin.
            if(record.bytes.size()>1048576)
                throw std::runtime_error("Opaque Scene record exceeds size limit");
            scene.opaque_records.push_back({
                static_cast<std::uint16_t>(record.kind),
                record.version,record.id,record.bytes});
            r.offset=record.bytes.size();
        }
        if(r.offset!=record.bytes.size())
            throw std::runtime_error("Scene ListFile record contains trailing bytes");
    }
    for(auto id:old_owned_ids)scene_pawn::migrate_imported_local(scene,id);
    validate_scene(scene);
    return scene;
}
} // namespace scene_codec_detail

} // namespace northstarcpp
