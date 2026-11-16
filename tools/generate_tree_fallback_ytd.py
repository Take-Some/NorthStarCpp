"""Create a reproducible native NEF8/NETD placeholder for missing tree assets.

Fallback artwork, not an extraction of original tree textures. Never overwrites
an existing asset with different bytes.
"""
from __future__ import annotations
from pathlib import Path
import hashlib
import math
import random
import struct
import zlib
from blake3 import blake3

ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/"sharedAssets/prefabs/tree_animate/textures/tree_animate.ytd"
W=H=64
NAMES=(
    "bark_diffuse","bark_normal",
    "leaf_diffuse","leaf_normal",
    "branch_diffuse","branch_normal",
)
def clamp(v):return max(0,min(255,int(v)))
def surface(name):
    rng=random.Random(0x20261116 + NAMES.index(name)*111)
    normal=name.endswith("_normal")
    out=bytearray()
    for y in range(H):
        for x in range(W):
            if normal:
                # Stable neutral tangent-space normals for the placeholder.
                pixel=(128,128,255,255)
            elif name.startswith("leaf_"):
                u=(x%16)-7.5
                v=(y%16)-7.5
                # Four tiled leaf clusters; transparent outside each leaf.
                inside=(u/6.5)**2+((v+u*.28)/7.0)**2<1
                grain=rng.randrange(-12,13)
                shade=int(8*math.sin(x*.27+y*.37))
                pixel=(clamp(39+grain),clamp(115+grain+shade),
                       clamp(47+grain//2),255 if inside else 0)
            else:
                grain=rng.randrange(-12,13)
                ridge=int(24*math.sin(x*.52+3*math.sin(y*.19)))
                if name.startswith("branch_"):
                    pixel=(clamp(82+grain+ridge),clamp(58+grain+ridge//2),
                           clamp(40+grain),255)
                else:
                    pixel=(clamp(103+grain+ridge),clamp(72+grain+ridge//2),
                           clamp(50+grain//2),255)
            out.extend(pixel)
    return bytes(out)

def build():
    count=len(NAMES)
    textures=[surface(n) for n in NAMES]
    name_bytes=b"".join(n.encode("ascii") for n in NAMES)
    index_offset=64
    entries_off=64
    mips_off=entries_off+count*64
    names_off=mips_off+count*32
    table_size=names_off+len(name_bytes)
    payload_start=index_offset+table_size
    tex_bytes=b"".join(textures)
    body=bytearray(payload_start+len(tex_bytes))
    body[:4]=b"NETD"
    struct.pack_into("<IIIQQQQ",body,4,2,64,count,
                     index_offset,table_size,payload_start,len(tex_bytes))
    table=index_offset
    body[table:table+4]=b"NTDX"
    struct.pack_into("<HHIIIIII",body,table+4,1,64,32,count,
                     count,entries_off,mips_off,names_off)
    struct.pack_into("<I",body,table+32,len(name_bytes))
    offset=0
    name_offset=0
    for i,(name,pixels) in enumerate(zip(NAMES,textures)):
        entry=table+entries_off+i*64
        encoded=name.encode("ascii")
        struct.pack_into("<QQQIHHIIIII",body,entry,0,
                         offset,len(pixels),name_offset,len(encoded),
                         1,W,H,i,1,1 if name.endswith("_normal") else 2)
        mip=table+mips_off+i*32
        struct.pack_into("<QQIIII",body,mip,offset,len(pixels),W,H,0,0)
        offset+=len(pixels)
        name_offset+=len(encoded)
    body[table+names_off:table+names_off+len(name_bytes)]=name_bytes
    body[payload_start:]=tex_bytes
    xml=bytes(body)
    compressor=zlib.compressobj(level=9,wbits=-zlib.MAX_WBITS)
    packed=compressor.compress(xml)+compressor.flush()
    header=bytearray(128)
    header[:4]=b"NEF8"
    struct.pack_into("<HHHHHH",header,4,1,128,1,1,count,0)
    struct.pack_into("<6Q",header,16,128,len(packed),len(xml),
                     count,0,0)
    header[64:96]=blake3(xml).digest()
    struct.pack_into("<Q",header,112,1)
    return bytes(header)+packed

if __name__=="__main__":
    data=build()
    DEST.parent.mkdir(parents=True,exist_ok=True)
    if DEST.exists() and DEST.read_bytes()!=data:
        raise SystemExit(f"Refusing to overwrite existing YTD: {DEST}")
    DEST.write_bytes(data)
    unpacked=zlib.decompress(data[128:],-zlib.MAX_WBITS)
    assert unpacked[:4]==b"NETD"
    assert blake3(unpacked).digest()==data[64:96]
    assert len(unpacked)>len(NAMES)*W*H*4
    print(f"Generated fallback texture dictionary: {DEST}")
    print(f"textures={len(NAMES)} size={len(data)} SHA256={hashlib.sha256(data).hexdigest()}")
