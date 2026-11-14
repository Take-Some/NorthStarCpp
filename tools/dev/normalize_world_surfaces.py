#!/usr/bin/env python3
"""Repair malformed native NEF8/NETD mip indexes without touching source YTD.

Source bytes are BLAKE3-verified before reading. Only contiguous, range-valid
mip levels are copied. Missing/truncated tail mips are deliberately dropped.
Exports a strict native NETD v2 ListFile understood by engine.assets.
"""
from __future__ import annotations
import argparse
import json
import struct
import zlib
from pathlib import Path
from blake3 import blake3

def u(blob: bytes, off: int, n: int=4) -> int:
    if off<0 or off+n>len(blob):raise ValueError("Out-of-bounds index")
    return int.from_bytes(blob[off:off+n],"little")

def put(blob: bytearray, off: int, value: int, n: int=4) -> None:
    blob[off:off+n] = value.to_bytes(n,"little")

def align16(n: int) -> int:
    return (n+15)&~15

def load_input(path: Path) -> tuple[bytes,bytes]:
    source=path.read_bytes()
    if not (128<len(source)<64*1024*1024 and source[:4]==b"NEF8"
            and u(source,4,2)==1 and u(source,6,2)==128
            and u(source,8,2)==1 and u(source,10,2)==1
            and u(source,12,2)==1 and u(source,112,8)==1
            and u(source,16,8)==128 and u(source,24,8)==len(source)-128
            and u(source,48,8)==0 and u(source,56,8)==0):
        raise ValueError("Unexpected NEF8/YTD envelope")
    unzip=zlib.decompressobj(-15)
    expanded=u(source,32,8)
    if expanded<64 or expanded>64*1024*1024:raise ValueError("Unsafe expanded size")
    body=unzip.decompress(source[128:],expanded+1)
    body+=unzip.flush()
    if not unzip.eof or unzip.unconsumed_tail or unzip.unused_data:
        raise ValueError("Malformed deflate stream")
    if len(body)!=expanded or blake3(body).digest()!=source[64:96]:
        raise ValueError("Original NEF8 body checksum mismatch")
    return source,body

