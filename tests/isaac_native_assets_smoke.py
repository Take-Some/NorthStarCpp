#!/usr/bin/env python3
"""Validate Isaac's native character asset set from original PC PAKs.

Verifies real source provenance, NEF8-YMT skeleton, NEF8-YDD geometry,
source twelve-influence bone streams, XML YMAT, native YTD and prefab links.
Does NOT claim animated character acceptance or GPU viewport rendering.
"""
from __future__ import annotations
import hashlib,json,struct,sys,xml.etree.ElementTree as ET,zlib
from pathlib import Path
from blake3 import blake3

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from native_ytd import validate_ytd

BASE=ROOT/"engine/northstarcpp/resources/sharedAssets/characters/isaac"
manifest=json.loads((BASE/"source/native-isaac-report.json").read_text("utf8"))

def raw_nef(path,kind):
    packed=path.read_bytes()
    assert packed[:4]==b"NEF8" and struct.unpack_from("<H",packed,6)[0]==kind,path
    length=struct.unpack_from("<Q",packed,16)[0]
    out=zlib.decompress(packed[-length:],-15)
    assert len(out)==struct.unpack_from("<Q",packed,24)[0]
    assert blake3(out).digest()==packed[32:64]
    return packed,out

assert len(manifest["source_paks"])==6
for entry in manifest["source_paks"]:
    source=Path(entry["source"])
    assert hashlib.sha256(source.read_bytes()).hexdigest()==entry["sha256"]

rig_sizes={}
for name,entry in manifest["skeletons"].items():
    packed,raw=raw_nef(BASE/entry["name"],10)
    xml=ET.fromstring(raw)
    joints=xml.find("Skeleton").findall("Joint")
    assert len(joints)==entry["joint_count"]
    assert hashlib.sha256(packed).hexdigest()==entry["sha256"]
    assert all(int(e.attrib["index"])==i for i,e in enumerate(joints))
    rig_sizes[entry["name"].split("/")[-1]]=len(joints)
assert rig_sizes=={
    "isaac.ymt":757,
    "isaac_jacket_cloth.ymt":253,
    "isaac_apple.ymt":9,
}
assert manifest["skeletons"]["main"]["inferred_helpers"]["unresolved_identity"]==276

packed,_=raw_nef(BASE/"textures/isaac.ytd",1)
tv=validate_ytd(packed,157)
assert tv["textures"]==157 and tv["mip_levels"]==1569
fullres=json.loads((BASE/"source/fullres-texture-report.json").read_text("utf8"))
assert fullres["count"]==157 and fullres["stats"]["upgraded"]==152
assert fullres["stats"]["matching_source_local_mips"]==1109
assert fullres["stats"]["original_streamed_mip_levels"]==1569
assert fullres["native"]["sha256"]==hashlib.sha256(packed).hexdigest()
assert hashlib.sha256(packed).hexdigest()==manifest["textures"]["sha256"]
available={
    "tex_"+x["hash64"] for source in json.loads(
        (BASE/"source/native_vram/vram.manifest.json").read_text("utf8")
    )["sources"] for x in source["entries"]
}
assert len(available)==157

ymat=(BASE/"materials/isaac.ymat").read_bytes()
assert ymat[:4]==b"NEF8" and struct.unpack_from("<H",ymat,8)[0]==4
length=struct.unpack_from("<Q",ymat,24)[0]
xml=zlib.decompress(ymat[128:128+length],-15)
assert blake3(xml).digest()==ymat[64:96]
material_nodes=ET.fromstring(xml).findall("Material")
materials={e.attrib["name"] for e in material_nodes}
assert len(material_nodes)==len(materials)==24
assert hashlib.sha256(ymat).hexdigest()==manifest["materials"]["sha256"]
links=0
for element in material_nodes:
    assert element.attrib["shader"]=="pbr.default"
    textures=element.findall("Textures/Texture")
    assert len({t.attrib["slot"] for t in textures})==len(textures)
    for t in textures:
        path,name=t.attrib["ref"].split("@")
        assert path=="characters/isaac/textures/isaac.ytd" and name in available
        links+=1
