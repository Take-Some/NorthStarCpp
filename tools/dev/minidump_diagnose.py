"""Read only summary of a Windows minidump (modules, exception, stacks).
Standalone stdlib; useful when cdb/WinDbg is not installed.
"""
from __future__ import annotations
import struct
import sys
from pathlib import Path

def parse(p):
 b=Path(p).read_bytes()
 u32=lambda o:struct.unpack_from('<I',b,o)[0]
 u64=lambda o:struct.unpack_from('<Q',b,o)[0]
 if b[:4]!=b'MDMP':raise ValueError('Missing MDMP signature')
 count=u32(8);directory=u32(12)
 print('MINIDUMP',len(b),'streams',count)
 streams={}
 for i in range(count):
  kind,size,rva=struct.unpack_from('<III',b,directory+i*12)
  streams[kind]=(rva,size)
 print('STREAMS',streams)
 mods=[]
 if 4 in streams:
  off,size=streams[4]
  for i in range(u32(off)):
   m=off+4+i*108
   base=u64(m);extent=u32(m+8);timestamp=u32(m+16)
   sn=u32(m+20);length=u32(sn)
   name=b[sn+4:sn+4+length].decode('utf-16le','replace')
   mods.append((base,base+extent,name,timestamp))
 print('MODULES:')
 for base,end,name,stamp in mods: print(f' {base:016X}-{end:016X} {name} ts={stamp:08x}')
 def module(addr):
  for a,z,name,_ in mods:
   if a<=addr<z:return name,addr-a
  return '<unknown>',-1
 if 6 in streams:
  o,n=streams[6]
  tid=u32(o);context_size=u32(o+160);context_rva=u32(o+164)
  code=u32(o+8);flags=u32(o+12);addr=u64(o+24);nparams=u32(o+32)
  details=[u64(o+40+8*i) for i in range(min(nparams,15))]
  print('EXCEPTION',hex(code),'flags',hex(flags),'tid',tid,'address',hex(addr),'details',[hex(x) for x in details])
  print(' AT',module(addr),'context',context_rva,context_size)
  if context_size>264:
   rax=u64(context_rva+0x80);rcx=u64(context_rva+0x88);rdx=u64(context_rva+0x90)
   rsp=u64(context_rva+0xa0);rbp=u64(context_rva+0xa8);rip=u64(context_rva+0x100)
   print(' REGISTERS RIP',hex(rip),'RSP',hex(rsp),'RBP',hex(rbp),'RAX',hex(rax),'RCX',hex(rcx),'RDX',hex(rdx))
 if 3 in streams:
  o,n=streams[3]
  print('THREADS count',u32(o))
  for i in range(u32(o)):
   r=o+4+i*48
   tid=u32(r);start=u64(r+24);lenstack=u32(r+32);rva=u32(r+36)
   print(' THREAD',tid,'stack',hex(start),lenstack,'rva',rva)
   if 6 in streams and tid==u32(streams[6][0]):
    if lenstack:
     print('  STACK hits:')
     printed=0
     for index in range(0,min(lenstack,20000)-7,8):
      val=u64(rva+index)
      path,delta=module(val)
      if delta>=0:
       print(f'  {start+index:016X} -> {val:016X} +{delta:08X} {path}')
       printed+=1
       if printed>95:break
if __name__=='__main__':
 parse(sys.argv[1])
