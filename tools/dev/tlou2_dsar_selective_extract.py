#!/usr/bin/env python3
"""Read-only, selective DSAR/PSARC extractor for local TLOU2R animation research.

Never modifies source archives. DSAR LZ4 chunks are decompressed on demand;
PSARC data entries are unpacked only when selected. Does not defeat encryption.
Requires "lz4" (local vendor copy under out/tlou2_native/.deps is supported).
"""
from __future__ import annotations

import argparse
import bisect
from collections import OrderedDict
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import zlib


def lz4_codec():
    try:
        import lz4.block
        return lz4.block
    except ImportError:
        root = Path(__file__).resolve().parents[2]
        vendor = root / "out" / "tlou2_native" / ".deps"
        sys.path.insert(0, str(vendor))
        import lz4.block
        return lz4.block


class DsarStream(io.RawIOBase):
    def __init__(self, path: Path):
        self.file = path.open("rb")
        header = self.file.read(32)
        if len(header) != 32:
            raise ValueError("Truncated DSAR header")
        magic, maj, minor, count, first_off, total, padding = (
            struct.unpack("<4sHHIIQ8s", header))
        if magic != b"DSAR" or not (1 <= count <= 1_000_000):
            raise ValueError("Unsupported/non-DSAR archive")
        self.total = total
        self.entries = []
        self.offsets = []
        for i in range(count):
            row = self.file.read(32)
            if len(row) != 32:
                raise ValueError(f"Truncated DSAR chunk {i}")
            uoff, coff, usz, csz, flag = struct.unpack("<QQIIB", row[:25])
            if i and uoff < self.offsets[-1]:
                raise ValueError("Unsorted DSAR chunks")
            self.entries.append((uoff, coff, usz, csz, flag))
            self.offsets.append(uoff)
        self.position = 0
        self.cache: OrderedDict[int,bytes] = OrderedDict()
        self.codec = lz4_codec()
        self.path = path

    def readable(self):
        return True

    def seekable(self):
        return True

    def seek(self, offset, whence=io.SEEK_SET):
        if whence == io.SEEK_CUR:
            offset += self.position
        elif whence == io.SEEK_END:
            offset += self.total
        elif whence != io.SEEK_SET:
            raise ValueError("Unsupported seek mode")
        if offset < 0 or offset > self.total:
            raise ValueError(f"Seek outside DSAR {offset}/{self.total}")
        self.position = offset
        return self.position

    def tell(self):
        return self.position

    def _load(self, idx):
        if idx in self.cache:
            self.cache.move_to_end(idx)
            return self.cache[idx]
        uoff, coff, usz, csz, flag = self.entries[idx]
        if usz > 1024*1024:
            raise ValueError("Oversized DSAR chunk")
        if flag == 254 or csz == 0:
            result = bytes(usz)
        else:
            self.file.seek(coff)
            raw = self.file.read(csz)
            if len(raw) != csz:
                raise IOError("Truncated compressed DSAR chunk")
            if flag == 3:
                result = self.codec.decompress(raw, uncompressed_size=usz)
            elif flag == 0:
                result = raw
            else:
                raise ValueError(f"Unsupported DSAR compression flag {flag}")
            if len(result) != usz:
                raise ValueError("DSAR decompressed size mismatch")
        self.cache[idx] = result
        if len(self.cache) > 16:
            self.cache.popitem(last=False)
        return result

    def read(self, count=-1):
        if count is None or count < 0:
            count = self.total - self.position
        count = min(count, self.total - self.position)
        result = bytearray()
        while count:
            idx = bisect.bisect_right(self.offsets, self.position) - 1
            if idx < 0:
                raise IOError("DSAR chunk gap")
            uoff, _, usz, _, _ = self.entries[idx]
            inner = self.position - uoff
            if inner >= usz:
                raise IOError(f"Invalid DSAR chunk span at {self.position}")
            block = self._load(idx)
            take = min(count, usz-inner)
            result.extend(block[inner:inner+take])
            self.position += take
            count -= take
        return bytes(result)

    def close(self):
        self.file.close()
        super().close()