assert links==352
assert any(m.attrib["name"]=="mat_7d3a58427bb3dbb5_isaac-head_00083870"
               and any(t.attrib["slot"]=="base_color"
                       for t in m.findall("Textures/Texture"))
           for m in material_nodes), "Isaac eyes lost source sclera"

def check_model(name,expected_rig,expected_meshes=None):
    packed,raw=raw_nef(BASE/name,6)
    assert raw[:4]==b"NVRM"
    meta_size=struct.unpack_from("<I",raw,8)[0]
    geometry_size=struct.unpack_from("<Q",raw,12)[0]
    assert len(raw)==20+meta_size+geometry_size
    meta=json.loads(raw[20:20+meta_size])
    assert meta["material_library"]=="characters/isaac/materials/isaac.ymat"
    records=meta["meshes"]
    assert len(records)==len(meta["material_slots"])
    if expected_meshes is not None:assert len(records)==expected_meshes
    for slot in meta["material_slots"]:assert slot["name"] in materials
    geo=20+meta_size
    num_vertices=0;num_triangles=0
    for record in records:
        n=record["vertex_count"]
        num_vertices+=n
        assert record["skin_binding"]["skeleton"]=="characters/isaac/models/"+expected_rig
        streams={s["semantic"]:s for s in record["streams"]}
        assert {"position","normal","texcoord0","joint_indices","joint_weights"}<=streams.keys()
        skin=streams["joint_indices"];weights=streams["joint_weights"]
        normal=streams["normal"]
        for i in range(n):
            nx,ny,nz=struct.unpack_from("<3f",raw,geo+normal["offset"]+i*12)
            assert .985<=(nx*nx+ny*ny+nz*nz)**.5<=1.015, "Isaac normal codec invalid"
        assert skin["format"]=="u16x12" and skin["stride"]==24 and skin["length"]==n*24
        assert weights["format"]=="f32x12" and weights["stride"]==48 and weights["length"]==n*48
        for i in range(n):
            js=struct.unpack_from("<12H",raw,geo+skin["offset"]+i*24)
            ws=struct.unpack_from("<12f",raw,geo+weights["offset"]+i*48)
            assert .98<=sum(ws)<=1.02
            assert all(j<rig_sizes[expected_rig] for j,w in zip(js,ws) if w>0)
        ib=record["index_buffer"]
        assert ib["format"]=="u32"
        assert ib["index_count"]%3==0
        num_triangles+=ib["index_count"]//3
    return packed,num_vertices,num_triangles

for source,info in manifest["parts"].items():
    file=BASE/info["path"]
    raw,verts,tris=check_model(info["path"],info["rig"],info["submeshes"])
    assert verts==info["vertices"] and tris==info["triangles"]
    assert hashlib.sha256(raw).hexdigest()==info["sha256"]

for path,info in manifest["assembled"].items():
    raw,verts,tris=check_model(path,"isaac.ymt",info["submeshes"])
    assert verts==info["vertices"] and tris==info["triangles"]
    assert hashlib.sha256(raw).hexdigest()==info["sha256"]
assert manifest["assembled"]["models/isaac.ydd"]["vertices"]==79266
assert manifest["assembled"]["models/isaac.ydd"]["triangles"]==110029

character=json.loads((BASE/"isaac.character.json").read_text("utf8"))
assert character["schema"]=="northstar.character.v1"
assert character["model"]=="characters/isaac/models/isaac.ydd"
assert character["skeleton"]=="characters/isaac/models/isaac.ymt"
assert character["appearance"]["material"]=="characters/isaac/materials/isaac.ymat"
assert character["appearance"]["textures"]=="characters/isaac/textures/isaac.ytd"
assert character["animations"]==[]
prefab=(ROOT/"engine/northstarcpp/resources/sharedAssets/prefabs/characters/isaac.nprefab").read_text("utf8")
assert prefab.startswith("NorthStarCpp.Scene 3\n")
assert '"characters/isaac/models/isaac.ydd"' in prefab

print("PASS: original Isaac PAK SHA256 x6; YDD 79,266 vertices/110,029 triangles; "
      "757-bone main YMT plus 253/9-bone auxiliary rigs; 157 genuine full-resolution textures/1569 mips; "
      "24 source YMAT materials/352 texture bindings; 12-influence skin streams; prefab paths. "
      "YCD and GPU image QA pending.")
