#!/usr/bin/env python3
"""Fail-closed original decoded animation keys -> private NEF8/YCD -> project install.

ICE/STM are not themselves decoded here: a verified bone-track manifest is required.
Never relabel Abby retargets or raw ICE buffers as native Ellie/Isaac clips.
"""
from __future__ import annotations
import argparse, hashlib, json, os, struct, sys, zlib
from pathlib import Path
from blake3 import blake3

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools"))
from native_decoded_tracks_to_ycd import build, ImportErrorInvariant

PRIVATE=Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2")
SHARED=ROOT/"engine/northstarcpp/resources/sharedAssets/characters"
PROJECT=ROOT/"Projects/KaylaNightCity/assets/animations/npcs/native"

def verify_ycd(blob,expected):
    if len(blob)<64 or blob[:4]!=b"NEF8" or blob[4]!=2:raise ValueError("Invalid YCD header")
    packed,plain=struct.unpack_from("<QQ",blob,16)
    if not 0<packed<=len(blob)-64 or not 0<plain<=64*1024*1024:
        raise ValueError("Unsafe YCD compressed size")
    raw=zlib.decompress(blob[-packed:],-15)
    if len(raw)!=plain or blake3(raw).digest()!=blob[32:64]:raise ValueError("YCD checksum mismatch")
    version,count,table,strings,_,payload=struct.unpack_from("<IIQQQQ",raw)
    if (version,count,table)!=(3,1,48) or not 112<=strings<=payload<=len(raw):
        raise ValueError("Malformed YCD dictionary")
    name_offset=struct.unpack_from("<I",raw,table+8)[0]
    start_name=strings+name_offset
    if start_name>=payload:raise ValueError("Invalid YCD name")
    nul=raw.find(b"\x00",start_name,payload)
    if nul<0 or raw[start_name:nul].decode("ascii")!=expected:
        raise ValueError("Wrong YCD clip name")
    tracks,frames=struct.unpack_from("<II",raw,table+16)
    offset,length=struct.unpack_from("<QQ",raw,table+40)
    if (not 1<=tracks<=4096 or not 1<=frames<=12000 or
        not payload<=offset<=len(raw) or length!=tracks*(4+40*frames) or
        offset+length>len(raw)):
        raise ValueError("Invalid YCD frame and joint buffer")
    return {"tracks":tracks,"frames":frames,"sha256":hashlib.sha256(blob).hexdigest()}

def save_immutable(path,data):
    if path.is_file():
        if path.read_bytes()!=data:raise FileExistsError("Refusing to overwrite modified YCD: "+str(path))
        return "unchanged"
    path.parent.mkdir(parents=True,exist_ok=True)
    stage=path.with_suffix(".ycd.tmp")
    try:
        stage.write_bytes(data)
        if stage.read_bytes()!=data:raise IOError("Staged YCD differs")
        os.replace(stage,path)
    finally:
        stage.unlink(missing_ok=True)
    return "created"

def convert_one(path):
    doc=json.loads(path.read_text(encoding="utf-8"))
    char=doc.get("character")
    if char not in ("ellie","isaac"):raise ImportErrorInvariant("Missing original character")
    clip=doc["clips"][0]["name"]
    logical="animations/npcs/native/"+char+"/"+clip+".ycd"
    rig="characters/"+char+"/models/"+char+".ymt"
    original=SHARED/char/"models"/(char+".ymt")
    if doc.get("logical_path")!=logical or doc.get("skeleton_reference")!=rig:
        raise ImportErrorInvariant("Native YCD reference does not match its actor")
    if Path(doc["skeleton_ymt"]).resolve()!=original.resolve():
        raise ImportErrorInvariant("Not the original target YMT skeleton")
    src=Path(doc["source_file"])
    if src.suffix.lower()!=(".iceanim" if char=="ellie" else ".stm"):
        raise ImportErrorInvariant("Not the original game animation source for actor")
    data,info=build(doc)
    test=verify_ycd(data,clip)
    if info["frames"]!=test["frames"] or info["joint_tracks"]!=test["tracks"]:
        raise ImportErrorInvariant("Independent YCD roundtrip mismatch")
    return char,clip,data,{**info,**test,"decoded_manifest":str(path),
        "provenance":"source-hash-matched decoded original joint tracks"}

def run(source,output,project,write=False,install=False):
    if install and not write:raise ValueError("Install requires private conversion")
    files=sorted(source.rglob("*.json")) if source.is_dir() else []
    if len(files)>4096:raise ValueError("Too many input animation manifests")
    results=[];failed=[];seen=set()
    for file in files:
        try:
            char,name,data,info=convert_one(file)
            if (char,name) in seen:raise ImportErrorInvariant("Duplicate native animation")
            seen.add((char,name))
            if write:
                info["private"]=save_immutable(output/char/(name+".ycd"),data)
                if install:
                    info["project"]=save_immutable(project/char/(name+".ycd"),data)
                    info["engine_reference"]="animations/npcs/native/"+char+"/"+name+".ycd@"+name
            results.append(info)
        except (ValueError,KeyError,IndexError,TypeError,OSError,struct.error,zlib.error) as exc:
            failed.append({"file":str(file),"error":str(exc)})
    return {"schema":"northstar.tlou2.native_ycd_conversion.v1",
            "inputs":len(files),"validated":len(results),"converted":len(results) if write else 0,
            "installed":len(results) if install else 0,"rejected":len(failed),
            "actual_native_decoders":"external verified bone-key data required",
            "clips":results,"errors":failed}

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--input",type=Path,default=PRIVATE/"decoded")
    ap.add_argument("--output",type=Path,default=PRIVATE/"converted")
    ap.add_argument("--project-output",type=Path,default=PROJECT)
    ap.add_argument("--convert",action="store_true")
    ap.add_argument("--install",action="store_true")
    ap.add_argument("--report",type=Path)
    a=ap.parse_args()
    out=run(a.input,a.output,a.project_output,a.convert,a.install)
    if a.report:
        a.report.parent.mkdir(parents=True,exist_ok=True)
        a.report.write_text(json.dumps(out,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    print(json.dumps({k:v for k,v in out.items() if k not in ("clips","errors")},indent=2))
    for item in out["errors"][:6]:print("REJECTED",item,file=sys.stderr)
    return 2 if out["rejected"] else 0
if __name__=="__main__":raise SystemExit(main())
