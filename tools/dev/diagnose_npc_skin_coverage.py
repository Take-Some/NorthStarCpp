#!/usr/bin/env python3
"""Read-only analysis: actual imported YDD vertex influences vs NPC YCD keyed joints."""
from collections import defaultdict,Counter
import json,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.dev.retarget_nightcity_npc_ycd import unpack,skeleton,SHARED,OUT

def info(slug):
    raw=unpack(SHARED/slug/"models"/(slug+".ydd"))
    assert raw[:4]==b"NVRM"
    metadata_size=struct.unpack_from("<I",raw,8)[0]
    meta=json.loads(raw[20:20+metadata_size])
    base=20+metadata_size
    joints=skeleton(slug)
    names=[n for n,*_ in joints]
    ycd=unpack(OUT/(slug+"_locomotion.ycd"))
    records=struct.unpack_from("<IIQQQQ",ycd)
    clip=records[2]
    tracks_count=struct.unpack_from("<I",ycd,clip+16)[0]
    offset=struct.unpack_from("<Q",ycd,clip+40)[0]
    keyed=set(struct.unpack_from("<"+"I"*tracks_count,ycd,offset))
    weight_total=defaultdict(float)
    total=0
    for mesh in meta["meshes"]:
        streams={k["semantic"]:k for k in mesh["streams"]}
        weights=streams["joint_weights"]
        indices=streams["joint_indices"]
        for i in range(mesh["vertex_count"]):
            ws=struct.unpack_from("<12f",raw,base+weights["offset"]+i*48)
            bs=struct.unpack_from("<12H",raw,base+indices["offset"]+i*24)
            for b,w in zip(bs,ws):
                if w>0:
                    weight_total[b]+=w
                    total+=w
    weights_sorted=sorted(weight_total.items(),key=lambda p:-p[1])
    covered=sum(w for b,w in weight_total.items() if b in keyed)
    weighted_primary=[(names[b],round(w/total*100,3),b in keyed) for b,w in weights_sorted[:28]]
    suffix_counts=Counter()
    for b,w in weight_total.items():
        n=names[b]
        c="keyed" if b in keyed else ("helper" if ("_helper" in n or "_roll" in n or "runtime" in n) else "unkeyed_other")
        suffix_counts[c]+=w
    import xml.etree.ElementTree as ET
    xml=ET.fromstring(unpack(SHARED/slug/"models"/(slug+".ymt")))
    entries=xml.findall("./Skeleton/Joint")
    by_name={a.attrib["name"]:i for i,a in enumerate(entries)}
    def ancestors(index):
        chain=[]
        for _ in range(1500):
            a=entries[index].attrib
            chain.append(a["name"])
            nxt=by_name.get(a.get("parent",""),-1)
            if nxt<0:break
            index=nxt
        return chain
    for b,_ in weights_sorted[:16]:
        print("BONE_PATH",slug,names[b],"->"," > ".join(ancestors(b)[:14]))
    print(json.dumps({"model":slug,"bones":len(joints),"ycdTracks":len(keyed),"vertices":sum(x["vertex_count"] for x in meta["meshes"]),"weightTotal":round(total,1),"weightsPercentDirectKeyed":round(covered/total*100,3),"weightFractions":{x:round(n/total*100,3) for x,n in suffix_counts.items()},"top":weighted_primary},indent=2))
for slug in ("ellie","isaac"):info(slug)
