#!/usr/bin/env python3
"""Read authentic PC Remastered texturedict3 streaming archives by hash64.

Only reads bounded PAK header pages and targeted texture byte ranges: never
loads a multi-GiB dictionary into memory, never modifies the source game.

Header layout derived from verified Naughty Dog PAK page/fixup semantics and
the public MIT-licensed texture-dictionary implementation in
https://github.com/skelebone208/tlou-mod-installer/blob/main/texpatch.go .
"""
from __future__ import annotations
import argparse,collections,json,struct,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
GAME_DICTIONARIES=Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\archives")
ELLIE=ROOT/"engine/northstarcpp/resources/sharedAssets/characters/ellie"
CHARACTERS=ROOT/"engine/northstarcpp/resources/sharedAssets/characters"
DICT_MAGIC=0x10A79
DICT_ENTRY_TYPE=0x36125D3CFB7F3991

def u32(buf,off):
    if off<0 or off+4>len(buf):raise ValueError("Dictionary u32 outside header")
    return struct.unpack_from("<I",buf,off)[0]

def u64(buf,off):
    if off<0 or off+8>len(buf):raise ValueError("Dictionary u64 outside header")
    return struct.unpack_from("<Q",buf,off)[0]

def read_index(path:Path,target_ids:set[str]|None=None):
    with path.open("rb") as source:
        header=source.read(48)
        if len(header)!=48 or struct.unpack_from("<I",header)[0]!=DICT_MAGIC:
            raise ValueError(f"Unexpected texturedict3 header in {path}")
        pg_count=u32(header,16);table=u32(header,20)
        login_pg=u32(header,8);login_rel=u32(header,12)
        fix_table=u32(header,28)
        if not 1<=pg_count<=4096 or table+pg_count*12>4*1024*1024:
            raise ValueError("Dictionary page table invalid")
        source.seek(table)
        page_data=source.read(pg_count*12)
        if len(page_data)!=pg_count*12:raise ValueError("Incomplete page table")
        pages=[struct.unpack_from("<III",page_data,12*i)[:2] for i in range(pg_count)]
        header_end=max(a+b for a,b in pages)
        raw_start=pages[-1][0]+pages[-1][1]
        if header_end>64*1024*1024 or login_pg>=pg_count or raw_start>path.stat().st_size:
            raise ValueError("Dictionary index header unexpectedly large")
        source.seek(0);data=source.read(header_end)
    if len(data)!=header_end:raise ValueError("Truncated header pages")
    for start,length in pages:
        if start+length>len(data) or data[start:start+8]!=bytes.fromhex("efbeaddeefbeadde"):
            raise ValueError("Unexpected dictionary page marker")
    fix_ptr=u32(data,fix_table+4)
    fix_count=u32(data,fix_table+8)
    if fix_count>2000000 or fix_ptr+fix_count*8>len(data):
        raise ValueError("Dictionary fixup table invalid")
    fixups={}
    for n in range(fix_count):
        source_pg,dest_pg,address=struct.unpack_from("<HHI",data,fix_ptr+n*8)
        if source_pg>=pg_count or dest_pg>=pg_count:
            raise ValueError("Invalid dictionary fixup page")
        location=pages[source_pg][0]+address
        if location+8>len(data):
            raise ValueError("Dictionary fixup location invalid")
        fixups[location]=dest_pg
    def ptr(addr):
        target_page=fixups.get(addr)
        if target_page is None:raise ValueError(f"Unresolved dictionary pointer {addr:#x}")
        loc=pages[target_page][0]+u64(data,addr)
        if loc<0 or loc>=len(data):
            raise ValueError("Dictionary pointer outside header")
        return loc

    login=pages[login_pg][0]+login_rel
    count=u32(data,login+48)
    if not 1<=count<=10000 or login+56+8*count>len(data):
        raise ValueError("Dictionary login entries invalid")
    out={}
    listing=0
    for i in range(count):
        pg=u32(data,login+56+i*8)
        rel=u32(data,login+60+i*8)
        if pg>=pg_count:raise ValueError("Dictionary login points to absent page")
        item=pages[pg][0]+rel
        # Remastered Part II tags type by its name string, while Part I
        # may store a known hashed type id; 0x0001234500012345 is a sentinel
        # in the Remastered header, NOT a texture-dictionary type id.
        type_rel=u64(data,item+8)
        type_off=pages[pg][0]+type_rel
        kind=data[type_off:type_off+40].split(b"\0",1)[0]
        if kind!=b"TEXTURE_DICTIONARY":
            continue
        textures=u32(data,item+48)
        if textures>200000:raise ValueError("Dictionary texture count unreasonable")
        desc_list=ptr(item+72)
        for j in range(textures):
            descriptor=ptr(desc_list+8*j)-32
            offset=u32(data,descriptor+40)
            byte_count=u32(data,descriptor+48)
            hash64=f"{u64(data,descriptor+56):016x}"
            if target_ids is not None and hash64 not in target_ids:continue
            fmt=u32(data,descriptor+72)
            mip=u32(data,descriptor+80)
            width=u32(data,descriptor+84)
            height=u32(data,descriptor+88)
            if (not 0<byte_count<=256*1024*1024 or
                not 0<width<=16384 or not 0<height<=16384 or
                not 1<=mip<=17 or raw_start+offset+byte_count>path.stat().st_size):
                raise ValueError(f"Invalid streamed texture extent: {hash64}")
            image={
                "hash64":hash64,"path":str(path),"offset":raw_start+offset,
                "size":byte_count,"descriptor":descriptor,
                "format":fmt,"mips":mip,"width":width,"height":height}
            if hash64 in out:
                old=out[hash64]
                if any(old[k]!=image[k] for k in ("format","width","height","mips","size")):
                    raise ValueError("Conflicting texture descriptor inside one dictionary")
            out[hash64]=image
            listing+=1
    return out,{"dict":str(path),"pages":pg_count,"header_bytes":header_end,
                "login_entries":count,"selected_ids":len(out),"total_selected_entries":listing}

