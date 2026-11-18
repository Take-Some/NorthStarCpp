#!/usr/bin/env python3
"""Convert The Rust Sea's approved CC0 source OBJ geometry into native NEF8 YDR.

Source OBJ files are build intermediates only. All scene references use .ydr.
The binary is the exact YDR type-5/part-52 layout parsed in
engine/northstarcpp/modules/host/shared_asset_models.cpp.
"""
from __future__ import annotations

import hashlib
import math
import struct
import zlib
from pathlib import Path
from blake3 import blake3

ROOT=Path(__file__).resolve().parents[2]
ASSETS=ROOT/"tools/templates/moving_cities/assets"
SOURCE=ASSETS/"models/industrial"
DEST=ASSETS/"models/rustsea"

def pack_nef8(raw:bytes,kind:int=2,entries:int=1)->bytes:
    zipstream=zlib.compressobj(level=9,wbits=-15)
    packed=zipstream.compress(raw)+zipstream.flush()
    header=bytearray(128)
    header[:4]=b"NEF8"
    struct.pack_into("<6H",header,4,1,128,kind,1,1,0)
    struct.pack_into("<6Q",header,16,128,len(packed),len(raw),entries,0,0)
    header[64:96]=blake3(raw).digest()
    struct.pack_into("<Q",header,112,1)
    return bytes(header)+packed

def obj_mesh(raw:str):
    positions,uvs,normals=[],[],[]
    verts,inds,lookup=[],[],{}
    for line in raw.splitlines():
        parts=line.split()
        if not parts:continue
        if parts[0]=="v":
            positions.append(tuple(map(float,parts[1:4])))
        elif parts[0]=="vt":
            uvs.append(tuple(map(float,parts[1:3])))
        elif parts[0]=="vn":
            normals.append(tuple(map(float,parts[1:4])))
        elif parts[0]=="f":
            face=[]
            for token in parts[1:]:
                fields=token.split("/")
                key=tuple((int(fields[i]) if i<len(fields) and fields[i] else 0)
                          for i in range(3))
                if key not in lookup:
                    vi,ti,ni=key
                    if vi<=0:raise ValueError("Source OBJ must use positive vertex indices")
                    v=positions[vi-1]
                    t=uvs[ti-1] if ti else (v[0]*0.5,v[2]*0.5)
                    n=normals[ni-1] if ni else (0.,1.,0.)
                    if len(v)!=3 or len(t)!=2 or len(n)!=3 or not all(map(math.isfinite,(*v,*t,*n))):
                        raise ValueError("Invalid OBJ vertex")
                    lookup[key]=len(verts)
                    verts.append((*v,*n,*t))
                face.append(lookup[key])
            for i in range(1,len(face)-1):
                inds.extend((face[0],face[i],face[i+1]))
    if not verts or not inds or len(inds)>750000:raise ValueError("Empty/oversized OBJ")
    return verts,inds

def tile():
    # Local top y=1: preserve preexisting 28 m cube collider at center y=-28.
    # Local x/z +-0.5: visual 28 m tiles, matching 28 m sector spacing.
    # Correct top face CCW from +Y; 3x UV tiling per 28 m segment.
    s=3.
    verts=[
        (-.5,1.,-.5,0.,1.,0.,0.,0.),
        (-.5,1., .5,0.,1.,0.,0.,s),
        ( .5,1., .5,0.,1.,0.,s,s),
        ( .5,1.,-.5,0.,1.,0.,s,0.),
    ]
    return verts,[0,1,2,0,2,3]

def deck():
    # Local deck top at y=1: 6m per cell, mesh uses only a thin top.
    s=1.
    return [
        (-.5,1.,-.5,0.,1.,0.,0.,0.),
        (-.5,1., .5,0.,1.,0.,0.,s),
        ( .5,1., .5,0.,1.,0.,s,s),
        ( .5,1.,-.5,0.,1.,0.,s,0.),
    ],[0,1,2,0,2,3]

