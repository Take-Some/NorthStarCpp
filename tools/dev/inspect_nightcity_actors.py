"""Inspect the authored KaylaNightCity NEF8 scene without modifying it."""
from pathlib import Path
import struct
from collections import Counter
from tools.dev.scene_binary import read_nef8_scene_records
from tools.dev.scene_binary_v2 import _chunks
root=Path(__file__).resolve().parents[2]
source=root/"Projects/KaylaNightCity/main.nscn"
data=source.read_bytes()
records=read_nef8_scene_records(data)
print("SCENE",source,"bytes",len(data),"records",len(records),"kinds",dict(Counter(k for k,_,_ in records)))
print("CHUNKS",[(c[0],c[1]) for c in _chunks(data)[0]])
def read_str(blob,off):
    size=struct.unpack_from("<H",blob,off)[0]
    return blob[off+2:off+2+size].decode("utf8"),off+2+size
for kind,eid,blob in records:
    if kind==2:
        name,off=read_str(blob,0)
        xyz=struct.unpack_from("<fff",blob,off);off+=12
        size,color,legacy_owned,mass,uid,parent=struct.unpack_from("<fIBfQQ",blob,off);off+=29
        strings=[]
        for i in range(5):
            s,off=read_str(blob,off);strings.append(s)
        print("ENTITY",eid,name,"xyz",tuple(round(x,2) for x in xyz),"size",round(size,2),"mass",round(mass,2),"parent",parent,"tags",strings,"tailbytes",len(blob)-off)
    if kind==7:
        key,off=read_str(blob,0)
        if key=="northstar.physics.collider":
            ver,length=struct.unpack_from("<II",blob,off)
            print("COMPONENT",eid,key,"version",ver,"raw",blob[off+8:off+8+length].decode("utf8",errors="replace"))
        else:
            print("COMPONENT",eid,key,"payloadsize",len(blob)-off)
