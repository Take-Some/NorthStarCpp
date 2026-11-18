#!/usr/bin/env python3
"""Restore Isaac's full-resolution PC Remastered streamed texture dictionary.

Never modifies original game PAK files. All PAK-local mip levels must match
corresponding streaming-dictionary entries byte-for-byte; no synthesized
pixels, fake mips or approximated images. Existing native colorspaces are
preserved exactly. Atomic replace with private backup after NETD validation.

Requires: python tools/native_texturedict.py --character isaac
"""
from __future__ import annotations
import argparse,collections,hashlib,json,os,struct,sys,zlib
from pathlib import Path
from blake3 import blake3
from native_texturedict import CHARACTERS
from native_ytd import load_sources,unpad,encode_netd,encode_nef8,validate_ytd

BASE=CHARACTERS/"isaac"
EXPECTED=157

def current_color_spaces(path:Path):
    """Read the current genuine BCn color-space policy, not re-infer it."""
    data=path.read_bytes()
    if data[:4]!=b"NEF8" or struct.unpack_from("<H",data,6)[0]!=1:
        raise ValueError("Expected a native Isaac NEF8 YTD for original colorspaces")
    c=struct.unpack_from("<Q",data,16)[0]
    body=zlib.decompress(data[-c:],-15)
    if blake3(body).digest()!=data[32:64] or body[:4]!=b"NETD":
        raise ValueError("Isaac existing YTD is corrupt")
    table=struct.unpack_from("<Q",body,16)[0]
    if body[table:table+4]!=b"NTDX":
        raise ValueError("Missing Isaac NETD table")
    n=struct.unpack_from("<I",body,table+12)[0]
    if n!=EXPECTED:
        raise ValueError(f"Unexpected Isaac YTD count {n}")
    result={}
    for i in range(n):
        h,start,size,no,nlen,fmt,w,ht,mi,mnum,cs=struct.unpack_from(
            "<QQQIHHIIIII",body,table+40+i*64)
        if cs not in (1,2):raise ValueError("Original colorspace invalid")
        key=f"{h:016x}"
        if key in result:raise ValueError("Duplicate original hash in YTD")
        result[key]=cs
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index",type=Path,
        default=BASE/"source/streamed-texture-index.json")
    parser.add_argument("--output",type=Path,
        default=BASE/"textures/isaac.ytd")
    parser.add_argument("--report",type=Path,
        default=BASE/"source/fullres-texture-report.json")
    parser.add_argument("--no-replace",action="store_true")
    args=parser.parse_args()
    manifest=json.loads(args.index.read_text("utf8"))
    mapping=manifest["textures"]
    if (manifest.get("character")!="isaac" or
        manifest["matched_hashes"]!=EXPECTED or
        manifest["missing_hashes"] or len(mapping)!=EXPECTED):
        raise ValueError("Expected 157/157 original Isaac dictionary hashes")
    original,duplicates=load_sources(BASE/"source/native_vram/vram.manifest.json")
    if len(original)!=EXPECTED:
        raise ValueError("Unexpected original Isaac PAK texture count")
    low={t["hash64"]:t for t in original}
    color=current_color_spaces(args.output)
    if set(low)!=set(color) or set(low)!=set(mapping):
        raise ValueError("Isaac dictionary/PAK/YTD texture hashes do not match")
    upgraded=[];stats=collections.Counter();records=[]
    for ordinal,key in enumerate(sorted(mapping)):
        locations=mapping[key]
        source=locations[0]
        if source["format"]!=low[key]["dxgi"]:
            raise ValueError("GPU format mismatch: "+key)
        with Path(source["path"]).open("rb") as f:
            f.seek(source["offset"])
            pixels=f.read(source["size"])
        if len(pixels)!=source["size"]:
            raise ValueError("Truncated native streamed pixels: "+key)
        mips=unpad(pixels,source["width"],source["height"],
                   source["mips"],source["format"])
        local=low[key]["mips"]
        for w,h,raw in local:
            matching=[m for m in mips if m[0]==w and m[1]==h]
            if len(matching)!=1 or matching[0][2]!=raw:
                raise ValueError(f"Original Isaac mip mismatch: {key} {w}x{h}")
        if len(mips)<len(local) or mips[0][0]<local[0][0] or mips[0][1]<local[0][1]:
            raise ValueError("Streaming content smaller than original PAK")
        stats["upgraded" if len(mips)>len(local) else "already_complete"]+=1
        stats["matching_source_local_mips"]+=len(local)
        stats["original_streamed_mip_levels"]+=len(mips)
        stats["pixel_bytes"]+=sum(len(entry[2]) for entry in mips)
        stats["streaming_payload_bytes"]+=len(pixels)
        upgraded.append({
            "name":"tex_"+key,"hash64":key,
            "dxgi":source["format"],"width":source["width"],
            "height":source["height"],"mips":mips,"colorspace":color[key]})
        records.append({
            "hash64":key,
            "original_dictionary":source["path"],
            "original_source_offset":source["offset"],
            "raw_original_sha256":hashlib.sha256(pixels).hexdigest(),
            "source_local_mips_verified":len(local),
            "width":source["width"],"height":source["height"],
            "format":source["format"],"streamed_mips":len(mips),
            "colorspace":color[key]})
        if (ordinal+1)%40==0:
            print(f"ISAAC FULLRES verified {ordinal+1}/{EXPECTED}",flush=True)
    report={
        "schema":"northstar.isaac.streamed_texture_import.v1",
        "status":"original_157_of_157_verified",
        "count":EXPECTED,
        "source_dict_count":len(manifest["dictionaries"]),
        "stats":dict(stats),
        "texture_records":records,
        "source_texture_policy":"Original PAK-local mip bytes must match all streaming dictionary mip tails",
        "original_bcn_blocks_preserved":True,
        "color_space_policy":"Preserved original Isaac YTD values",
        "copyright":"Game proprietary assets remain local and excluded from Git"
    }
    args.report.parent.mkdir(parents=True,exist_ok=True)
    if args.no_replace:
        args.report.write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
        print(json.dumps({"status":"validated_only","stats":report["stats"]},indent=2))
        return 0
    native=encode_nef8(encode_netd(upgraded),EXPECTED,
                       "characters/isaac/textures/isaac.ytd")
    result=validate_ytd(native,EXPECTED)
    if (result["mip_levels"]!=stats["original_streamed_mip_levels"] or
        result["pixel_bytes"]!=stats["pixel_bytes"]):
        raise ValueError("Isaac NETD lost some original streamed mips")
    report["native"]={**result,"sha256":hashlib.sha256(native).hexdigest()}
    backup=args.report.parent/"previous-lowres-isaac.ytd"
    if not backup.exists():
        original_bytes=args.output.read_bytes()
        backup.write_bytes(original_bytes)
        report["previous_lowres_sha256"]=hashlib.sha256(original_bytes).hexdigest()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    temp=args.output.with_suffix(".ytd.tmp")
    temp.write_bytes(native)
    if hashlib.sha256(temp.read_bytes()).hexdigest()!=report["native"]["sha256"]:
        raise ValueError("Atomic staging corrupted")
    os.replace(temp,args.output)
    args.report.write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    print(json.dumps({"status":"fullres_written","native":report["native"],
                      "stats":report["stats"],"backup":str(backup)},indent=2))
    return 0

if __name__=="__main__":
    try:raise SystemExit(main())
    except (ValueError,OSError,KeyError,struct.error) as exc:
        print("ISAAC FULLRES ERROR:",exc,file=sys.stderr)
        raise SystemExit(2)
