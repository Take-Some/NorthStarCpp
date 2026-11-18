#!/usr/bin/env python3
"""Build genuine NorthStar YCD idle/walk *retargets* from an existing playable
motion source onto each character's OWN native YMT joint hierarchy.

The output is explicitly derived from Abby animations, NOT original Ellie or
Isaac ICE motions. Avoids fabricated joint ids, implicit fallback, and raw
bind-pose sliding. No file changes unless --apply; existing output is checked
for byte-identical idempotence and otherwise refused without --force.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zlib
from blake3 import blake3
from tools.dev.install_nightcity_npcs import ROOT

SHARED=ROOT/"engine/northstarcpp/resources/sharedAssets/characters"
OUT=ROOT/"Projects/KaylaNightCity/assets/animations/npcs"
SOURCE="characters/abby/animations/mm-explore.ycd"
CLIPS={"npc-idle":"abby-mm-explore-idle","npc-walk":"abby-mm-explore-walk-loop-fw"}
CORE={
    "root","pelvis","spinea","spineb","spinec","spined","neck",
    "heada","headb","l_clavicle","r_clavicle",
    "l_shoulder","r_shoulder","l_elbow","r_elbow","l_wrist","r_wrist",
    "l_palm","r_palm","l_hand_prop","r_hand_prop",
    "l_hand_prop_attachment","r_hand_prop_attachment",
    "l_upper_leg","r_upper_leg","l_knee","r_knee",
    "l_ankle","r_ankle","l_ball","r_ball","l_toe","r_toe",
    "l_heel","r_heel",
}
def unpack(path:Path)->bytes:
    data=path.read_bytes()
    if data[:4]!=b"NEF8" or data[4]!=2:raise ValueError(f"Invalid NEF8: {path}")
    packed,expanded=struct.unpack_from("<QQ",data,16)
    if packed<=0 or expanded>100_000_000 or packed>len(data)-64:
        raise ValueError("Unsafe NEF8 decoded size")
    out=zlib.decompress(data[-packed:],-15)
    flags=struct.unpack_from("<H",data,8)[0]
    if len(out)!=expanded or (flags & 4 and blake3(out).digest()!=data[32:64]):
        raise ValueError("NEF8 digest mismatch")
    return out

def skeleton(slug):
    p=SHARED/slug/"models"/(slug+".ymt")
    doc=ET.fromstring(unpack(p))
    joints=[]
    for i,j in enumerate(doc.findall("./Skeleton/Joint")):
        attrs=j.attrib
        if int(attrs["index"])!=i:raise ValueError("Invalid joint order")
        pos=tuple(float(attrs[k]) for k in ("tx","ty","tz"))
        quat=tuple(float(attrs[k]) for k in ("qx","qy","qz","qw"))
        scale=tuple(float(attrs[k]) for k in ("sx","sy","sz"))
        joints.append((attrs["name"],pos,quat,scale))
    return joints

def source_clips():
    data=unpack(SHARED/"abby"/"animations"/"mm-explore.ycd")
    version,entries,table,strings,_,payload=struct.unpack_from("<IIQQQQ",data)
    if version not in (2,3) or entries>4096 or table!=48 or strings<48+64*entries or strings>payload:
        raise ValueError("Unsupported YCD dictionary")
    desired=set(CLIPS.values())
    result={}
    for index in range(entries):
        record=table+64*index
        name_offset=struct.unpack_from("<I",data,record+8)[0]
        start_name=strings+name_offset
        name=data[start_name:data.index(b"\0",start_name)].decode("utf8")
        if name not in desired:continue
        tracks,frames=struct.unpack_from("<II",data,record+16)
        duration,rate=struct.unpack_from("<ff",data,record+24)
        loop=struct.unpack_from("<I",data,record+32)[0]
        start,size=struct.unpack_from("<QQ",data,record+40)
        if not (1<=tracks<=200 and 1<=frames<=12000 and rate>0 and
                size==tracks*(4+40*frames) and start+size<=len(data)):
            raise ValueError(f"Invalid selected YCD clip {name}: tracks={tracks} frames={frames} rate={rate} size={size} expected={tracks*(4+40*frames)} start={start} data={len(data)}")
        ids=struct.unpack_from("<"+("I"*tracks),data,start)
        sample_offset=start+tracks*4
        samples=struct.unpack_from("<"+("f"*(tracks*frames*10)),data,sample_offset)
        result[name]=(ids,frames,rate,loop,samples)
    if set(result)!=desired:
        raise ValueError("Source is missing idle or walking YCD clips")
    return result

def qnormalize(q):
    mag=math.sqrt(sum(n*n for n in q))
    if mag<1e-8:raise ValueError("Invalid animation quaternion")
    return tuple(x/mag for x in q)
def qinv(q):
    x,y,z,w=qnormalize(q)
    return (-x,-y,-z,w)
def qmul(a,b):
    x,y,z,w=a;u,v,t,s=b
    return (w*u+x*s+y*t-z*v,w*v-x*t+y*s+z*u,
            w*t+x*v-y*u+z*s,w*s-x*u-y*v-z*t)

def retarget(source,target,clip):
    ids,frames,rate,loop,samples=clip
    mapping={name:i for i,(name,*_) in enumerate(target)}
    matched=[]
    for source_track,source_id in enumerate(ids):
        if source_id>=len(source):raise ValueError("Invalid source YCD joint")
        name=source[source_id][0]
        if name in CORE and name in mapping:
            matched.append((source_track,source_id,mapping[name],name))
    covered={name for _,_,_,name in matched}
    needed={"pelvis","l_shoulder","r_shoulder","l_elbow","r_elbow",
            "l_upper_leg","r_upper_leg","l_knee","r_knee","l_ankle","r_ankle"}
    if not needed.issubset(covered):
        raise ValueError(f"Retarget lacks humanoid joints: {needed-covered}")
    stream=bytearray()
    for _,_,target_id,_ in matched:
        stream.extend(struct.pack("<I",target_id))
    for f in range(frames):
        for source_track,source_id,target_id,name in matched:
            src=source[source_id]
            dst=target[target_id]
            pos=src[1];bindq=src[2];srcscale=src[3]
            destpos=dst[1];destq=dst[2];destscale=dst[3]
            off=(f*len(ids)+source_track)*10
            record=samples[off:off+10]
            # Keep physical locomotion on the Scene/Jolt capsule; strip
            # original model-space root translation and root yaw.
            if name=="root":
                nextpos=destpos
                nextq=qnormalize(destq)
                nextscale=destscale
            else:
                # Decode source bind-relative local animation, then apply the
                # rotation to the target's own bind; preserve bone lengths.
                nextq=qnormalize(qmul(qmul(qnormalize(record[3:7]),
                                              qinv(bindq)),qnormalize(destq)))
                cap=0.09 if name=="pelvis" else 0.035
                nextpos=tuple(d+max(-cap,min(cap,record[i]-pos[i]))
                              for i,d in enumerate(destpos))
                nextscale=tuple(
                    d*max(0.90,min(1.10,record[i+7]/s)) if abs(s)>1e-5 else d
                    for i,(s,d) in enumerate(zip(srcscale,destscale)))
            if not all(math.isfinite(x) for x in (*nextpos,*nextq,*nextscale)):
                raise ValueError("Invalid retarget result")
            stream.extend(struct.pack("<10f",*nextpos,*nextq,*nextscale))
    return len(matched),frames,rate,loop,bytes(stream),sorted(covered)

def pack(slug,clip_records):
    names=list(clip_records)
    strings=bytearray();offsets={}
    for name in names:
        offsets[name]=len(strings)
        strings.extend(name.encode("ascii")+b"\0")
    count=len(names)
    string_pos=48+count*64
    payload_pos=(string_pos+len(strings)+15)&~15
    body=bytearray(payload_pos)
    struct.pack_into("<IIQQQQ",body,0,3,count,48,string_pos,0,payload_pos)
    body[string_pos:string_pos+len(strings)]=strings
    for index,name in enumerate(names):
        tracks,frames,rate,loop,data,_=clip_records[name]
        start=len(body)
        size=len(data)
        assert size==tracks*(4+40*frames)
        rec=48+64*index
        struct.pack_into("<I",body,rec+8,offsets[name])
        struct.pack_into("<II",body,rec+16,tracks,frames)
        duration=(frames-1)/rate if frames>1 else 1/rate
        struct.pack_into("<ff",body,rec+24,duration,rate)
        struct.pack_into("<I",body,rec+32,loop)
        struct.pack_into("<QQ",body,rec+40,start,size)
        body.extend(data)
    meta={
        "schema":"newengine.asset.list_file.header_metadata",
        "logical_path":f"animations/npcs/{slug}_locomotion.ycd",
        "content_kind":"ycd_clip_dictionary",
        "authored_by":"NorthStarCpp rig-space retarget (not original Ellie/Isaac ICE)",
        "source":SOURCE,
        "source_skeleton":"characters/abby/models/abby.ymt",
        "target_skeleton":f"characters/{slug}/models/{slug}.ymt",
        "entries":[
            {"name":name,"dependencies":[{"kind":"skeleton",
               "reference":f"characters/{slug}/models/{slug}.ymt"}]}
            for name in names
        ],
    }
    raw=bytes(body)
    meta_bytes=json.dumps(meta,ensure_ascii=False,separators=(",",":")).encode("utf8")
    zipped=zlib.compressobj(level=9,wbits=-15)
    payload=zipped.compress(raw)+zipped.flush()
    header=bytearray(64)
    header[:4]=b"NEF8"
    struct.pack_into("<BBHHHIQQ",header,4,2,6,11,7,2,count,len(payload),len(raw))
    header[32:64]=blake3(raw).digest()
    return bytes(header)+meta_bytes+payload

def build():
    source=skeleton("abby")
    clips=source_clips()
    outputs={}
    for slug in ("ellie","isaac"):
        target=skeleton(slug)
        retargeted={k:retarget(source,target,clips[v]) for k,v in CLIPS.items()}
        result=pack(slug,retargeted)
        outputs[slug]=result
        for name,(tracks,frames,rate,loop,_,covered) in retargeted.items():
            print(f"{slug} {name}: tracks={tracks} frames={frames} rate={rate:.2f} "
                  f"root-motion=in-place shared={len(covered)}")
    return outputs

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--apply",action="store_true")
    args=p.parse_args()
    outputs=build()
    for slug,data in outputs.items():
        path=OUT/(slug+"_locomotion.ycd")
        if path.exists():
            if path.read_bytes()!=data:
                raise RuntimeError("Refusing to overwrite modified derived YCD: "+str(path))
            print("VERIFIED",path.relative_to(ROOT),len(data))
        elif args.apply:
            path.parent.mkdir(parents=True,exist_ok=True)
            tmp=path.with_suffix(".ycd.tmp")
            try:
                tmp.write_bytes(data)
                if tmp.read_bytes()!=data:raise RuntimeError("Staging YCD mismatch")
                os.replace(tmp,path)
            finally:
                if tmp.exists():tmp.unlink()
            print("APPLIED",path.relative_to(ROOT),len(data))
        else:
            print("DRY_RUN",path.relative_to(ROOT),len(data))
if __name__=="__main__":main()
