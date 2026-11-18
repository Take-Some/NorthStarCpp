#!/usr/bin/env python3
"""Upgrade Ellie textures from original PC streaming dictionary to native YTD.

Match every 64-bit texture id, compare ALL source PAK-local low-resolution mips
byte-for-byte against the corresponding streamed mips, preserve original BCn
blocks, then atomically replace the low-res NorthStar YTD only after complete
NETD/NEF8 validation. Never alters the source game dictionaries.

Source dictionary offsets derived from the PC PAK page/fixup structure.
See native_texturedict.py and its documented external format reference.
"""
from __future__ import annotations
import argparse,collections,hashlib,json,os,sys,time
from pathlib import Path
from native_texturedict import ELLIE
from native_ytd import (
    unpad,load_sources,encode_netd,encode_nef8,validate_ytd
)

def main():
    arg=argparse.ArgumentParser(description=__doc__)
    arg.add_argument("--index",type=Path,
        default=ELLIE/"source/streamed-texture-index.json")
    arg.add_argument("--output",type=Path,
        default=ELLIE/"textures/ellie.ytd")
    arg.add_argument("--report",type=Path,
        default=ELLIE/"source/fullres-texture-report.json")
    arg.add_argument("--no-replace",action="store_true",
        help="Validate all dictionary mip tails, but do not replace YTD")
    a=arg.parse_args()
    index=json.loads(a.index.read_text("utf8"))
    references=index["textures"]
    if index["matched_hashes"]!=397 or len(references)!=397 or index["missing_hashes"]:
        raise ValueError("Full-resolution source coverage must be 397/397")
    low,duplicates=load_sources(ELLIE/"source/native_vram/vram.manifest.json")
    old={t["hash64"]:t for t in low}
    old_report=json.loads(
        (ELLIE/"source/native-ellie-textures-report.json").read_text("utf8"))
    color={t["hash64"]:t["colorspace"] for t in old_report["texture_manifest"]}
    if len(old)!=397 or set(old)!=set(references):
        raise ValueError("Source PAK/streaming texture IDs differ")
    stats=collections.Counter()
    ready=[]
    checks=[]
    total_source_bytes=0
    for i,hash64 in enumerate(sorted(references)):
        item=references[hash64][0]
        if item["format"]!=old[hash64]["dxgi"]:
            raise ValueError("DXGI format mismatch for "+hash64)
        path=Path(item["path"])
        with path.open("rb") as f:
            f.seek(item["offset"])
            raw=f.read(item["size"])
        if len(raw)!=item["size"]:
            raise ValueError("Incomplete original streamed texture "+hash64)
        levels=unpad(raw,item["width"],item["height"],
                     item["mips"],item["format"])
        low_mips=old[hash64]["mips"]
        for width,height,bytes_ in low_mips:
            exact=[m for m in levels
                   if m[0]==width and m[1]==height]
            if len(exact)!=1 or exact[0][2]!=bytes_:
                raise ValueError(f"Streaming/local PAK mip mismatch at {hash64} {width}x{height}")
        if len(levels)<len(low_mips):
            raise ValueError("Full resolution has fewer mip levels than local PAK")
        if levels[0][0]<low_mips[0][0] or levels[0][1]<low_mips[0][1]:
            raise ValueError("Streaming dictionary unexpectedly downscaled")
        if len(low_mips)==len(levels):
            stats["already_complete"]+=1
        else:stats["upgraded"]+=1
        stats["matching_source_local_mips"]+=len(low_mips)
        stats["original_streamed_mip_levels"]+=len(levels)
        stats["pixel_bytes"]+=sum(len(m[2]) for m in levels)
        total_source_bytes+=len(raw)
        checks.append({
           "hash64":hash64,"original_dictionary":path.name,
           "dxgi":item["format"],"width":item["width"],
           "height":item["height"],"mips":len(levels),
           "source_local_mip_count":len(low_mips),
           "raw_streamed_sha256":hashlib.sha256(raw).hexdigest(),
        })
        ready.append({
           "name":"tex_"+hash64,"hash64":hash64,
           "dxgi":item["format"],"width":item["width"],
           "height":item["height"],"mips":levels,
           "colorspace":color[hash64],
        })
        if (i+1)%75==0:
            print(f"[fullres] verified {i+1}/397 original texturedict3 entries",flush=True)

    result={
        "schema":"northstar.ellie.streamed_texture_import.v1",
        "status":"397_original_D3D12_streaming_mips_validated",
        "sources":{"local_source_pak_count":len(low),
                   "original_dict_files":len(index["dictionaries"]),
                   "source_streamed_payload_bytes":total_source_bytes},
        "stats":dict(stats),"textures":checks,
        "color_space_policy":"Preserved source material slot inference from prior native YTD",
        "original_bcn_blocks_preserved":True,
        "source_mip_hash_match":"all matching low-res mip blocks byte-identical",
        "copyright":"Third-party game content, local use only; do not publish"
    }
    if a.no_replace:
        a.report.parent.mkdir(parents=True,exist_ok=True)
        a.report.write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
        print(json.dumps({"mode":"validate-only","stats":result["stats"]},indent=2))
        return 0

    native=encode_nef8(encode_netd(ready),397,
        "characters/ellie/textures/ellie.ytd")
    check=validate_ytd(native,397)
    if check["mip_levels"]!=stats["original_streamed_mip_levels"] or \
        check["pixel_bytes"]!=stats["pixel_bytes"]:
        raise ValueError("Final YTD does not contain all original mip levels")
    result["native"]={
        **check,"sha256":hashlib.sha256(native).hexdigest(),
        "path":str(a.output)}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.report.parent.mkdir(parents=True,exist_ok=True)
    # Preserve the old lowres YTD before final atomic swap.
    backup=a.report.parent/"previous-lowres-ellie.ytd"
    if a.output.exists() and not backup.exists():
        oldbytes=a.output.read_bytes()
        if oldbytes[:4]!=b"NEF8":
            raise ValueError("Existing asset is not an NEF8 YTD")
        backup.write_bytes(oldbytes)
    output_tmp=a.output.with_suffix(".ytd.tmp")
    output_tmp.write_bytes(native)
    if hashlib.sha256(output_tmp.read_bytes()).hexdigest()!=result["native"]["sha256"]:
        raise ValueError("Temporary YTD changed on disk")
    os.replace(output_tmp,a.output)
    a.report.write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
    print(json.dumps({"mode":"native-fullres","stats":result["stats"],
                      "native":result["native"],"backup":str(backup)},
                     indent=2))
    return 0

if __name__=="__main__":
    try:raise SystemExit(main())
    except (ValueError,OSError,KeyError) as exc:
        print("FULLRES ELLIE IMPORT FAILED:",exc,file=sys.stderr)
        raise SystemExit(2)
