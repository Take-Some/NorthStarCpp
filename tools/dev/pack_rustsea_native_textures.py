#!/usr/bin/env python3
"""Pack Rust Sea CC0 JPEG PBR maps into native NEF8 YTD/NETD texture dictionaries.

Source JPEGs are intermediates; project materials use only YTD @entry.
Requires first-party configured ImageMagick command on the development PC.
"""
from __future__ import annotations
import hashlib
import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path
from blake3 import blake3

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.dev.prepare_rust_sea_assets import DEST as ASSETS,TEXTURES,build_xml,atomic
from tools.pack_native_ymat import build_ymat

MAGICK=Path(r"C:\Users\Kayla\Documents\NorthStar\Tools\toolbelt\third_party\imagemagick\bin\magick.cmd")
WIDTH=HEIGHT=512

def rgba_image(src:Path):
    if not MAGICK.is_file():raise FileNotFoundError("ImageMagick toolbelt missing: "+str(MAGICK))
    command=["cmd.exe","/d","/c",str(MAGICK),str(src),
             "-resize",str(WIDTH)+"x"+str(HEIGHT)+"!", "-depth","8", "rgba:-"]
    proc=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=35)
    if proc.returncode:
        raise RuntimeError("ImageMagick conversion failed: "+proc.stderr.decode("utf8","replace")[:200])
    if len(proc.stdout)!=WIDTH*HEIGHT*4:raise ValueError("Unexpected RGBA byte count "+str(len(proc.stdout)))
    return proc.stdout

def nef8(body:bytes,count:int):
    compressor=zlib.compressobj(level=9,wbits=-15)
    comp=compressor.compress(body)+compressor.flush()
    hdr=bytearray(128)
    hdr[:4]=b"NEF8"
    struct.pack_into("<6H",hdr,4,1,128,1,1,1,0)  # schema=1; count lives at +40
    struct.pack_into("<6Q",hdr,16,128,len(comp),len(body),count,0,0)
    hdr[64:96]=blake3(body).digest()
    struct.pack_into("<Q",hdr,112,1)
    return bytes(hdr)+comp

def dust_rgba():
    # Authored translucent dirt plume; RGBA is inserted directly into NEF8
    # rather than placing loose PNG/JPG data into project runtime assets.
    import math
    data=bytearray(WIDTH*HEIGHT*4)
    for j in range(HEIGHT):
        v=(j+0.5)/HEIGHT*2.-1.
        for i in range(WIDTH):
            u=(i+0.5)/WIDTH*2.-1.
            r=math.sqrt(u*u+v*v)
            turb=0.78+0.11*math.sin(u*17.+v*13.)+0.11*math.cos(u*31.-v*21.)
            a=max(0.,min(1.,max(0.,1.-r/max(.4,turb))**1.6))
            off=(j*WIDTH+i)*4
            data[off:off+4]=bytes((139,115,91,round(125*a)))
    return bytes(data)

def ytd(name,types):
    surfaces=[(kind,dust_rgba() if name=="dust_cloud" else
               rgba_image(ASSETS/"textures/rustsea"/f"{name}_{kind}.jpg")) for kind in types]
    count=len(surfaces)
    name_bytes=b"".join(k.encode("ascii") for k,_ in surfaces)
    entries_off=64
    mips_off=entries_off+count*64
    names_off=mips_off+count*32
    index_bytes=names_off+len(name_bytes)
    payload_start=64+index_bytes
    joined=b"".join(p for _,p in surfaces)
    body=bytearray(payload_start+len(joined))
    body[:4]=b"NETD"
    struct.pack_into("<IIIQQQQ",body,4,2,64,count,64,index_bytes,payload_start,len(joined))
    index=64
    body[index:index+4]=b"NTDX"
    struct.pack_into("<HHIIIIII",body,index+4,1,64,32,count,count,
                     entries_off,mips_off,names_off)
    struct.pack_into("<I",body,index+32,len(name_bytes))
    data_offset=name_offset=0
    for i,(kind,pixels) in enumerate(surfaces):
        e=index+entries_off+i*64
        # 1 = RGBA8, color_space 2 = sRGB for albedo and 1 = linear for all other maps
        color_space=2 if kind=="diff" else 1
        struct.pack_into("<QQQIHHIIIII",body,e,0,data_offset,len(pixels),
                         name_offset,len(kind),1,WIDTH,HEIGHT,i,1,color_space)
        m=index+mips_off+i*32
        struct.pack_into("<QQIIII",body,m,data_offset,len(pixels),WIDTH,HEIGHT,0,0)
        data_offset+=len(pixels)
        name_offset+=len(kind)
    body[index+names_off:index+names_off+len(name_bytes)]=name_bytes
    body[payload_start:]=joined
    return nef8(bytes(body),count)

def validate(p:Path,count:int):
    raw=p.read_bytes()
    if len(raw)>64*1024*1024 or raw[:4]!=b"NEF8":raise ValueError("Invalid YTD envelope")
    header=struct.unpack_from("<6H",raw,4)
    if header!=(1,128,1,1,1,0):raise ValueError("YTD header mismatch")
    off,clen,plain,entries,meta,ml=struct.unpack_from("<6Q",raw,16)
    body=zlib.decompress(raw[off:off+clen],-15)
    if len(body)!=plain or blake3(body).digest()!=raw[64:96]:raise ValueError("YTD body digest mismatch")
    if body[:4]!=b"NETD" or struct.unpack_from("<I",body,12)[0]!=count:raise ValueError("NETD mismatch")
    return hashlib.sha256(raw).hexdigest()

def main():
    report={}
    for name,types in {**TEXTURES,"dust_cloud":("diff",)}.items():
        out=ASSETS/"textures/rustsea"/(name+".ytd")
        atomic(out,ytd(name,types))
        report[name]={"textures":list(types),"sha256":validate(out,len(types)),
                      "byte_size":out.stat().st_size}
    xml=build_xml()
    atomic(ASSETS/"materials/rustsea.ymat.xml",xml)
    atomic(ASSETS/"materials/rustsea.ymat",build_ymat(xml))
    report_path=ASSETS/"rustsea_native_textures.json"
    atomic(report_path,(json.dumps({"format":"NEF8 YTD NETD","texture_sets":report},indent=2)+chr(10)).encode("utf8"))
    print(json.dumps({"ok":True,"native_ytd":len(report),"textures":sum(len(x["textures"]) for x in report.values()),"bytes":sum(x["byte_size"] for x in report.values())}))
if __name__=="__main__":
    main()
