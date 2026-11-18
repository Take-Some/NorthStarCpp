#!/usr/bin/env python3
from pathlib import Path
from native_pak import NaughtyDogPak
import struct,collections
root=Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\archives\sp-common\actor97")
for f in ["anim-ellie-player-movement.pak","anim-ellie-player-mm-explore.pak","anim-ellie-player-bindpose.pak"]:
 pak=NaughtyDogPak(root/f)
 animations=pak.typed("ANIM")
 raw=pak.typed("ANIM_RAWCHANNELS")
 print("BANK",f,"ANIM",len(animations),"RAW",len(raw))
 for anim in animations[:3]:
  base=anim["offset"]
  payload=pak.pointer(base+56)
  cha=pak.pointer(base+64)
  print("ANIM",anim["name"],"base",hex(base),"payload",hex(payload),"raw",hex(cha))
  for name,o in [("anim",base),("rawchannel",cha)]:
   data=pak.data[o:o+160]
   print("STRUCT",name,"bytes",data.hex(" ",4))
   for j in range(0,96,16):
    print(f"{j:02x}",[hex(v) for v in struct.unpack_from("<4I",data,j)])
  print("RAW ENTRY NAMES",[(x["name"],hex(x["offset"])) for x in raw[:2]])
  break
