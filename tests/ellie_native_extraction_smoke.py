#!/usr/bin/env python3
"""Real Ellie native extraction QA: no fake files and no renderer claims."""
from __future__ import annotations
import hashlib,json,struct,sys,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from import_ellie_shared_assets import check_nef8
from native_pak import NaughtyDogPak

BASE=ROOT/"engine/northstarcpp/resources/sharedAssets/characters/ellie"
manifest=json.loads((BASE/"source/source.manifest.json").read_text("utf8"))
assert len(manifest["files"])==22
for e in manifest["files"]:
    file=BASE/"source"/e["path"]
    content=file.read_bytes()
    assert hashlib.sha256(content).hexdigest()==e["sha256"]
    assert len(content)==e["size_bytes"]

skel=check_nef8(BASE/"models/ellie.ymt",10)
report=json.loads((BASE/"source/native-ellie-skeleton-report.json").read_text("utf8"))
assert report["joint_count"]==1074 and report["authored_transform_count"]==520
assert report["reconstructed_helpers"]["unresolved_identity"]==503

part_report=json.loads((BASE/"source/native-ellie-mesh-parts-report.json").read_text("utf8"))
assert len(part_report["files"])==15
assert part_report["total_native_parts"]==47
assert part_report["total_native_vertices"]==115925
assert part_report["skin_weights_preserved"] and not part_report["runtime_skinning_enabled"]

def check_model(path):
    encoded=check_nef8(path,6)
    compressed=struct.unpack_from("<Q",encoded,16)[0]
    body=zlib.decompress(encoded[-compressed:],-15)
    assert body[:4]==b"NVRM" and struct.unpack_from("<I",body,4)[0]==1
    meta_size=struct.unpack_from("<I",body,8)[0]
    geometry_bytes=struct.unpack_from("<Q",body,12)[0]
    assert len(body)==20+meta_size+geometry_bytes
    meta=json.loads(body[20:20+meta_size].decode("utf8"))
    for mesh in meta["meshes"]:
        count=mesh["vertex_count"]
        assert count>0
        semantics={s["semantic"]:s for s in mesh["streams"]}
        assert {"position","normal","texcoord0","joint_indices","joint_weights"}<=semantics.keys()
        assert mesh["skin_binding"]["skeleton"]=="characters/ellie/models/ellie.ymt"
        assert not mesh["skin_binding"]["runtime_skinning_enabled"]
        for stream in semantics.values():
            assert stream["stride"]>0 and stream["length"]==count*stream["stride"]
            assert 0<=stream["offset"] and stream["offset"]+stream["length"]<=geometry_bytes
        ib=mesh["index_buffer"]
        assert ib["index_count"]%3==0 and ib["length"]==ib["index_count"]*4
        assert ib["offset"]+ib["length"]<=geometry_bytes
    return len(meta["meshes"]),sum(m["vertex_count"] for m in meta["meshes"])

total_meshes=total_vertices=0
for entry in part_report["files"]:
    file=Path(entry["path"])
    assert file.is_file() and file.parent==BASE/"models/parts"
    assert hashlib.sha256(file.read_bytes()).hexdigest()==entry["sha256"]
    n,v=check_model(file)
    assert n==entry["meshes"] and v==entry["vertices"]
    total_meshes+=n;total_vertices+=v
assert (total_meshes,total_vertices)==(47,115925)
assert check_model(BASE/"models/ellie_preview.ydd")== (15,49119)

textures=json.loads((BASE/"source/native_vram/vram.manifest.json").read_text("utf8"))
assert textures["total_source_paks"]==15
assert textures["total_texture_descriptors"]==809
assert textures["total_gpu_bytes"]==8068096
for entry in textures["sources"]:
    p=Path(entry["vram_blob"])
    payload=p.read_bytes()
    assert len(payload)==entry["total_bytes"]
    for image in entry["entries"]:
        start=image["blob_offset"];end=start+image["blob_bytes"]
        assert 0<=start<end<=len(payload)
        assert hashlib.sha256(payload[start:end]).hexdigest()==image["sha256"]

prefab=(BASE.parent.parent/"prefabs/characters/ellie_preview.nprefab").read_text("utf8")
assert "characters/ellie/models/ellie_preview.ydd" in prefab
assert "NorthStarCpp.Scene 3" in prefab
print("PASS: 22 original PAKs; YMT 1074 bones (partial helpers); 15 skinned-stream NVRM YDDs (47 parts, 115925 vertices); preview YDD; 809 raw VRAM blocks; renderer skinning/materials/YCD PENDING")
