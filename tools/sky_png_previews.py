#!/usr/bin/env python3
"""Render extracted original TLOU2 sky/cloud DDS into browseable PNG previews.

All preview pixels derive from original compressed GPU blocks via vendored
bcdec. BC6H HDR is Reinhard-tonemapped to LDR *only for PNG previews*.
The original DDS/DX10 files remain lossless.
"""
from __future__ import annotations
import argparse,ctypes,hashlib,json,re,struct,sys,zlib
from pathlib import Path
from native_sky import OUTPUT,PROJECT,FMT

def make_png(w,h,rgba):
    if len(rgba)!=w*h*4:raise ValueError("RGBA PNG payload length mismatch")
    signature=b"\x89PNG\r\n\x1a\n"
    def chunk(kind,data):
        return (struct.pack(">I",len(data))+kind+data+
            struct.pack(">I",zlib.crc32(kind+data)&0xffffffff))
    scan=b"".join(b"\x00"+rgba[y*w*4:(y+1)*w*4] for y in range(h))
    ihdr=struct.pack(">IIBBBBB",w,h,8,6,0,0,0)
    return signature+chunk(b"IHDR",ihdr)+chunk(b"IDAT",zlib.compress(scan,9))+chunk(b"IEND",b"")

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest",type=Path,default=OUTPUT/"source/sky.manifest.json")
    ap.add_argument("--bridge",type=Path,default=PROJECT/"out/sky-tools/bcn_preview.dll")
    ap.add_argument("--force",action="store_true")
    a=ap.parse_args()
    try:
        library=ctypes.CDLL(str(a.bridge))
        decode=library.sky_decode_rgba8
        decode.argtypes=[
            ctypes.POINTER(ctypes.c_uint8),ctypes.c_uint32,ctypes.c_uint32,
            ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint8),ctypes.c_uint64]
        decode.restype=ctypes.c_int
        doc=json.loads(a.manifest.read_text("utf8"))
        count={"sky":0,"clouds":0};issues=[]
        for tex in doc["original_textures"]:
            roles=[role for role in tex["roles"] if role in count]
            if not roles:continue
            source=Path(tex["lossless_dds"]).read_bytes()
            if hashlib.sha256(source).hexdigest()!=tex["dds_sha256"]:
                raise ValueError("Original DDS hash changed")
            dxgi,w,h=tex["original_dxgi"],tex["width"],tex["height"]
            if source[:4]!=b"DDS " or struct.unpack_from("<I",source,128)[0]!=dxgi:
                raise ValueError("Source DDS/DX10 header invalid")
            if dxgi==28:
                pixel=source[148:148+w*h*4]
            else:
                size=((w+3)//4)*((h+3)//4)*FMT[dxgi]
                data=source[148:148+size]
                encoded=(ctypes.c_uint8*len(data)).from_buffer_copy(data)
                decoded=(ctypes.c_uint8*(w*h*4))()
                code=decode(encoded,w,h,dxgi,decoded,len(data))
                if code:
                    raise ValueError(f"GPU image preview decode failed {tex['hash64']} ({dxgi}) => {code}")
                pixel=bytes(decoded)
            png=make_png(w,h,pixel)
            names=tex.get("original_shader_paths",[])
            natural=names[0].replace("\\","/").split("/")[-2] if names and len(names[0].split("/"))>1 else "skytexture"
            natural=re.sub(r"[^a-z0-9_-]+","_",natural.lower()).strip("_")[:58]
            for role in roles:
                target=a.manifest.parent/"previews"/role/(natural+"__"+tex["hash64"]+".png")
                target.parent.mkdir(parents=True,exist_ok=True)
                if target.exists() and not a.force:
                    raise FileExistsError("PNG exists, pass --force: "+str(target))
                tmp=target.with_suffix(".png.tmp")
                tmp.write_bytes(png);tmp.replace(target)
                count[role]+=1
        print(json.dumps({
            "png":count,"total":sum(count.values()),
            "output_directory":str(a.manifest.parent/"previews"),
            "note":"HDR source preserved losslessly in DDS; only previews are tone-mapped"
        },indent=2))
        return 0
    except (OSError,ValueError,KeyError,struct.error) as e:
        print("SKY PNG PREVIEW FAILURE:",e,file=sys.stderr);return 2

if __name__=="__main__":raise SystemExit(main())
