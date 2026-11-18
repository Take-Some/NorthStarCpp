#!/usr/bin/env python3
"""Convert renderer GPU PPM readbacks to compact PNG previews without Pillow."""
from pathlib import Path
import argparse,struct,zlib

def chunk(tag, data):
    return struct.pack(">I",len(data))+tag+data+struct.pack(
        ">I",zlib.crc32(tag+data)&0xffffffff)

def convert(source:Path,dest:Path,shrink:int=2,crop=None,zoom:int=1):
    data=source.read_bytes()
    magic,size,level,rgb=data.split(b"\n",3)
    if magic!=b"P6" or level!=b"255":
        raise ValueError("Not P6 RGB")
    w,h=map(int,size.split(b" "))
    if len(rgb)!=w*h*3:
        raise ValueError("Invalid RGB payload length")
    x0,y0,cw,ch=crop or (0,0,w,h)
    if x0<0 or y0<0 or x0+cw>w or y0+ch>h:
        raise ValueError("Crop outside PPM")
    nw,nh=(cw//shrink)*zoom,(ch//shrink)*zoom
    rows=bytearray()
    for y in range(nh):
        rows.append(0)
        for x in range(nw):
            o=((y0+(y//zoom)*shrink)*w+
                x0+(x//zoom)*shrink)*3
            rows.extend(rgb[o:o+3])
    png=(b"\x89PNG\r\n\x1a\n"+
         chunk(b"IHDR",struct.pack(">IIBBBBB",nw,nh,8,2,0,0,0))+
         chunk(b"IDAT",zlib.compress(bytes(rows),9))+
         chunk(b"IEND",b""))
    dest.write_bytes(png)
    print(dest,len(png))
if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("src",type=Path);p.add_argument("out",type=Path)
    p.add_argument("--shrink",type=int,default=2)
    p.add_argument("--crop",type=int,nargs=4)
    p.add_argument("--zoom",type=int,default=1)
    a=p.parse_args()
    convert(a.src,a.out,a.shrink,a.crop,a.zoom)
