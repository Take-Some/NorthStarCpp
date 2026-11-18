#!/usr/bin/env python3
"""Direct extraction of source GPU texture payloads from TLOU2 PC PAK.

Saves original (potentially tiled) GPU VRAM compressed blocks with hashes,
dimensions, formats and source provenance. Does NOT claim they are decoded
DDS/PNG or an engine .ytd dictionary.
"""
from __future__ import annotations
from pathlib import Path
import argparse, collections, hashlib, json, sys
from native_pak import NaughtyDogPak, PakError
from native_mesh_preview import BASE

DXGI={28:"R8G8B8A8_UNORM",71:"BC1_UNORM",72:"BC1_SRGB",
      77:"BC3_UNORM",80:"BC4_UNORM",83:"BC5_UNORM",98:"BC7_UNORM"}
def run():
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--source",type=Path,default=BASE/"source/actor97")
    cli.add_argument("--dest",type=Path,default=BASE/"source/native_vram")
    cli.add_argument("--force",action="store_true")
    args=cli.parse_args()
    record={"schema":"northstar.source.pak.vram.v1","status":"original_GPU_blocks_not_YTD","sources":[]}
    try:
        for file in sorted(args.source.glob("ellie-*.pak")):
            pak=NaughtyDogPak(file)
            descriptions=pak.typed("VRAM_DESC")
            if not descriptions:continue
            source_tail=pak.pages[-1][0]+pak.pages[-1][1]
            combined=bytearray()
            entries=[]
            for i,desc in enumerate(descriptions):
                start=desc["offset"]+16
                data_offset=pak.u32(start+40)
                size=pak.u32(start+48)
                h=pak.u64(start+56)
                fmt=pak.u32(start+72)
                mip=pak.u32(start+80)
                width=pak.u32(start+84)
                height=pak.u32(start+88)
                pos=source_tail+data_offset
                if size<1 or size>128*1024*1024 or pos+size>len(pak.data):
                    raise PakError(f"Invalid VRAM image range {file.name}#{i}")
                if not 0<width<=16384 or not 0<height<=16384:
                    raise PakError("Invalid native texture dimensions")
                entry={
                    "index":i, "hash64":f"{h:016x}",
                    "dxgi":fmt,"format":DXGI.get(fmt,"unknown"),
                    "width":width, "height":height, "mips":mip,
                    "source_file_offset":pos,
                    "blob_offset":len(combined), "blob_bytes":size,
                }
                data=pak.data[pos:pos+size]
                entry["sha256"]=hashlib.sha256(data).hexdigest()
                combined.extend(data)
                entries.append(entry)
            target=args.dest/(file.stem+".vram")
            if target.exists() and not args.force:
                raise FileExistsError(f"VRAM payload already exists: {target}")
            target.parent.mkdir(parents=True,exist_ok=True)
            part=target.with_suffix(".vram.tmp")
            part.write_bytes(combined)
            part.replace(target)
            record["sources"].append({
                "pak":file.name, "vram_blob":str(target),
                "gpu_blocks":len(entries),"total_bytes":len(combined),
                "entries":entries
            })
        record["total_source_paks"]=len(record["sources"])
        record["total_texture_descriptors"]=sum(a["gpu_blocks"] for a in record["sources"])
        record["total_gpu_bytes"]=sum(a["total_bytes"] for a in record["sources"])
        report=args.dest/"vram.manifest.json"
        report.write_text(json.dumps(record,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
        print(json.dumps({
            "source_paks":record["total_source_paks"],
            "source_texture_descriptors":record["total_texture_descriptors"],
            "raw_vram_bytes":record["total_gpu_bytes"],
            "report":str(report), "not_ytd":True
        },ensure_ascii=False,indent=2))
        return 0
    except (OSError,ValueError) as exc:
        print("VRAM NATIVE EXTRACT FAILURE:",exc,file=sys.stderr)
        return 2

if __name__=="__main__":
    raise SystemExit(run())
