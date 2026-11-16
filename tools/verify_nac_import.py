#!/usr/bin/env python3
"""Independent NEF8/NAC PCM check against WAV files, no legacy YSCD."""
import json
import pathlib
import struct
import sys
import wave
import zlib

HEADER = struct.Struct("<4sHHHHHHQQQQQQ")

def stable_id(name: str) -> int:
    h=2166136261
    for b in name.encode("utf-8"):
        h=((h^b)*16777619)&0xffffffff
    return h

def main(source: pathlib.Path, archive: pathlib.Path) -> None:
    blob=archive.read_bytes()
    assert len(blob)>=128 and blob[:4]==b"NEF8", "Not NEF8"
    (magic,major,hsize,kind,compression,schema,flags,offset,compressed,
     expanded,count,meta_offset,meta_size)=HEADER.unpack_from(blob)
    assert (major,hsize,kind,compression,schema,flags)==(1,128,12,1,1,0)
    assert 1<=count<=1024 and 0<expanded<=64*1024*1024
    assert meta_offset==128 and 0<meta_size<=8*1024*1024
    assert offset==128+meta_size and offset+compressed==len(blob)
    assert struct.unpack_from("<Q",blob,112)[0]==1
    index=json.loads(blob[128:offset].decode("utf-8"))
    assert index["schema"]=="northstar.nac.nef8.v1"
    assert len(index["tracks"])==count
    # Native engine checks the outer BLAKE3; stdlib decoder checks the
    # raw DEFLATE stream, expanded length and each independent PCM CRC32.
    raw=zlib.decompress(blob[offset:],wbits=-15)
    assert len(raw)==expanded, "NEF8 expanded size mismatch"
    names=set()
    ids=set()
    previous=0
    for entry in index["tracks"]:
        name=str(entry["name"])
        tid=int(entry["id"])
        rate=int(entry["sample_rate"])
        ch=int(entry["channels"])
        frames=int(entry["frames"])
        start=int(entry["offset"])
        length=int(entry["bytes"])
        crc=int(entry["crc32"])
        assert name not in names and tid not in ids and stable_id(name)==tid
        assert int(entry["codec"])==1 and int(entry["looping"])==0
        assert int(entry["loop_start"])==int(entry["loop_end"])==0
        assert ch in (1,2) and 8000<=rate<=192000
        assert start==(previous+15)//16*16
        assert length==frames*ch*2 and start+length<=len(raw)
        pcm=raw[start:start+length]
        assert zlib.crc32(pcm)==crc, name
        with wave.open(str(source/(name+".wav")),"rb") as source_wav:
            assert (source_wav.getcomptype(),source_wav.getsampwidth(),
                    source_wav.getframerate(),source_wav.getnchannels(),
                    source_wav.getnframes())==("NONE",2,rate,ch,frames)
            assert source_wav.readframes(frames)==pcm, name
        previous=start+length
        names.add(name)
        ids.add(tid)
    assert previous==len(raw)
    expected={p.relative_to(source).with_suffix("").as_posix()
              for p in source.rglob("*.wav")}
    assert names==expected, "Missing/extra audio tracks"
    print(f"VERIFIED NEF8/NAC tracks={count} bytes={len(blob)} PCM=exact CRC32=valid")

if __name__=="__main__":
    if len(sys.argv)!=3:
        sys.exit("Usage: verify_nac_import.py <wav-directory> <audio.nac>")
    main(pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2]))
