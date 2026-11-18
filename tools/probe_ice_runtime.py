#!/usr/bin/env python3
from pathlib import Path
import struct
root=Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\animations")
for bank in ["anim-ellie-player-movement","anim-ellie-player-mm-explore","anim-ellie-player-movement-combat","anim-ellie-player-bindpose"]:
    folder=root/"ellie"/bank
    files=sorted(folder.glob("*.iceanim"))
    if not files: continue
    for p in files[:2]:
        data=p.read_bytes()
        print("FILE",p.name,"bytes",len(data))
        for off in range(0,160,16):
            ints=struct.unpack_from("<4I",data,off)
            print(f"{off:03X}",' '.join(f"{x:08x}" for x in ints))
for p in sorted((root/"isaac"/"cinematic_streams").rglob("*.stm"))[:1]:
    data=p.read_bytes()
    print("STM",p.name,"bytes",len(data))
    for off in range(0,160,16):
        ints=struct.unpack_from("<4I",data,off)
        print(f"{off:03X}",' '.join(f"{x:08x}" for x in ints))
