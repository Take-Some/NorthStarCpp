#!/usr/bin/env python3
"""Preserve original Naughty Dog TLOU2R ICE ANIM buffers without codec claims.

Input: .pak resources selectively extracted from this user's local installation.
Output: genuine ICE payload slices (*.iceanim) + SHA256 manifest.
This DOES NOT create/pretend NorthStar YCD or decode animation keyframes.
No source PAK/PSARC changes, no DRM bypass, no redistribution.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools"))
from native_pak import NaughtyDogPak, PakError

MAGIC_PREFIX=bytes.fromhex("02014341")
SUFFIX=".iceanim"


def safe_stem(name):
    normalized=re.sub(r"[^a-z0-9_.-]+","_",name.lower()).strip("._")
    if not normalized:
        normalized="unnamed"
    return normalized[:90]+"_"+hashlib.sha256(name.encode("utf8")).hexdigest()[:12]


def main():
    cli=argparse.ArgumentParser()
    cli.add_argument("--input",type=Path,
        default=ROOT/"out/tlou2_native/extracted")
    cli.add_argument("--output",type=Path,
        default=ROOT/"out/tlou2_native/ice_animations")
    cli.add_argument("--manifest",type=Path)
    cli.add_argument("--package",action="append",help="Filter by exact .pak basename")
    args=cli.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    manifest=args.manifest or args.output/"manifest.json"
    paks=sorted(args.input.rglob("*.pak"))
    if args.package:
        allowed=set(args.package)
        paks=[p for p in paks if p.name in allowed]
        if len({p.name for p in paks})!=len(allowed):
            raise FileNotFoundError("At least one requested PAK was not extracted")
    metadata={"schema":"northstar.tlou2r.native_ice_sources.v1",
       "status":"original_ICE_animation_payloads_not_decoded_not_YCD",
       "source_root":str(args.input),
       "output_root":str(args.output),
       "packages":[],
       "clips":[],
       "unresolved":[],
       "codec":"Naughty Dog ICE / native source; not reverse decoded",
    }
    seen=set()
    total=0
    for p in paks:
        try:
            pak=NaughtyDogPak(p)
            anims=pak.typed("ANIM")
        except Exception as e:
            metadata["unresolved"].append({
                "package":p.name,"error":"parse: "+str(e)})
            print("ERROR",p.name,str(e)[:140],flush=True)
            continue
        pkgmeta={"package":p.name,"source":str(p),"anim_count":len(anims),
          "rawchannel_count":len(pak.typed("ANIM_RAWCHANNELS")),"exported":0,
          "bytes":0,"unresolved":0}
        folder=args.output / p.stem
        folder.mkdir(exist_ok=True)
        for clip in anims:
            name=clip["name"]
            src=clip["offset"]
            try:
                data_offset=pak.pointer(src+56)
                channel_ref=pak.pointer(src+64)
                size=pak.u32(data_offset+8)
                if not 64<=size<=32*1024*1024 or data_offset+size>len(pak.data):
                    raise PakError("ANIM extent invalid")
                blob=pak.data[data_offset:data_offset+size]
                if blob[:4]!=MAGIC_PREFIX:
                    raise PakError("Invalid native ICE ANIM signature")
                # Tiny 64-byte native records are *stream descriptors*, not
                # full keyframe streams; save their identities in the manifest
                # and rely on the separately preserved .stm source.
                if size<=128:
                    raise PakError(f"stream_descriptor_only size={size}")
                filename=safe_stem(name)+SUFFIX
                relative=Path(p.stem)/filename
                key=(p.name,name)
                if key in seen:
                    raise PakError("Duplicate animation entry")
                seen.add(key)
                target=args.output/relative
                # Never silently clobber authored or modified source files.
                if target.exists():
                    if target.read_bytes()!=blob:
                        raise FileExistsError(f"Existing ICE buffer differs: {target}")
                else:
                    tmp=target.with_suffix(".iceanim.partial")
                    tmp.write_bytes(blob)
                    tmp.replace(target)
                checksum=hashlib.sha256(blob).hexdigest()
                metadata["clips"].append({
                    "source_pak":str(p),
                    "source_pack_name":p.name,
                    "name":name,
                    "filename":relative.as_posix(),
                    "bytes":size,
                    "sha256":checksum,
                    "ice_header":blob[:8].hex(),
                    "source_data_offset":data_offset,
                    "source_rawchannels_offset":channel_ref,
                    "source_anim_version":pak.u32(src+48),
                    "source_flags":pak.u32(src+52),
                })
                total+=size
                pkgmeta["bytes"]+=size
                pkgmeta["exported"]+=1
            except (ValueError,IndexError,OverflowError,FileNotFoundError) as e:
                metadata["unresolved"].append({
                  "source_pack_name":p.name,"name":name,
                  "reason":str(e)})
                pkgmeta["unresolved"]+=1
        metadata["packages"].append(pkgmeta)
        print("PACKAGE",p.name,"ANIM",len(anims),
            "EXPORTED",pkgmeta["exported"],"UNRESOLVED",pkgmeta["unresolved"],
            "BYTES",pkgmeta["bytes"],flush=True)
    metadata["summary"]={
        "packages":len(metadata["packages"]),
        "native_ice_buffers":len(metadata["clips"]),
        "unresolved_records":len(metadata["unresolved"]),
        "total_ice_bytes":total,
        "ice_megabytes":round(total/1048576,2)
    }
    manifest.write_text(json.dumps(metadata,ensure_ascii=False,indent=2),"utf8")
    print("SUMMARY",json.dumps(metadata["summary"],ensure_ascii=False),flush=True)
    print("MANIFEST",manifest,flush=True)
    return 0 if metadata["clips"] else 3


if __name__=="__main__":
    raise SystemExit(main())
