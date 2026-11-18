"""Validate AbbyPistolDemo floor geometry and unchanged gameplay components."""
from pathlib import Path
import sys, struct, json, math
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.dev.scene_binary import read_nef8_scene_records,validate_nef8_scene
from tools.create_abby_pistol_demo import entity

SCENE=ROOT/"Projects/AbbyPistolDemo/main.nscn"

def entity_info(value):
    n=struct.unpack_from("<H",value,0)[0]
    name=value[2:2+n].decode("utf8")
    pos=struct.unpack_from("<3f",value,2+n)
    size,color,mass,stable_id,parent=struct.unpack_from("<fIfQQ",value,2+n+12)
    return {"name":name,"pos":pos,"size":size,"color":color,
            "mass":mass,"stable_id":stable_id,"parent":parent}

def inspect_scene(data):
    records=read_nef8_scene_records(data)
    entities=[entity_info(blob) for kind,eid,blob in records if kind==2]
    decks=[x for x in entities if x["name"].startswith("Deck") or "Floor" in x["name"]]
    return records,entities,decks

def main():
    data=SCENE.read_bytes()
    records,entities,decks=inspect_scene(data)
    print("entities",len(entities),"records",len(records))
    print("decks",json.dumps(decks,ensure_ascii=False))
    print("pawn",next(e for e in entities if e["name"]=="Demo Pawn"))
    floor=[d for d in decks if d["name"]=="Firing Range Floor"]
    assert len(floor)==1 and len(decks)==1,"Duplicated Deck faces or missing unified slab"
    x=floor[0];px,py,pz=x["pos"]
    assert (px,pz)==(0.,0.) and x["size"]==50 and py==-50
    assert x["mass"]==0 and x["parent"]==0
    assert floor[0]["stable_id"]==2
    assert len(entities)==21,(len(entities),"expected 45->21")
    assert sum(x["name"].startswith("Target ") for x in entities)==5
    assert sum(x["name"]=="SkeletalMesh" for x in entities)==1
    assert sum(x["name"]=="Demo Pawn" for x in entities)==1
    assert validate_nef8_scene(data)==len(records)
    print("PASS single 50x50m static floor, contiguous top and unchanged gameplay nodes")

if __name__=="__main__":main()
