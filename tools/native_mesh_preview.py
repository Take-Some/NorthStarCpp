#!/usr/bin/env python3
"""Pure-Python Naughty Dog PAK to real native NorthStar NVRM/YDD static mesh preview.

Reconstructs *original vertices and triangle indices* of the LOD0 geometry
directly from TLOU2 Remastered PC PAK pages. Does NOT invent mesh geometry.
Does NOT export skin weights or material textures; this is explicitly a
static-preview YDD and is not a complete playable/animated character.

Supports:
- raw FP32 positions (stream type 0)
- bit-packed quantized positions (type 64)
- UV half-floats (type 1)
- bit-packed quantized UVs (type 65)
- typed original index buffers (u16)
- approximately decoded signed-byte vertex normals (types 2,130).
"""
from __future__ import annotations

import argparse
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path
from blake3 import blake3

from native_pak import NaughtyDogPak, PakError

BASE = (Path(__file__).resolve().parents[1] /
        "engine/northstarcpp/resources/sharedAssets/characters/ellie")
DEFAULT_PAKS = ("ellie-body.pak","ellie-head.pak","ellie-arms.pak")
# Explicit source outfit, not a synthetic mesh: same local world-space rest
# coordinates are preserved. Cloth physics and secondary rigs stay inactive
# until their native bind poses/attachments are truly reconstructed.
COMPLETE_OUTFIT_PAKS = (
    "ellie-body.pak", "ellie-head.pak", "ellie-arms.pak",
    "ellie-hair-cloth.pak", "ellie-strand-hair-nocloth.pak",
    "ellie-jacket-cloth.pak", "ellie-backpack.pak",
    "ellie-leg-holster-r.pak", "ellie-bracelet.pak",
    "ellie-bandage-bicep-l.pak", "ellie-bandage-forearm-r.pak",
)


def bits_lsb(data: bytes, bit_offset: int, width: int) -> int:
    if width == 0:
        return 0
    start = bit_offset // 8
    shift = bit_offset % 8
    length = (shift + width + 7) // 8
    if start + length > len(data):
        raise PakError("Packed position/UV access exceeds buffer")
    return (int.from_bytes(data[start:start+length], "little") >> shift) & ((1<<width)-1)


def parse_stream(pak: NaughtyDogPak, desc_offset: int, vertex_count: int):
    ptr = pak.pointer(desc_offset)
    num, bits, size = pak.unpack("<3I",desc_offset+8)
    ty = pak.data[desc_offset+20]
    sizes = list(pak.data[desc_offset+24:desc_offset+28])
    scale = pak.unpack("<4f",desc_offset+32)
    shift = pak.unpack("<4f",desc_offset+48)
    if num != vertex_count:
        raise PakError(f"stream {ty} vertex count {num} != {vertex_count}")
    if bits < 0 or bits > 128 or size<=0:
        raise PakError("Invalid vertex stream size")
    if ptr+size>len(pak.data):
        raise PakError("Vertex stream overflows PAK")
    return {
        "type":ty,"vertex_count":num,"bits":bits,"size":size,
        "sizes":sizes,"scale":scale,"shift":shift,
        "bytes":pak.data[ptr:ptr+size]
    }


def decode_stream(stream, dims, target):
    ty=stream["type"];buf=stream["bytes"];count=stream["vertex_count"]
    if ty in (64,65,75,76):
        lengths=stream["sizes"]
        if sum(lengths)!=stream["bits"]:
            raise PakError("Invalid packed stream bit widths")
        if count * stream["bits"] > len(buf)*8:
            raise PakError("Packed buffer shorter than declared vertex bits")
        positions=[]
        for i in range(count):
            cursor=i*stream["bits"]
            row=[]
            for j in range(dims):
                value=bits_lsb(buf,cursor,lengths[j]) if lengths[j] else 0
                cursor+=lengths[j]
                row.append(value*stream["scale"][j]+stream["shift"][j])
            positions.append(tuple(row))
        return positions
    if ty==0 and target=="position":
        if stream["bits"]!=96 or stream["size"]!=count*12:
            raise PakError("Unexpected FP32 position stream")
        return [struct.unpack_from("<3f",buf,i*12) for i in range(count)]
    if ty==1 and target=="uv":
        if stream["bits"]!=32 or stream["size"]!=count*4:
            raise PakError("Unexpected half-float UV stream")
        return [struct.unpack_from("<2e",buf,i*4) for i in range(count)]
    if ty in (2,130) and target=="normal":
        if stream["size"] < 4*count:
            raise PakError("Invalid signed-byte normal stream")
        return [
            tuple(v/(127 if v>=0 else 128) for v in struct.unpack_from("<3b",buf,i*4))
            for i in range(count)
        ]
    raise PakError(f"Unsupported {target} vertex stream {ty}")