class PsarcIndex:
    def __init__(self, archive: Path):
        self.archive = archive
        self.stream = DsarStream(archive)
        header = self.stream.read(32)
        magic, major, minor, algorithm, toc_size, entry_size, n_entries, block_size, flags = (
            struct.unpack(">4sHH4sIIIII",header))
        if magic != b"PSAR" or not (30 <= entry_size <= 256):
            raise ValueError("Unsupported inner PSARC header")
        if n_entries > 2_000_000 or not 4096 <= block_size <= 1024*1024:
            raise ValueError("Unreasonable inner PSARC table size")
        self.entries = []
        for _ in range(n_entries):
            data=self.stream.read(entry_size)
            self.entries.append({
                "hash": data[:16].hex(),
                "zindex": struct.unpack(">I",data[16:20])[0],
                "size": int.from_bytes(data[20:25],"big"),
                "offset": int.from_bytes(data[25:30],"big"),
            })
        remaining = toc_size - 32 - n_entries*entry_size
        if remaining < 0 or remaining > 100*1024*1024 or remaining % 2:
            raise ValueError("Malformed PSARC chunk length table")
        zbytes=self.stream.read(remaining)
        self.zsizes=struct.unpack(">"+str(remaining//2)+"H",zbytes) if zbytes else ()
        self.block_size=block_size
        self.algorithm=algorithm.decode("ascii","replace").strip("\0")
        self.path_by_hash={}
        self.names=[]
        raw=self.read_entry(0)
        self.names=[s.decode("utf8","replace") for s in raw.split(b"\0") if s]
        for s in self.names:
            self.path_by_hash[hashlib.md5(s.encode("utf8")).hexdigest()]=s
        self.resolved=[]
        for i,e in enumerate(self.entries):
            name=self.path_by_hash.get(e["hash"])
            if not name:
                name=f"_unknown/{i:07d}_{e['hash']}.bin"
            self.resolved.append(name)

    def read_entry(self, idx, max_bytes=512*1024*1024):
        e=self.entries[idx]
        if e["size"] > max_bytes:
            raise ValueError(f"PSARC entry size {e['size']} exceeds limit {max_bytes}")
        self.stream.seek(e["offset"])
        remaining=e["size"]
        blockid=e["zindex"]
        output=bytearray()
        while remaining:
            wanted=min(self.block_size,remaining)
            if blockid >= len(self.zsizes):
                raise ValueError("PSARC missing compressed block lengths")
            stored_size=self.zsizes[blockid] or wanted
            raw=self.stream.read(stored_size)
            if stored_size == wanted:
                block=raw
            else:
                if self.algorithm.lower() != "zlib":
                    raise ValueError(f"Unsupported inner PSARC compression {self.algorithm}")
                block=zlib.decompress(raw)
            if len(block)!=wanted:
                raise ValueError(f"PSARC decoded block {blockid}: {len(block)} != {wanted}")
            output.extend(block)
            remaining-=wanted
            blockid+=1
        return bytes(output)

    def select(self, paths):
        normalized={x.casefold().lstrip("/").replace("\\","/") for x in paths}
        return [(i,name) for i,name in enumerate(self.resolved)
                if name.casefold().lstrip("/").replace("\\","/") in normalized]

    def extract(self, items, destination: Path, max_bytes=256*1024*1024):
        output=[]
        for idx, name in items:
            safe=Path(name.lstrip("/").replace("\\","/"))
            if safe.is_absolute() or ".." in safe.parts or not safe.parts:
                raise ValueError("Unsafe archive filename")
            data=self.read_entry(idx,max_bytes)
            target=destination / safe
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(data)
            output.append({
                "path": name,
                "bytes":len(data),
                "sha256":hashlib.sha256(data).hexdigest(),
                "destination":str(target)
            })
        return output

    def close(self):
        self.stream.close()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("archives",nargs="+",type=Path)
    parser.add_argument("--list",action="store_true")
    parser.add_argument("--find",action="append",default=[])
    parser.add_argument("--paths-json",type=Path)
    parser.add_argument("--out",type=Path)
    parser.add_argument("--max-mb",type=int,default=256)
    parser.add_argument("--report",type=Path)
    args=parser.parse_args()
    target=set(args.find)
    if args.paths_json:
        data=json.loads(args.paths_json.read_text("utf8"))
        target.update(data["paths"] if isinstance(data,dict) else data)
    report={"archives":[],"selected":[],"errors":[]}
    for archive in args.archives:
        try:
            idx=PsarcIndex(archive)
            matches=[(i,name) for i,name in enumerate(idx.resolved)
                     if name.casefold().lstrip("/").replace("\\","/")
                     in {x.casefold().lstrip("/").replace("\\","/") for x in target}]
            print("INDEX",archive.name,"entries",len(idx.entries),
                  "manifest",len(idx.names),"resolved",
                  sum(not x.startswith("_unknown/") for x in idx.resolved),
                  "matches",len(matches),flush=True)
            report["archives"].append({"archive":str(archive),"entries":len(idx.entries),
                                       "manifest_names":len(idx.names),
                                       "matches":[x for _,x in matches]})
            if args.list:
                for item in idx.resolved[:25]:print("  LIST",item)
            if matches and args.out:
                path=args.out/archive.stem
                report["selected"].extend(idx.extract(matches,path,args.max_mb*1024*1024))
            idx.close()
        except Exception as ex:
            print("FAILED",archive.name,type(ex).__name__,str(ex),flush=True)
            report["errors"].append({"archive":str(archive),"error":repr(ex)})
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2),"utf8")
    return int(bool(report["errors"]))


if __name__=="__main__":
    raise SystemExit(main())
