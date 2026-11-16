#!/usr/bin/env python3
"""NSCN/NEF8 codec v2: no serialized Pawn flag, v1 importer upgrades to Pawn."""
from pathlib import Path
R=Path(__file__).resolve().parents[1]
def edit(name,items):
 p=R/name
 s=p.read_text(encoding="utf-8-sig"); before=s
 for old,new,count in items:
  found=s.count(old)
  if found!=count:raise RuntimeError(f"{name}: {found}/{count} matches for {old[:75]!r}")
  s=s.replace(old,new)
 if s!=before:p.write_text(s,encoding="utf-8",newline="");print("updated",name)
bin="engine/northstarcpp/modules/host/scene_binary.cpp"
edit(bin,[
 ('#include <northstarcpp/scene_component_registry.hpp>','#include <northstarcpp/scene_component_registry.hpp>\n#include <northstarcpp/scene_pawn.hpp>',1),
 ('void write_entity(Writer& w, SceneObject const& e) {','void write_entity(Writer& w, SceneObject const& e,bool legacy_wire=false) {',1),
 ('    w.boolean(e.player);','    if(legacy_wire)w.boolean(false); // NSCN-v1 reserved field, never authoritative',1),
 ('SceneObject read_entity(Reader& r) {','SceneObject read_entity(Reader& r,bool legacy_wire,std::uint32_t* imported_flag=nullptr) {',1),
 ('    e.player=r.boolean();','    auto const old_owned=legacy_wire?r.boolean():false;\n    if(imported_flag)*imported_flag=old_owned?1u:0u;',1),
 ('for(auto const& entity:scene.objects)write_entity(payload,entity);','for(auto const& entity:scene.objects)write_entity(payload,entity,true);',1),
 ('    for(std::uint32_t i=0;i<objects;++i)scene.objects.push_back(read_entity(r));',
  '''    std::vector<std::uint64_t> old_owned_ids;
    for(std::uint32_t i=0;i<objects;++i) {
        std::uint32_t was_owned=0;
        auto entity=read_entity(r,true,&was_owned);
        if(was_owned)old_owned_ids.push_back(entity.node.id);
        scene.objects.push_back(std::move(entity));
    }''',1),
 ('    if(r.offset!=bytes.size())\n        throw std::runtime_error("NSCN contains trailing unrecognized data");',
  '''    if(r.offset!=bytes.size())
        throw std::runtime_error("NSCN contains trailing unrecognized data");
    for(auto id:old_owned_ids)scene_pawn::migrate_imported_local(scene,id);''',1),
 ('        Writer w;write_entity(w,entity);\n        records.push_back({RecordKind::entity,entity.node.id,std::move(w.bytes)});',
  '''        Writer w;write_entity(w,entity);
        records.push_back({RecordKind::entity,entity.node.id,std::move(w.bytes),2});''',1),
 ('    bool header_seen=false;','    bool header_seen=false;\n    std::vector<std::uint64_t> old_owned_ids;',1),
 ('        } else if(record.kind==RecordKind::entity && record.version==1) {',
  '        } else if(record.kind==RecordKind::entity && (record.version==1 || record.version==2)) {',1),
 ('            auto entity=read_entity(r);\n            if(!record.id',
  '''            std::uint32_t was_owned=0;
            auto entity=read_entity(r,record.version==1,&was_owned);
            if(was_owned)old_owned_ids.push_back(entity.node.id);
            if(!record.id''',1),
 ('    validate_scene(scene);\n    return scene;\n}\n} // namespace scene_codec_detail',
  '''    for(auto id:old_owned_ids)scene_pawn::migrate_imported_local(scene,id);
    validate_scene(scene);
    return scene;
}
} // namespace scene_codec_detail''',1),
])
v3="engine/northstarcpp/modules/host/scene_v3.cpp"
edit(v3,[
 ('#include <northstarcpp/scene.hpp>','#include <northstarcpp/scene.hpp>\n#include <northstarcpp/scene_pawn.hpp>',1),
 ('        if (object.player)\n            require(node.cell.empty() || !scene.cells.empty(),\n                    "Player needs a persistent or valid cell assignment");','',1),
 ('    std::unordered_map<std::uint64_t,std::string> scripts;','    std::unordered_map<std::uint64_t,std::string> scripts;\n    std::vector<std::uint64_t> old_owned_ids;',1),
 ('            object.player = player == 1;','            if(player==1)old_owned_ids.push_back(object.node.id);',1),
 ('    require(scripts.empty(),"Script component references a missing scene entity");',
  '    require(scripts.empty(),"Script component references a missing scene entity");\n    for(auto id:old_owned_ids)scene_pawn::migrate_imported_local(scene,id);',1),
 ("            ' ' << object.player << ' ' << object.mass << ' ';",
  "            ' ' << 0 << ' ' << object.mass << ' '; // text-v3 compatibility slot",1),
])
print("NEF8 writes entity record v2, legacy scene import upgraded")