def derived_geometry_normals(positions,indices):
    """Area-weighted normals from ORIGINAL vertex positions and face winding.

    Used only when Naughty Dog normal stream type 130 is still undecoded.
    The type-2 SNORM8x3 stream is already validated against source geometry
    and is preserved. This is explicitly a geometric lighting fallback,
    not a claim of exact imported tangent frames.
    """
    accum=[[0.,0.,0.] for _ in positions]
    for i in range(0,len(indices),3):
        ia,ib,ic=indices[i:i+3]
        pa,pb,pc=positions[ia],positions[ib],positions[ic]
        a=(pb[0]-pa[0],pb[1]-pa[1],pb[2]-pa[2])
        b=(pc[0]-pa[0],pc[1]-pa[1],pc[2]-pa[2])
        cross=(a[1]*b[2]-a[2]*b[1],
               a[2]*b[0]-a[0]*b[2],
               a[0]*b[1]-a[1]*b[0])
        for vertex in (ia,ib,ic):
            for c in range(3):accum[vertex][c]+=cross[c]
    out=[]
    for index,vector in enumerate(accum):
        length=math.sqrt(sum(v*v for v in vector))
        if length<1.e-12:
            # Degenerate/unused vertex: retain a unit-length approximation,
            # avoiding a zero lighting vector. Its original is still in PAK.
            out.append((0.,1.,0.))
        else:out.append(tuple(v/length for v in vector))
    return out

def decode_skin(pak: NaughtyDogPak, submesh_offset: int, vertex_count: int):
    """Read original 22-bit normalized weights and 10-bit skeleton joint IDs."""
    if submesh_offset + 88 not in pak.fixups:
        return None
    desc = pak.pointer(submesh_offset + 88)
    weight_count = pak.u32(desc + 4)
    uncompressed = pak.u32(desc + 8) > 0
    if weight_count > vertex_count * 12:
        raise PakError("Weight table exceeds maximum 12 influences per vertex")
    map_offset = pak.pointer(desc + 16)
    weight_offset = pak.pointer(desc + 24)
    if map_offset + vertex_count*8 > len(pak.data):
        raise PakError("Skin offset/count map exceeds PAK")
    pairs = [pak.unpack("<II", map_offset+i*8) for i in range(vertex_count)]
    if sum(count for count,_ in pairs)!=weight_count:
        raise PakError("Skin weight total does not match declared source count")
    joints = []
    weights = []
    for count,offset in pairs:
        if count > 12 or weight_offset+offset+count*(8 if uncompressed else 4) > len(pak.data):
            raise PakError("Invalid skin weights range")
        if uncompressed:
            entries=[pak.unpack("<fI",weight_offset+offset+8*j) for j in range(count)]
            ws=[float(v[0]) for v in entries]
            js=[int(v[1]) for v in entries]
            if any(j >= 1074 for j in js):
                raise PakError("Uncompressed skin joint index exceeds Ellie skeleton")
        else:
            entries=[pak.u32(weight_offset+offset+4*j) for j in range(count)]
            js=[value >> 22 for value in entries]
            ws=[(value & ((1<<22)-1))/4194304.0 for value in entries]
        if uncompressed and any(not math.isfinite(w) or w < 0 or w > 1 for w in ws):
            raise PakError("Nonfinite or invalid original float skin weight")
        if count and not uncompressed and not .995 <= sum(ws) <= 1.005:
            raise PakError("Original per-vertex skin weights do not normalize")
        joints.append(tuple((js+[0]*12)[:12]))
        weights.append(tuple((ws+[0.0]*12)[:12]))
    return {"joints":joints, "weights":weights, "source_influence_count":weight_count}


