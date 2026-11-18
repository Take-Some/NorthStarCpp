#!/usr/bin/env python3
"""Inspect imported NPC rigs and runtime animation/sky component references."""
from pathlib import Path
import json
import struct
import xml.etree.ElementTree as ET
import zlib
from tools.dev.scene_binary import read_nef8_scene_records
from tools.dev.install_nightcity_npcs import SCENE,ROOT,name_of,encode_component
from tools.dev.enable_nightcity_npc_animation import TYPE,VALUE
def bone_names(slug):
    source=ROOT/"engine/northstarcpp/resources/sharedAssets/characters"/slug/"models"/(slug+".ymt")
    data=source.read_bytes()
    assert data[:4]==b"NEF8"
    compressed=struct.unpack_from("<Q",data,16)[0]
    doc=ET.fromstring(zlib.decompress(data[-compressed:],-15))
    return [joint.attrib["name"] for joint in doc.findall("./Skeleton/Joint")]
def main():
    records=read_nef8_scene_records(SCENE.read_bytes())
    ids={name_of(raw):eid for kind,eid,raw in records if kind==2}
    for name,slug in (("NPC Ellie","ellie"),("NPC Isaac","isaac")):
        names=bone_names(slug)
        joints=[x for x in names if any(key in x.lower() for key in
            ["shoulder","elbow","hip","knee","thigh","head","spine","calf","wrist"])]
        print("RIG",slug,"total=",len(names),"core=",names[:115],"humanoid=",joints[:35])
        assert len(names)>250 and len(joints)>10,slug
        eid=ids[name]
        assert sum(kind==7 and entity==eid and body==encode_component(TYPE,1,VALUE)
                   for kind,entity,body in records)==1,name
    sky=json.loads((SCENE.parent/"main.nscn.sky.json").read_text("utf-8"))["sky"]
    assert sky["mode"]=="procedural" and sky["sun"] and sky["clouds"]
    print("PASS Ellie/Isaac YMT skeletons + authored procedural NPC components + sky profile")
if __name__=="__main__":main()
