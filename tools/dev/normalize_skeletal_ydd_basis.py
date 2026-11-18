"""Offline canonical YDD basis migration: rotate source mesh into YMT space.

Preserves YMT, YCD, UV, all eight skin weights, triangles and materials.
Never replaces a source asset: output must not exist.
"""
import argparse, json, math, struct, zlib, hashlib, sys
from pathlib import Path
from blake3 import blake3


def check(ok,msg):
    if not ok:raise ValueError(msg)


def read(path):
    data=path.read_bytes()
    check(len(data)>64 and data[:4]==b"NEF8" and data[4]==2
          and data[5] in (6,8) and struct.unpack_from("<H",data,6)[0]==2,
          "Expected native skinned NEF8 YDD v2")
    flags=struct.unpack_from("<H",data,8)[0]
    check(flags&1==1 and flags&2==0,"Unexpected NEF8 flags or metadata")
    compressed,expanded=struct.unpack_from("<QQ",data,16)
    check(len(data)==64+compressed and 0<expanded<=64*1024*1024,
          "Invalid NEF8 body ranges")
    raw=zlib.decompress(data[64:],-15)
    check(len(raw)==expanded and (not flags&4 or blake3(raw).digest()==data[32:64]),
          "NEF8 body checksum mismatch")
    check(struct.unpack_from("<III",raw,0)==(4,1,40),
          "Unsupported YDD drawable version")
    return bytearray(data[:64]),bytearray(raw)


def rotate(vec,r):
    return tuple(sum(vec[k]*r[k][j] for k in range(3)) for j in range(3))


def bounds(src,dst,offset,r):
    lo=struct.unpack_from("<3f",src,offset)
    hi=struct.unpack_from("<3f",src,offset+12)
    check(all(math.isfinite(n) for n in lo+hi)
          and all(a<=b for a,b in zip(lo,hi)),"Invalid mesh bounds")
    pts=[rotate((x,y,z),r) for x in (lo[0],hi[0])
         for y in (lo[1],hi[1]) for z in (lo[2],hi[2])]
    struct.pack_into("<6f",dst,offset,
        *(min(p[i] for p in pts) for i in range(3)),
        *(max(p[i] for p in pts) for i in range(3)))


def convert(source):
    header,old=read(source)
    raw=bytearray(old)
    geom=struct.unpack_from("<Q",raw,32)[0]
    check(120<=geom<=len(raw)-72,"Invalid YDD geometry offset")
    m=[struct.unpack_from("<4f",raw,geom+8+16*i) for i in range(4)]
    r=[row[:3] for row in m[:3]]
    check(all(abs(m[i][3])<1e-6 and abs(m[3][i])<1e-6
              for i in range(3)) and abs(m[3][3]-1)<1e-6,
          "Only rigid YDD mesh/skeleton bases supported")
    for i in range(3):
        for j in range(3):
            dot=sum(r[i][k]*r[j][k] for k in range(3))
            check(abs(dot-(1 if i==j else 0))<1e-4,"Nonorthogonal asset basis")
    det=(r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])-
         r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+
         r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]))
    check(abs(det-1)<1e-4,"Reflected basis requires index winding migration")
    check(any(abs(r[i][j]-(1 if i==j else 0))>1e-4
              for i in range(3) for j in range(3)),"Already canonical")
    count,version=struct.unpack_from("<II",raw,geom)
    check(0<count<=128 and version==1,"Invalid YDD parts")
    bounds(old,raw,76,r)
    cursor=geom+72
    verts=indices_total=0
    for _ in range(count):
        check(cursor+48<=len(raw),"Truncated part")
        n,k=struct.unpack_from("<II",raw,cursor+8)
        fmt,stride=struct.unpack_from("<II",raw,cursor+40)
        end=cursor+48+n*80+k*4
        check(n>0 and k>0 and k%3==0 and fmt==1 and stride==48
              and end<=len(raw),"Invalid YDD part buffers")
        bounds(old,raw,cursor+16,r)
        base=cursor+48
        for v in range(n):
            at=base+v*32
            for offset in (0,12):
                vec=struct.unpack_from("<3f",old,at+offset)
                check(all(math.isfinite(x) for x in vec),"Nonfinite vertex")
                struct.pack_into("<3f",raw,at+offset,*rotate(vec,r))
        cursor=end;verts+=n;indices_total+=k
    check(cursor==len(raw) and
          (verts,indices_total)==struct.unpack_from("<II",raw,60),
          "YDD topology mismatch")
    for i in range(4):
        for j in range(4):
            struct.pack_into("<f",raw,geom+8+16*i+4*j,
                             1.0 if i==j else 0.0)
    encoder=zlib.compressobj(6,wbits=-15)
    encoded=encoder.compress(raw)+encoder.flush()
    struct.pack_into("<H",header,8,struct.unpack_from("<H",header,8)[0]|4)
    struct.pack_into("<QQ",header,16,len(encoded),len(raw))
    header[32:64]=blake3(raw).digest()
    blob=bytes(header)+encoded
    return blob,{"parts":count,"vertices":verts,"indices":indices_total,
                 "source_basis":r,"sha256":hashlib.sha256(blob).hexdigest()}


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("source",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    try:
        check(args.source.resolve()!=args.output.resolve(),"Source must be preserved")
        check(not args.output.exists(),"Refusing to overwrite output")
        blob,report=convert(args.source)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_bytes(blob)
        read(args.output)  # The same BLAKE3 + DEFLATE contract as C++.
        print(json.dumps(report))
    except Exception as ex:
        print("YDD conversion ERROR:",ex,file=sys.stderr)
        raise SystemExit(2)