def native_ydr(drawable_name,part_name,material,verts,indices):
    if not verts or len(verts)>200000 or not indices or len(indices)%3 or len(indices)>600000:
        raise ValueError("Outside native YDR part limit")
    if not all(0<=i<len(verts) for i in indices):raise ValueError("Invalid triangle index")
    names=(drawable_name+"\0"+part_name+"\0"+material+"\0").encode("ascii")
    part_offset=len(drawable_name)+1
    material_offset=part_offset+len(part_name)+1
    geo_offset=120+len(names)
    header=bytearray(120)
    struct.pack_into("<III",header,0,5,1,40)
    struct.pack_into("<QQQ",header,16,120,len(names),geo_offset)
    bounds_min=[min(v[i] for v in verts) for i in range(3)]
    bounds_max=[max(v[i] for v in verts) for i in range(3)]
    struct.pack_into("<3f",header,76,*bounds_min)
    struct.pack_into("<3f",header,88,*bounds_max)
    geo=bytearray(72)
    struct.pack_into("<II",geo,0,1,1)
    part=bytearray(52)
    struct.pack_into("<IIII",part,0,part_offset,material_offset,len(verts),len(indices))
    struct.pack_into("<II",part,40,1,48)
    payload=(b"".join(struct.pack("<8f",*v) for v in verts)+
             bytes(48*len(verts)))  # 32-byte vertices THEN separate 48-byte aux stream
    ix=struct.pack("<"+str(len(indices))+"I",*indices)
    return pack_nef8(bytes(header)+names+bytes(geo)+bytes(part)+payload+ix)

def verify(path:Path, expected_material:str):
    src=path.read_bytes()
    if src[:4]!=b"NEF8":raise ValueError("Expected NEF8 "+path.name)
    if struct.unpack_from("<6H",src,4)!=(1,128,2,1,1,0):raise ValueError("Bad envelope header")
    offset,compressed,unpacked,entries,mo,ml=struct.unpack_from("<6Q",src,16)
    if (offset,entries,mo,ml)!=(128,1,0,0) or offset+compressed!=len(src):raise ValueError("Bad offsets")
    raw=zlib.decompress(src[offset:],-15)
    if len(raw)!=unpacked or blake3(raw).digest()!=src[64:96]:raise ValueError("Bad checksum")
    if struct.unpack_from("<III",raw,0)!=(5,1,40):raise ValueError("Bad YDR header")
    string_offset,string_size,geometry=struct.unpack_from("<3Q",raw,16)
    if string_offset!=120 or geometry!=120+string_size:raise ValueError("Bad string table")
    if struct.unpack_from("<II",raw,geometry)!=(1,1):raise ValueError("Bad part table")
    off=geometry+72
    entry_off,material_off,nv,ni=struct.unpack_from("<4I",raw,off)
    if struct.unpack_from("<II",raw,off+40)!=(1,48):raise ValueError("Bad vertex format")
    if len(raw)!=off+52+nv*80+ni*4:raise ValueError("Bad buffer sizes")
    # Important: C++ parser reads 32*vertex_count consecutive render
    # vertices and only then the auxiliary 48*vertex_count stream.
    # Interleaving both streams once produced massive triangle corruption.
    start=off+52
    aux=raw[start+nv*32:start+nv*80]
    if any(aux):raise ValueError("YDR auxiliary stream interleaved with render vertices")
    for index in range(nv):
        values=struct.unpack_from("<8f",raw,start+index*32)
        if not all(math.isfinite(value) for value in values):
            raise ValueError("Non-finite YDR vertex attributes")
    first_index=start+nv*80
    for index in range(ni):
        if struct.unpack_from("<I",raw,first_index+index*4)[0]>=nv:
            raise ValueError("Out-of-bounds YDR triangle index")
    names=raw[string_offset:string_offset+string_size]
    name=names[material_off:names.find(b"\0",material_off)].decode("ascii")
    if name!=expected_material:raise ValueError("Incorrect native material reference "+name)
    return {"vertices":nv,"triangles":ni//3,"sha256":hashlib.sha256(src).hexdigest()}

def main():
    DEST.mkdir(parents=True,exist_ok=True)
    manifest={}
    for src in sorted(SOURCE.glob("*.obj")):
        model=src.stem
        verts,indices=obj_mesh(src.read_text("utf8"))
        mat="steel_grate" if "catwalk" in model else "steel_corroded"
        ref="materials/rustsea.ymat@"+mat
        output=DEST/(model+".ydr")
        output.write_bytes(native_ydr(model,"main",ref,verts,indices))
        manifest[output.name]=verify(output,ref)
    for name,fn,mat in [
        ("wasteland_dry",tile,"ground_dry"),
        ("wasteland_gravel",tile,"ground_gravel"),
        ("bastion_deck_grate",deck,"steel_grate"),
    ]:
        verts,indices=fn()
        ref="materials/rustsea.ymat@"+mat
        out=DEST/(name+".ydr")
        out.write_bytes(native_ydr(name,"main",ref,verts,indices))
        manifest[out.name]=verify(out,ref)
    report=ASSETS/"rustsea_native_models.json"
    import json
    report.write_text(json.dumps({"format":"NEF8 YDR binary v1","models":manifest},indent=2)+"\n",encoding="utf8")
    print(json.dumps({"ok":True,"native_ydr":len(manifest),
                      "triangles":sum(x["triangles"] for x in manifest.values())}))
if __name__=="__main__":
    main()
