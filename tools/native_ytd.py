#!/usr/bin/env python3
"""Repack original TLOU2 PC D3D12 footprints to native NorthStar NEF8/NETD YTD.

Original BCn compressed blocks preserved; remove D3D12 256-byte row padding
and 512-byte mip alignment. Only PAK-local low-resolution texture variants.
"""
from __future__ import annotations
import argparse, hashlib, json, struct, sys, zlib
from pathlib import Path
from blake3 import blake3
from native_mesh_preview import BASE

# DXGI -> NETD codec / colorspace / source blockbytes / compressed.
FMT={28:(2,1,4,False),71:(101,1,8,True),72:(101,2,8,True),
     77:(103,1,16,True),80:(102,1,8,True),83:(105,1,16,True),
     98:(104,1,16,True)}
def align(x,n):return (x+n-1)//n*n

def unpad(data,w,h,count,dxgi):
    if dxgi not in FMT:raise ValueError(f"Unsupported DXGI format {dxgi}")
    fmt,cs,bpp,compressed=FMT[dxgi]
    if not(1<=count<=15 and 0<w<=16384 and 0<h<=16384):
        raise ValueError("Bad original texture dimensions or mips")
    cursor=0;out=[]
    for level in range(count):
        cursor=align(cursor,512)
        row=(w+3)//4*bpp if compressed else w*bpp
        rows=(h+3)//4 if compressed else h
        pitch=align(row,256)
        if cursor+rows*pitch>len(data):raise ValueError("Source mip extent overruns GPU blob")
        tex=b"".join(data[cursor+r*pitch:cursor+r*pitch+row] for r in range(rows))
        out.append((w,h,tex))
        cursor+=rows*pitch
        w=max(1,w//2);h=max(1,h//2)
    if align(cursor,512)!=len(data):
        raise ValueError(f"Footprint mismatch {align(cursor,512)} vs {len(data)}")
    return out

def load_sources(manifest):
    records=json.loads(manifest.read_text("utf8"))
    unique={};dupes=0
    for source in records["sources"]:
        data=Path(source["vram_blob"]).read_bytes()
        if len(data)!=source["total_bytes"]:raise ValueError("Source VRAM bytes changed")
        for item in source["entries"]:
            off=item["blob_offset"];chunk=data[off:off+item["blob_bytes"]]
            if hashlib.sha256(chunk).hexdigest()!=item["sha256"]:
                raise ValueError("Source GPU SHA256 mismatch")
            key=item["hash64"]
            if key in unique:
                old=unique[key]
                if old["sha256"]!=item["sha256"] or old["dxgi"]!=item["dxgi"]:
                    raise ValueError("Conflicting GPU hash")
                dupes+=1;continue
            unique[key]={
                "name":"tex_"+key,"hash64":key,
                "source_pak":source["pak"],"sha256":item["sha256"],
                "dxgi":item["dxgi"],"width":item["width"],"height":item["height"],
                "mips":unpad(chunk,item["width"],item["height"],item["mips"],item["dxgi"])}
    return list(unique.values()),dupes

def assign_source_color_spaces(textures, root):
    """Classify UNORM color vs data from exact source material slot bindings.

    A BC1/BC7 texture carrying tangent normals or masks must remain linear,
    even when other textures of the same DXGI format are used for albedo.
    Explicit DXGI BC1 SRGB is respected unconditionally.
    """
    from native_pak import NaughtyDogPak
    usage={}
    seen_materials=set()
    for filename in sorted(root.glob("ellie-*.pak")):
        pak=NaughtyDogPak(filename)
        geo=pak.typed("GEOMETRY_1")
        if not geo:
            continue
        start=pak.pointer(geo[0]["offset"]+88)
        for i in range(pak.u32(geo[0]["offset"]+56)):
            mat=pak.pointer(start+i*192+72)
            if (filename.name,mat) in seen_materials:
                continue
            seen_materials.add((filename.name,mat))
            table=pak.pointer(mat+32)
            for j in range(pak.u32(mat+276)):
                row=table+j*48
                slot=pak.ascii(pak.pointer(row)).lower()
                ref=pak.pointer(row+24)
                h=f"{pak.u64(ref+8):016x}"
                if any(k in slot for k in ("basecolor","albedo","diffuse","emissive","burncolor")):
                    purpose="color"
                elif any(k in slot for k in ("normal","mask","ao0","occlusion",
                                               "rough","metal","spec","curvature","blend")):
                    purpose="data"
                else:
                    purpose="unknown"
                usage.setdefault(h,set()).add(purpose)
    counters={"explicit_srgb":0,"inferred_srgb":0,"inferred_linear":0,
              "ambiguous_linear":0}
    for texture in textures:
        dxgi=texture["dxgi"]
        types=usage.get(texture["hash64"],set())
        if dxgi==72:
            colorspace=2
            key="explicit_srgb"
        elif dxgi in (71,77,98):
            if "color" in types and "data" not in types:
                colorspace=2
                key="inferred_srgb"
            elif "color" in types and "data" in types:
                colorspace=1
                key="ambiguous_linear"
            else:
                colorspace=1
                key="inferred_linear"
        else:
            colorspace=1
            key="inferred_linear"
        texture["colorspace"]=colorspace
        texture["material_purposes"]=sorted(types)
        counters[key]+=1
    return counters


def encode_netd(textures):
    index=bytearray(40);entries=bytearray();mips=bytearray()
    names=bytearray();pixel=bytearray();next_mip=0
    for t in textures:
        name=t["name"].encode("ascii")
        name_offset=len(names);names+=name+b"\x00"
        tex_start=len(pixel)
        fmt,default_cs,bpp,compressed=FMT[t["dxgi"]]
        cs=t.get("colorspace",default_cs)
        for lv,(w,h,tex) in enumerate(t["mips"]):
            mips+=struct.pack("<QQIIII",len(pixel),len(tex),w,h,lv,0)
            pixel+=tex
        e=bytearray(64)
        struct.pack_into("<QQQIHHIIIII",e,0,int(t["hash64"],16),
                         tex_start,len(pixel)-tex_start,name_offset,len(name),
                         fmt,t["width"],t["height"],next_mip,len(t["mips"]),cs)
        entries+=e;next_mip+=len(t["mips"])
    entry_pos=40;mip_pos=entry_pos+len(entries);names_pos=mip_pos+len(mips)
    struct.pack_into("<4sHHIIIIII",index,0,b"NTDX",1,64,32,
                     len(textures),next_mip,entry_pos,mip_pos,names_pos)
    struct.pack_into("<I",index,32,len(names))
    index+=entries+mips+names
    payload=align(64+len(index),16)
    netd=bytearray(64)
    struct.pack_into("<4sIIIQQQQ",netd,0,b"NETD",2,64,len(textures),
                     64,len(index),payload,len(pixel))
    netd+=index+bytes(payload-len(netd)-len(index))+pixel
    if len(netd)!=payload+len(pixel):raise ValueError("NETD layout mismatch")
    return bytes(netd)

def encode_nef8(body,count,logical_path="characters/ellie/textures/ellie.ytd"):
    zipstream=zlib.compressobj(9,wbits=-15)
    zipped=zipstream.compress(body)+zipstream.flush()
    header=bytearray(64);header[:4]=b"NEF8"
    struct.pack_into("<BBHHHIQQ",header,4,2,6,1,7,1,count,len(zipped),len(body))
    header[32:64]=blake3(body).digest()
    meta=json.dumps({
       "schema":"newengine.asset.list_file.header_metadata",
       "logical_path":logical_path,
       "content_kind":"texture_dictionary",
       "source":"TLOU2 Remastered PC/actor97 PAK native D3D12 VRAM",
       "warnings":["Only source PAK-local low-resolution mips",
                   "Color space for BC1/BC3/BC7 UNORM is inferred"],
    },separators=(",",":")).encode("utf8")
    return bytes(header)+meta+zipped

def validate_ytd(enc,count):
    if enc[:4]!=b"NEF8" or struct.unpack_from("<H",enc,6)[0]!=1:
        raise ValueError("Bad NEF8 kind")
    compressed=struct.unpack_from("<Q",enc,16)[0]
    body=zlib.decompress(enc[-compressed:],-15)
    if len(body)!=struct.unpack_from("<Q",enc,24)[0] or blake3(body).digest()!=enc[32:64]:
        raise ValueError("NEF8 checksum mismatch")
    if body[:4]!=b"NETD":raise ValueError("NETD magic missing")
    table,table_size,payload,psize=struct.unpack_from("<4Q",body,16)
    if struct.unpack_from("<I",body,12)[0]!=count or payload+psize!=len(body):
        raise ValueError("NETD header and length inconsistent")
    if body[table:table+4]!=b"NTDX":raise ValueError("No NTDX index")
    ver,header,esize,n,mc,entry,mip,names,nbytes=struct.unpack_from(
        "<HHIIIIIII",body,table+4)
    if (ver,header,esize,n,entry,mip,names)!=(1,64,32,count,40,40+64*count,40+64*count+32*mc):
        raise ValueError("NTDX header inconsistent")
    if names+nbytes>table_size:raise ValueError("NTDX names extent invalid")
    next_mip=0;end=0
    for k in range(count):
        e=table+entry+k*64
        h,start,size,no,nlen,fmt,w,ht,mi,mnum,cs=struct.unpack_from(
            "<QQQIHHIIIII",body,e)
        if start!=end or no+nlen>nbytes or not(1<=mnum<=15) or mi!=next_mip:
            raise ValueError("NETD texture table invalid")
        if fmt not in (1,2,101,102,103,104,105) or cs not in (1,2):
            raise ValueError("Unsupported NETD codec")
        for j in range(mnum):
            mo=table+mip+32*(mi+j)
            off,sz,mw,mh,lv,zero=struct.unpack_from("<QQIIII",body,mo)
            row=mw*4 if fmt in (1,2) else (mw+3)//4*(8 if fmt in (101,102) else 16)
            rows=mh if fmt in (1,2) else (mh+3)//4
            if off!=end or sz!=row*rows or lv!=j or zero or mw!=w or mh!=ht:
                raise ValueError("NETD mip table invalid")
            end+=sz;w=max(1,w//2);ht=max(1,ht//2)
        if end!=start+size:raise ValueError("NETD texture payload extent invalid")
        next_mip+=mnum
    if next_mip!=mc or end!=psize:raise ValueError("Unclaimed NETD payload data")
    return {"textures":count,"mip_levels":mc,"pixel_bytes":psize,"nef8_bytes":len(enc)}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest",type=Path,default=BASE/"source/native_vram/vram.manifest.json")
    p.add_argument("--output",type=Path,default=BASE/"textures/ellie.ytd")
    p.add_argument("--report",type=Path,default=BASE/"source/native-ellie-textures-report.json")
    p.add_argument("--force",action="store_true")
    p.add_argument("--allow-downgrade",action="store_true",
                   help="Explicitly permit overwriting a larger fullres YTD with local low-res mips")
    a=p.parse_args()
    try:
        tex,duplicates=load_sources(a.manifest)
        gamma_stats=assign_source_color_spaces(tex,BASE/"source/actor97")
        content=encode_nef8(encode_netd(tex),len(tex))
        validation=validate_ytd(content,len(tex))
        if a.output.exists() and not a.force:
            raise FileExistsError("YTD exists: pass --force for overwrite")
        if (a.output.exists() and a.force and not a.allow_downgrade and
            a.output.stat().st_size>len(content)*4):
            raise ValueError("Refusing to downgrade fullres source dictionary to PAK-local mip YTD; use native_fullres_ellie.py, or explicitly --allow-downgrade")
        a.output.parent.mkdir(parents=True,exist_ok=True)
        tmp=a.output.with_suffix(".ytd.tmp");tmp.write_bytes(content);tmp.replace(a.output)
        report={
            "status":"native_YTD_original_D3D12_pixel_blocks",
            "source_variants":"PAK-local low-resolution, not full high-resolution texturedict",
            "gamma_assumption":"UNORM source color spaces inferred per original material slot; normal/mask/etc linear, explicit SRGB preserved",
            "color_space_classification":gamma_stats,
            "ytd":str(a.output),"sha256":hashlib.sha256(content).hexdigest(),
            "deduplicated_source_entries":duplicates,
            "validation":validation,
            "texture_manifest":[{k:v for k,v in t.items() if k!="mips"} for t in tex],
        }
        a.report.parent.mkdir(parents=True,exist_ok=True)
        a.report.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n",encoding="utf8")
        print(json.dumps({"file":str(a.output),"duplicate_records":duplicates,
                          "color_space_classification":gamma_stats,
                          **validation},indent=2,ensure_ascii=False))
        return 0
    except (OSError,ValueError,KeyError,zlib.error,struct.error) as error:
        print("NATIVE YTD FAILED:",error,file=sys.stderr)
        return 2

if __name__=="__main__":
    raise SystemExit(main())
