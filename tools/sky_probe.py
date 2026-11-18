#!/usr/bin/env python3
"""Inspect actual sky/cloud PAK source texture names, formats and sky effects."""
from pathlib import Path
import sys,json,collections
from native_pak import NaughtyDogPak
ROOT=Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\archives")
FILES=sorted([
 p for archive in ("common","sp-common")
 for p in (ROOT/archive/"pak68").glob("sky-*.pak")
 if not p.stem.endswith(("-phys","-ingame","-mat"))
])
def inspect(p):
    pak=NaughtyDogPak(p)
    vrams={}
    for v in pak.typed("VRAM_DESC"):
        off=v["offset"]+16
        h=f"{pak.u64(off+56):016x}"
        vrams[h]={"format":pak.u32(off+72),"width":pak.u32(off+84),"height":pak.u32(off+88),
                  "mips":pak.u32(off+80),"source_bytes":pak.u32(off+48)}
    refs=[]
    for g in pak.typed("GEOMETRY_1"):
        ptr=pak.pointer(g["offset"]+88)
        seen=set()
        for k in range(pak.u32(g["offset"]+56)):
            sm=ptr+192*k
            mat=pak.pointer(sm+72)
            if mat in seen:continue
            seen.add(mat)
            shader=pak.ascii(pak.pointer(mat+8))
            table=pak.pointer(mat+32)
            for j in range(pak.u32(mat+276)):
                entry=table+j*48
                slot=pak.ascii(pak.pointer(entry))
                ref=pak.pointer(entry+24)
                name=pak.ascii(pak.pointer(ref))
                h=f"{pak.u64(ref+8):016x}"
                refs.append({"shader":shader,"slot":slot,"name":name,"hash":h,"local":h in vrams})
    return {"source":str(p),"sky_variant":p.stem,"size":p.stat().st_size,"vram":vrams,"textures":refs}
def run():
    packs=[inspect(p) for p in FILES]
    freq=collections.Counter(v["format"] for p in packs for v in p["vram"].values())
    slots=collections.Counter(t["slot"] for p in packs for t in p["textures"])
    missing=[(p["sky_variant"],t) for p in packs for t in p["textures"] if not t["local"]]
    uniques={}
    for p in packs:
        for k,v in p["vram"].items():
            if k in uniques and uniques[k]!=v:raise RuntimeError("Same source VRAM hash changed fields: "+k)
            uniques[k]=v
    print(json.dumps({
        "sky_paks":len(packs),
        "vram_total":sum(len(p["vram"]) for p in packs),
        "unique_vram_hashes":len(uniques),
        "format_counts":dict(freq),
        "source_binding_slots":dict(slots),
        "missing_bindings":missing[:15],
        "missing_binding_count":len(missing),
        "sky_variants":[{"name":p["sky_variant"],"vram":len(p["vram"]),"bindings":len(p["textures"])} for p in packs],
        "example_materials":packs[0]["textures"]
    },indent=2,ensure_ascii=False))
if __name__=="__main__":run()
