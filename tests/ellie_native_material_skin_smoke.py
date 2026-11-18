#!/usr/bin/env python3
"""Validate original Ellie native YTD + XML YMAT + NVRM skin linkage.

This is file-level QA, NOT a GPU screenshot / in-game animation acceptance.
"""
import hashlib,json,struct,sys,xml.etree.ElementTree as ET,zlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from import_ellie_shared_assets import check_nef8
from native_ytd import validate_ytd

BASE=ROOT/"engine/northstarcpp/resources/sharedAssets/characters/ellie"

ytd=BASE/"textures/ellie.ytd"
tex_validation=validate_ytd(check_nef8(ytd,1),397)
assert tex_validation["textures"]==397 and tex_validation["mip_levels"]==4047
tex_names={"tex_"+tex["hash64"] for tex in json.loads(
    (BASE/"source/native-ellie-textures-report.json").read_text("utf8"))["texture_manifest"]}

ymat=check_nef8(BASE/"materials/ellie.ymat",4)
unpacked=zlib.decompress(ymat[128:128+struct.unpack_from("<Q",ymat,24)[0]],-15)
materials=ET.fromstring(unpacked).findall("Material")
assert len(materials)==42
material_names={material.attrib["name"] for material in materials}
assert len(material_names)==len(materials)
all_refs=0
for material in materials:
    assert material.attrib["shader"]=="pbr.default"
    surface=material.find("Surface")
    assert surface is not None
    bindings=material.findall("Textures/Texture")
    assert len({binding.attrib["slot"] for binding in bindings})==len(bindings)
    assert len(bindings)>0
    for binding in bindings:
        vfs,tex_name=binding.attrib["ref"].split("@")
        assert vfs=="characters/ellie/textures/ellie.ytd"
        assert tex_name in tex_names
        all_refs+=1
    for param in material.findall("Params/Param"):
        assert param.attrib["type"] in ("float","float2","float3","float4")
assert all_refs==822

def inspect_ydd(path):
    buffer=check_nef8(path,6)
    compressed=struct.unpack_from("<Q",buffer,16)[0]
    raw=zlib.decompress(buffer[-compressed:],-15)
    assert raw[:4]==b"NVRM"
    meta_size=struct.unpack_from("<I",raw,8)[0]
    doc=json.loads(raw[20:20+meta_size].decode("utf8"))
    assert doc["material_library"]=="characters/ellie/materials/ellie.ymat"
    meshes=doc["meshes"]
    assert len(doc["material_slots"])==len(meshes)
    for slot in doc["material_slots"]:
        assert slot["name"] in material_names
    data_offset=20+meta_size
    source_count=0
    for mesh in meshes:
        vertex_count=mesh["vertex_count"]
        streams={s["semantic"]:s for s in mesh["streams"]}
        assert {"position","normal","texcoord0","joint_indices","joint_weights"}<=streams.keys()
        assert mesh["skin_binding"]["skeleton"]=="characters/ellie/models/ellie.ymt"
        js=streams["joint_indices"];ws=streams["joint_weights"]
        assert js["stride"]==24 and ws["stride"]==48
        assert js["length"]==vertex_count*24 and ws["length"]==vertex_count*48
        assert mesh["primitives"][0]["material_slot"]<len(meshes)
        for i in range(vertex_count):
            bones=struct.unpack_from("<12H",raw,data_offset+js["offset"]+i*24)
            weights=struct.unpack_from("<12f",raw,data_offset+ws["offset"]+i*48)
            assert abs(sum(weights)-1)<0.011
            assert all(0<=v<=1 for v in weights)
            assert all(bone<1074 for bone,w in zip(bones,weights) if w>0)
        source_count+=vertex_count
    return len(meshes),source_count

count=0;total=0
for file in sorted((BASE/"models/parts").glob("*.ydd")):
    n,v=inspect_ydd(file);count+=n;total+=v
assert count==47 and total==115925
assert inspect_ydd(BASE/"models/ellie_preview.ydd")== (15,49119)
# The canonical character prefab must target a real native YDD, not a missing placeholder.
assert inspect_ydd(BASE/"models/ellie.ydd")== (38,100038)
character=json.loads((BASE/"ellie.character.json").read_text("utf8"))
assert character["model"]=="characters/ellie/models/ellie.ydd"
assert (BASE/"models/ellie.ydd").is_file()

ice=json.loads((BASE/"source/native_animations/manifest.json").read_text("utf8"))
assert ice["count"]==72 and ice["total_bytes"]==3783440
for item in ice["clips"]:
    file=BASE/"source/native_animations"/item["filename"]
    raw=file.read_bytes()
    assert raw[:8]==bytes.fromhex("020143414c1bbe5a")
    assert len(raw)==item["bytes"]
    assert hashlib.sha256(raw).hexdigest()==item["sha256"]

print("PASS: 397 original full-resolution texturedict3 textures/4047 mips in native NETD YTD; 42 source XML YMAT materials/822 bindings; 47 NVRM part meshes/115925 vertices with valid 12-weight source bone links; 72 ICE source clips (NOT YCD); renderer QA pending")