def read_meshes(pak: NaughtyDogPak, max_lod: int = 0):
    geometry=pak.typed("GEOMETRY_1")
    if len(geometry)!=1:
        raise PakError(f"{pak.path.name}: expected a GEOMETRY_1 record")
    base=geometry[0]["offset"]
    version=pak.u32(base+48)
    if version!=57:
        raise PakError(f"Unexpected geometry layout version: {version}")
    count=pak.u32(base+56)
    if not 1<=count<=256:
        raise PakError("Unexpected submesh count")
    first=pak.pointer(base+88)
    result=[]
    for i in range(count):
        # PC Remastered version 57 uses a 192-byte submesh descriptor;
        # old TLOU2 PS4 Noesis plugin assumes 176 bytes and misreads it.
        sm=first+192*i
        full_name=pak.ascii(pak.pointer(sm+32))
        name=full_name.split("|")[-1]
        lod_match=re.search(r"LODShape(\d+)",name,re.I)
        lod=int(lod_match.group(1)) if lod_match else 0
        if lod>max_lod:
            continue
        vertices,indices,streams,_=pak.unpack("<4I",sm+136)
        if not 0<vertices<=250000 or not 0<indices<=600000 or indices%3 or not 1<=streams<=20:
            raise PakError(f"Invalid mesh counts at {sm:#x}")
        stream_root=pak.pointer(sm+48)
        index_base=pak.pointer(sm+64)
        stream_map={}
        for j in range(streams):
            stream=parse_stream(pak,stream_root+j*64,vertices)
            stream_map.setdefault(stream["type"],stream)
        position=decode_stream(stream_map[0 if 0 in stream_map else 64],3,"position")
        uv=decode_stream(stream_map[1 if 1 in stream_map else 65],2,"uv")
        normal_stream=stream_map[2 if 2 in stream_map else 130]
        normals=decode_stream(normal_stream,3,"normal")
        idx=struct.unpack_from("<"+str(indices)+"H",pak.data,index_base)
        if max(idx)>=vertices:
            raise PakError("Index buffer references vertex outside native mesh")
        if normal_stream["type"]==130:
            normals=derived_geometry_normals(position,idx)
        if any(not math.isfinite(v) for row in position+uv+normals for v in row):
            raise PakError("Vertex stream contains NaN/Infinity")
        source_material=pak.pointer(sm+72)
        material_id=f"mat_{pak.u64(source_material+16):016x}_{pak.path.stem}"
        result.append({
            "name": name, "lod":lod, "source":pak.path.name,
            "position":position,"uv":uv,"normal":normals,"indices":idx,
            "normal_encoding":("original_snorm8x3" if normal_stream["type"]==2 else
                               "derived_area_weighted_geometry_type130_undecoded"),
            "skin":decode_skin(pak,sm,vertices),
            "material_name":material_id,
        })
    return result


def pack_nef8_v2(payload: bytes, name: str):
    encoder=zlib.compressobj(level=9,wbits=-15)
    compressed=encoder.compress(payload)+encoder.flush()
    header=bytearray(64)
    header[:4]=b"NEF8"
    struct.pack_into("<BBHHHIQQ",header,4,2,6,6,7,1,1,len(compressed),len(payload))
    header[32:64]=blake3(payload).digest()
    metadata=json.dumps({
        "schema":"newengine.asset.list_file.header_metadata",
        "logical_path":name,
        "content_kind":"nvrm_mesh",
        "authored_by":"NorthStarCpp native Naughty Dog PAK decoder",
        "source":"The Last of Us Part II Remastered PC",
        "build_profile":"static-preview-no-skin",
        "entries":[],
        "warnings":["Real vertices and indices from PAK; skin weights and original materials are NOT exported"],
    },separators=(",",":")).encode("utf-8")
    return bytes(header)+metadata+compressed