def dict_paths():
    all_files=[]
    for name in ("common","sp-common"):
        all_files.extend(sorted((GAME_DICTIONARIES/name/"texturedict3").glob("*.pak")))
    return all_files

def inspect_hashes(character="ellie"):
    base=CHARACTERS/character
    if character=="ellie":
        source=base/"source/native-ellie-textures-report.json"
        manifest=json.loads(source.read_text("utf8"))
        return {x["hash64"] for x in manifest["texture_manifest"]}
    # Isaac's original PAK-local mip manifest preserves each 64-bit hash.
    source=base/"source/native_vram/vram.manifest.json"
    manifest=json.loads(source.read_text("utf8"))
    return {entry["hash64"] for block in manifest["sources"]
            for entry in block["entries"]}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--report",type=Path,default=ELLIE/"source/streamed-texture-index.json")
    p.add_argument("--scope",choices=("ellie","all"),default="ellie")
    p.add_argument("--character",choices=("ellie","isaac"),default="ellie")
    a=p.parse_args()
    hashes=inspect_hashes(a.character) if a.scope=="ellie" else None
    base=CHARACTERS/a.character
    if a.report==ELLIE/"source/streamed-texture-index.json":
        a.report=base/"source/streamed-texture-index.json"
    items=collections.defaultdict(list)
    dictionaries=[]
    for source in dict_paths():
        entries,meta=read_index(source,hashes)
        dictionaries.append(meta)
        for h,item in entries.items():
            items[h].append(item)
        print(f"{source.name}: header {meta['header_bytes']:,} B, matched {len(entries)}",flush=True)
    report={
       "schema":"northstar.nd.texturedict3.index.v1",
       "status":"READ_ONLY_original_streamed_texture_dictionary_index",
       "source_game":"The Last of Us Part II Remastered PC",
       "source_hashes":len(hashes) if hashes else None,
       "matched_hashes":len(items),
       "missing_hashes":sorted(hashes-items.keys()) if hashes else [],
       "dictionaries":dictionaries,
       "textures":dict(items),
    }
    a.report.parent.mkdir(parents=True,exist_ok=True)
    report["character"]=a.character
    a.report.write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    print(json.dumps({
      "dictionaries":len(dictionaries),"source_hashes":len(hashes) if hashes else None,
      "matched_hashes":len(items),"missing":len(report["missing_hashes"]),
      "copies":sum(len(v) for v in items.values()),"report":str(a.report),
    },indent=2))
    return 0

if __name__=="__main__":raise SystemExit(main())