def extract(body: bytes) -> tuple[list[dict],list[str]]:
    if body[:4]!=b"NETD" or u(body,4)!=2 or u(body,8)!=64:
        raise ValueError("Expected NETD 2")
    total=u(body,12)
    table=u(body,16,8);table_size=u(body,24,8)
    payload=u(body,32,8);size=u(body,40,8)
    if not (1<=total<=10000 and table>=64
            and table+table_size<=payload and payload+size==len(body)):
        raise ValueError("NETD dictionary index out of bounds")
    if body[table:table+4]!=b"NTDX" or u(body,table+4,2)!=1 or u(body,table+6,2)!=64:
        raise ValueError("Unsupported NTDX")
    eb=u(body,table+20);mb=u(body,table+24);nb=u(body,table+28)
    ns=u(body,table+32)
    if not (eb>=40 and eb+64*total<=mb
            and nb>=mb and nb+ns<=table_size):
        raise ValueError("Invalid NETD table layout")
    items=[]
    issues=[]
    for i in range(total):
        entry=table+eb+i*64
        name_offset=u(body,entry+24);name_len=u(body,entry+28,2)
        if name_offset+name_len>ns:raise ValueError("Malformed name entry")
        name=body[table+nb+name_offset:table+nb+name_offset+name_len].decode("utf-8")
        if not name or any(ch in name for ch in "@/\\:") or name in [x["name"] for x in items]:
            raise ValueError("Unsafe or duplicate texture name")
        fmt=u(body,entry+30,2)
        if fmt not in (101,102,103,104,105):raise ValueError("Unsupported NETD format: "+str(fmt))
        width=u(body,entry+32);height=u(body,entry+36)
        count=u(body,entry+44);first=u(body,entry+40)
        color=u(body,entry+48)
        if not (0<width<=16384 and 0<height<=16384 and 0<count<=15
                and first+count<=u(body,table+16) and color in (1,2)):
            raise ValueError("Invalid texture geometry or color space")
        mips=[]
        w,h=width,height
        first_start=u(body,table+mb+first*32,8)
        if i+1<total:
            following=table+eb+(i+1)*64
            next_first=u(body,table+mb+u(body,following+40)*32,8)
        else:
            next_first=size
        expected_next=first_start
        for j in range(count):
            mip=table+mb+(first+j)*32
            src=u(body,mip,8);length=u(body,mip+8,8)
            stride=((w+3)//4)*((h+3)//4)*(8 if fmt in (101,102) else 16)
            valid=(u(body,mip+16)==w and u(body,mip+20)==h and
                   u(body,mip+24)==j and u(body,mip+28)==0 and
                   length==stride and src>=expected_next and
                   src-expected_next<=15 and src<=size and
                   length<=size-src and src+length<=next_first)
            if not valid:
                issues.append(f"{name}: dropped {count-j} damaged tail mip(s)")
                break
            mips.append((w,h,body[payload+src:payload+src+length]))
            expected_next=src+length
            w=max(1,w//2);h=max(1,h//2)
        if not mips:raise ValueError("No valid texture mips: "+name)
        if (u(body,entry+8,8)!=first_start or
                u(body,entry+16,8)!=(next_first-first_start)):
            issues.append(f"{name}: rebuilt incorrect texture byte span")
        items.append({
            "name":name,"hash":u(body,entry,8),"format":fmt,
            "color":color,"width":width,"height":height,
            "mips":mips})
    return items,issues

def pack(items: list[dict]) -> bytes:
    names=b"".join(item["name"].encode("utf-8") for item in items)
    count=len(items)
    n_mips=sum(len(x["mips"]) for x in items)
    eb=40;mb=eb+64*count;nb=mb+32*n_mips
    table=bytearray(nb+len(names))
    table[:4]=b"NTDX"
    put(table,4,1,2);put(table,6,64,2);put(table,8,32)
    put(table,12,count);put(table,16,n_mips)
    put(table,20,eb);put(table,24,mb)
    put(table,28,nb);put(table,32,len(names))
    table[nb:nb+len(names)]=names
    raw=bytearray();mipindex=0;namepos=0
    for i,item in enumerate(items):
        off=eb+64*i
        begin=len(raw)
        put(table,off,item["hash"],8)
        put(table,off+8,begin,8)
        put(table,off+24,namepos)
        put(table,off+28,len(item["name"].encode("utf-8")),2)
        put(table,off+30,item["format"],2)
        put(table,off+32,item["width"]);put(table,off+36,item["height"])
        put(table,off+40,mipindex)
        put(table,off+44,len(item["mips"]))
        put(table,off+48,item["color"])
        namepos+=len(item["name"].encode("utf-8"))
        for j,(width,height,chunk) in enumerate(item["mips"]):
            mip=mb+mipindex*32
            put(table,mip,len(raw),8)
            put(table,mip+8,len(chunk),8)
            put(table,mip+16,width);put(table,mip+20,height)
            put(table,mip+24,j)
            raw+=chunk;mipindex+=1
        put(table,off+16,len(raw)-begin,8)
    header=bytearray(64);header[:4]=b"NETD"
    put(header,4,2);put(header,8,64);put(header,12,count)
    put(header,16,64,8);put(header,24,len(table),8)
    offset=align16(64+len(table))
    put(header,32,offset,8);put(header,40,len(raw),8)
    return bytes(header)+bytes(table)+b"\x00"*(offset-64-len(table))+bytes(raw)

def normalize(source_path: Path,out_path: Path) -> dict:
    source,body=load_input(source_path)
    items,issues=extract(body)
    clean=pack(items)
    compressor=zlib.compressobj(6,zlib.DEFLATED,-15)
    compressed=compressor.compress(clean)+compressor.flush()
    header=bytearray(source[:128])
    put(header,24,len(compressed),8)
    put(header,32,len(clean),8)
    header[64:96]=blake3(clean).digest()
    output=bytes(header)+compressed
    if out_path.resolve()==source_path.resolve():
        raise ValueError("Refusing to overwrite original YTD")
    out_path.parent.mkdir(parents=True,exist_ok=True)
    out_path.write_bytes(output)
    checkbody=load_input(out_path)[1]
    if checkbody!=clean:raise ValueError("Output verification failed")
    return {"source":str(source_path),"output":str(out_path),"source_bytes":len(source),
            "output_bytes":len(output),"textures":[
                {"name":x["name"],"format":x["format"],"mips":len(x["mips"]),
                 "color_space":"sRGB" if x["color"]==2 else "linear"}
                for x in items],"repairs":issues}

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("source",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    report=normalize(args.source,args.output)
    print(json.dumps(report,indent=2))