def build_nvrm(meshes, name):
    if not meshes or len(meshes)>128:
        raise PakError("NVRM preview needs 1..128 source submeshes")
    blob=bytearray()
    mesh_records=[]
    minimum=[float("inf")]*3
    maximum=[float("-inf")]*3
    for k,mesh in enumerate(meshes):
        count=len(mesh["position"])
        record={
            "name":"mesh_"+str(k),
            "source_pak":mesh["source"],
            "source_name":mesh["name"],
            "vertex_count":count,
            "streams":[],
            "index_buffer":{},
            "primitives":[{"first_index":0,"index_count":len(mesh["indices"]),
                           "base_vertex":0,"material_slot":k}],
        }
        for semantic, fmt, rows, stride in [
            ("position","f32x3",mesh["position"],12),
            ("normal","f32x3",mesh["normal"],12),
            ("texcoord0","f32x2",mesh["uv"],8)]:
            start=len(blob)
            dimension=3 if stride==12 else 2
            for row in rows:
                blob.extend(struct.pack("<"+str(dimension)+"f",*row))
            record["streams"].append({
                "semantic":semantic,"format":fmt,
                "offset":start,"length":len(blob)-start,"stride":stride,
            })
        skin=mesh.get("skin")
        if skin is not None:
            record["skin_binding"]={
                "skeleton":"characters/ellie/models/ellie.ymt",
                "schema":"northstar.source_skin.inert_streams.v1",
                "max_influences":12,
                "source_weight_count":skin["source_influence_count"],
                "runtime_skinning_enabled":False
            }
            for semantic,format_name,rows,stride in [
                ("joint_indices","u16x12",skin["joints"],24),
                ("joint_weights","f32x12",skin["weights"],48)]:
                start=len(blob)
                pack_format="<12H" if stride==24 else "<12f"
                for row in rows:
                    blob.extend(struct.pack(pack_format,*row))
                record["streams"].append({
                    "semantic":semantic,"format":format_name,
                    "offset":start,"length":len(blob)-start,"stride":stride
                })
        for row in mesh["position"]:
            for j in range(3):
                minimum[j]=min(minimum[j],row[j])
                maximum[j]=max(maximum[j],row[j])
        start=len(blob)
        for i in mesh["indices"]:
            blob.extend(struct.pack("<I",i))
        record["index_buffer"]={
            "format":"u32","offset":start,
            "length":len(blob)-start,"index_count":len(mesh["indices"]),
        }
        mesh_records.append(record)
    obj={
        "schema":"northstar.model.runtime.v1",
        "bounds":{"min":minimum,"max":maximum},
        "material_slots":[{"slot":i,"name":m["material_name"]} for i,m in enumerate(meshes)],
        "material_library":"characters/ellie/materials/ellie.ymat",
        "meshes":mesh_records,
    }
    metadata=json.dumps(obj,separators=(",",":"),ensure_ascii=False).encode("utf-8")
    body=b"NVRM"+struct.pack("<IIQ",1,len(metadata),len(blob))+metadata+blob
    return body, obj


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input-root",type=Path,default=BASE/"source/actor97")
    ap.add_argument("--output",type=Path,default=BASE/"models/ellie_preview.ydd")
    ap.add_argument("--report",type=Path,default=BASE/"source/native-ellie-mesh-preview-report.json")
    ap.add_argument("--max-lod",type=int,default=0)
    ap.add_argument("--outfit",choices=("basic","complete"),default=None,
                    help="Complete original PC outfit is default for canonical ellie.ydd; previews default basic")
    ap.add_argument("--force",action="store_true")
    a=ap.parse_args()
    try:
        if not 0<=a.max_lod<=3:raise PakError("max-lod must be 0..3")
        meshes=[]
        sources=[]
        outfit=a.outfit or ("complete" if a.output.name=="ellie.ydd" else "basic")
        selected=COMPLETE_OUTFIT_PAKS if outfit=="complete" else DEFAULT_PAKS
        for name in selected:
            pak=NaughtyDogPak(a.input_root/name)
            parts=read_meshes(pak,a.max_lod)
            meshes.extend(parts)
            sources.append({"file":name,"submeshes":len(parts),"pak_sha256":pak.summary()["sha256"]})
        # Every referenced slot must exist before replacing the production YDD.
        import xml.etree.ElementTree as ET
        material_file=BASE/"materials/ellie.ymat"
        packed_ymat=material_file.read_bytes()
        if packed_ymat[:4]!=b"NEF8":
            raise PakError("Missing native Ellie YMAT")
        source_xml=zlib.decompress(
            packed_ymat[128:128+struct.unpack_from("<Q",packed_ymat,24)[0]],-15)
        known={node.attrib["name"] for node in ET.fromstring(source_xml).findall("Material")}
        if missing:=(set(m["material_name"] for m in meshes)-known):
            raise PakError("Outfit refers to missing original YMAT entries: "+repr(sorted(missing)))
        body, manifest=build_nvrm(meshes,a.output.stem)
        packed=pack_nef8_v2(body,"characters/ellie/models/"+a.output.name)
        if a.output.exists() and not a.force:
            raise FileExistsError("Output exists, refusing overwrite (use --force)")
        a.output.parent.mkdir(parents=True,exist_ok=True)
        temp=a.output.with_suffix(".ydd.tmp")
        temp.write_bytes(packed)
        temp.replace(a.output)
        report={
            "status":"real_source_geometry_complete_outfit_static_skinning_pending",
            "assembly_preset":outfit,
            "missing_attachments":[],
            "original_cloth_physics":False,
            "original_helper_bind_poses_complete":False,
            "source_game":"The Last of Us Part II Remastered PC",
            "source_files":sources,
            "source_submeshes":len(meshes),
            "source_vertices":sum(len(m["position"]) for m in meshes),
            "source_triangles":sum(len(m["indices"])//3 for m in meshes),
            "bounds":manifest["bounds"],
            "native_ydd":str(a.output),
            "native_ydd_bytes":len(packed),
            "skin_data_preserved":all(m["skin"] is not None for m in meshes),
            "runtime_skinning_enabled":False,
            "source_materials_preserved":False,
            "normal_encoding":"source_type2_snorm8xyz_or_area_weighted_geometry_fallback_for_type130",
            "submeshes":[{"source_pak":m["source"],"name":m["name"],
                          "verts":len(m["position"]),"indices":len(m["indices"])} for m in meshes],
        }
        a.report.parent.mkdir(parents=True,exist_ok=True)
        a.report.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n",encoding="utf8")
        print(json.dumps(report,indent=2,ensure_ascii=False))
        return 0
    except (OSError,ValueError,struct.error,KeyError) as exc:
        print(f"NATIVE MESH PREVIEW ERROR: {exc}",file=sys.stderr)
        return 2


if __name__=="__main__":
    raise SystemExit(main())
