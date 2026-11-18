#!/usr/bin/env python3
"""Extract true original ICE animation buffers from Ellie skeleton PAK.

This preserves source compressed animation clips, including original metadata,
without claiming to decode the game's codec or fabricate a NorthStar YCD.
"""
import argparse, hashlib, json, re, struct, sys
from pathlib import Path
from native_mesh_preview import BASE
from native_pak import NaughtyDogPak, PakError

MAGIC=bytes.fromhex("020143414c1bbe5a")
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input",type=Path,default=BASE/"source/actor97/ellie-skel.pak")
    ap.add_argument("--output",type=Path,default=BASE/"source/native_animations")
    ap.add_argument("--force",action="store_true")
    opts=ap.parse_args()
    try:
        pak=NaughtyDogPak(opts.input)
        anims=pak.typed("ANIM")
        raw=pak.typed("ANIM_RAWCHANNELS")
        if len(anims)!=72 or len(raw)!=72:
            raise PakError(f"Unexpected Ellie source animation count ({len(anims)},{len(raw)})")
        known=set()
        report={"schema":"northstar.tlou2.pc.original_ice_animation_sources.v1",
                "status":"native_SOURCE_animation_export_not_decoded_YCD",
                "source_pak":str(opts.input),"clips":[]}
        for clip in anims:
            base=clip["offset"]
            payload_addr=pak.pointer(base+56)
            channels_addr=pak.pointer(base+64)
            n=pak.u32(payload_addr+8)
            if not 64<=n<=20*1024*1024 or payload_addr+n>len(pak.data):
                raise PakError("Unsafe original ICE animation extent")
            buffer=pak.data[payload_addr:payload_addr+n]
            if buffer[:8]!=MAGIC:
                raise PakError("Original ICE animation magic mismatch")
            source_name=clip["name"]
            stem=re.sub(r"[^a-z0-9_-]","_",source_name.lower())
            if stem in known:
                raise PakError("Duplicate original ICE animation name")
            known.add(stem)
            target=opts.output/(stem+".iceanim")
            if target.exists() and not opts.force:
                raise FileExistsError(f"Source clip already exported: {target}")
            target.parent.mkdir(parents=True,exist_ok=True)
            tmp=target.with_suffix(".iceanim.tmp")
            tmp.write_bytes(buffer);tmp.replace(target)
            report["clips"].append({
                "name":source_name,"filename":target.name,"bytes":len(buffer),
                "sha256":hashlib.sha256(buffer).hexdigest(),
                "source_data_offset":payload_addr,
                "source_rawchannels_offset":channels_addr,
                "source_anim_version":pak.u32(base+48),
                "original_ice_magic":MAGIC.hex(),
                "source_flags":pak.u32(base+52),
            })
        report["total_bytes"]=sum(r["bytes"] for r in report["clips"])
        report["count"]=len(report["clips"])
        opts.output.joinpath("manifest.json").write_text(
            json.dumps(report,indent=2,ensure_ascii=False)+"\n",encoding="utf8")
        print(json.dumps({"exported_original_ice_buffers":len(anims),
                          "total_bytes":report["total_bytes"],
                          "decoded_YCD":False,
                          "manifest":str(opts.output/"manifest.json")},
                         indent=2,ensure_ascii=False))
        return 0
    except (OSError,ValueError,struct.error) as e:
        print("NATIVE ICE EXPORT FAILED:",e,file=sys.stderr)
        return 2
if __name__=="__main__":raise SystemExit(main())
