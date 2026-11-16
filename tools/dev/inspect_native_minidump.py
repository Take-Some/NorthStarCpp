#!/usr/bin/env python3
"""Inspect local Windows minidump thread stacks without copying private dumps.

Safe diagnostics: emits module/stack addresses and optional symbols only, does
not export raw stack memory or environment variables. Works for minidumps made
with MiniDumpWithThreadInfo but no exception stream (SIGABRT).
"""
import argparse
import bisect
import pathlib
import re
import struct
import subprocess
from dataclasses import dataclass

def u32(data, off): return struct.unpack_from("<I",data,off)[0]
def u64(data, off): return struct.unpack_from("<Q",data,off)[0]

@dataclass
class Module:
    base: int
    size: int
    name: str

def wide_name(data, rva):
    if not rva or rva+4>len(data): return "?"
    length=u32(data,rva)
    if length>8192 or rva+4+length>len(data):return "?"
    return data[rva+4:rva+4+length].decode("utf-16-le","replace")

def parse(path,symbols_path=None):
    symbol_addresses=[]
    symbol_names=[]
    if symbols_path:
        raw_symbols=pathlib.Path(symbols_path).read_bytes()
        encoding="utf-16" if raw_symbols.startswith(bytes((255,254))) else "utf-8"
        for line in raw_symbols.decode(encoding,errors="replace").splitlines():
            fields=line.split(" ",2)
            if len(fields)==3 and fields[1] in ("T","t"):
                try: addr=int(fields[0],16)
                except ValueError: continue
                if addr>=0x140000000:
                    symbol_addresses.append(addr)
                    symbol_names.append(fields[2])
    data=path.read_bytes()
    if len(data)<32 or data[:4]!=b"MDMP":raise ValueError("Not a MINIDUMP")
    count=u32(data,8)
    table=u32(data,12)
    streams={}
    for i in range(count):
        typ,size,rva=struct.unpack_from("<III",data,table+i*12)
        if rva+size<=len(data):
            streams[typ]=(rva,size)
    print(f"dump_size={len(data)} streams={sorted(streams)} exception_stream={'yes' if 6 in streams else 'no'}")
    mods=[]
    if 4 in streams:
        rva,size=streams[4]
        n=u32(data,rva)
        for i in range(n):
            off=rva+4+i*108
            if off+108>len(data):break
            base=u64(data,off)
            length=u32(data,off+8)
            name=wide_name(data,u32(data,off+20))
            mods.append(Module(base,length,pathlib.PureWindowsPath(name).name))
    mods.sort(key=lambda m:m.base)
    print("modules="+", ".join(f"{m.name} @ 0x{m.base:x} size=0x{m.size:x}" for m in mods if (m.name.lower() in ("northstarcpp_player.exe","render_d3d12.dll","render_vulkan.dll","libstdc++-6.dll","ucrtbase.dll","kernelbase.dll") or "v8" in m.name.lower() or "scripting" in m.name.lower() or "northstar" in m.name.lower())))
    def loc(value):
        for mod in mods:
            if mod.base<=value<mod.base+mod.size:
                suffix=""
                if mod.name.lower()=="northstarcpp_player.exe" and symbol_addresses:
                    preferred=0x140000000+(value-mod.base)
                    index=bisect.bisect_right(symbol_addresses,preferred)-1
                    if index>=0:
                        suffix=f" [{symbol_names[index][:110]}+0x{preferred-symbol_addresses[index]:x}]"
                return f"{mod.name}+0x{value-mod.base:x}"+suffix,mod
        return None,None
    if 6 in streams:
        off,_=streams[6]
        if off+32<len(data):
            print(f"exception_tid={u32(data,off)} code=0x{u32(data,off+8):08x} address=0x{u64(data,off+24):x}")
    if 3 not in streams:return
    off,_=streams[3]
    n=u32(data,off)
    print(f"threads={n}")
    for i in range(n):
        t=off+4+i*48
        if t+48>len(data):break
        tid=u32(data,t)
        if tid not in (8484,4980):
            continue
        stack_base=u64(data,t+24)
        stack_len=u32(data,t+32)
        stack_rva=u32(data,t+36)
        context_len=u32(data,t+40)
        context_rva=u32(data,t+44)
        rip=rsp=0
        if context_len>=256 and context_rva+256<=len(data):
            rsp=u64(data,context_rva+152)
            rip=u64(data,context_rva+248)
        symbol,_=loc(rip)
        print(f"thread={tid} RIP=0x{rip:x} ({symbol}) RSP=0x{rsp:x} stack=0x{stack_base:x}+0x{stack_len:x}")
        if not (stack_len and stack_rva+stack_len<=len(data)):continue
        mem=data[stack_rva:stack_rva+stack_len]
        scan_start=max(0,rsp-stack_base) if rsp>=stack_base else 0
        if scan_start>len(mem):scan_start=0
        discovered=[]
        for pos in range(scan_start,min(len(mem)-7,scan_start+0x10000),8):
            ptr=u64(mem,pos)
            where,mod=loc(ptr)
            if where and (mod.name.lower() in ("northstarcpp_player.exe","scripting_v8_sidecar.dll","libstdc++-6.dll","ucrtbase.dll")) and pos-scan_start>=0x9e00:
                discovered.append((pos,ptr,where))
        print("  candidate return addresses (stack scan, NOT unwound backtrace):")
        for pos,ptr,where in discovered[:130]:
            print(f"    sp+0x{pos-scan_start:x} 0x{ptr:x} {where}")
        if len(discovered)>130:print(f"    ... {len(discovered)-130} more candidates")
    return mods

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("dump",type=pathlib.Path)
    parser.add_argument("--symbols",type=pathlib.Path)
    args=parser.parse_args()
    parse(args.dump,args.symbols)
