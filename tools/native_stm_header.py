#!/usr/bin/env python3
"""Validate ANIMMAGICSTR cinematic stream headers; no actor track decode."""
import argparse,hashlib,json,struct
from pathlib import Path
DEFAULT=Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\animations\isaac\cinematic_streams")
class StreamHeaderError(ValueError):pass
def inspect(p):
    n=p.stat().st_size
    with p.open("rb") as f: head=f.read(176)
    if len(head)<176 or head[4:16]!=b"ANIMMAGICSTR":
        raise StreamHeaderError("Truncated/wrong original cinematic header")
    version,size,unused,table=struct.unpack_from("<4I",head,16)
    page=struct.unpack_from("<I",head,32)[0]
    offsets=[struct.unpack_from("<I",head,i)[0] for i in (56,64)]
    if version!=2681 or size!=168 or page!=1 or not 168<=table<n or any(not 168<=i<n for i in offsets):
        raise StreamHeaderError("Invalid source cinematic header offsets")
    hash=hashlib.sha256()
    with p.open("rb") as f:
        for buf in iter(lambda:f.read(1024*1024),b""):hash.update(buf)
    return {"file":str(p),"bytes":n,"sha256":hash.hexdigest(),
            "format":"ANIMMAGICSTR","version":version,"header_bytes":size,
            "table_offset":table,"uninterpreted_offsets":offsets,
            "decoded_actor_tracks":0}
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--root",type=Path,default=DEFAULT)
    ap.add_argument("--output",type=Path)
    a=ap.parse_args()
    files=sorted(a.root.rglob("*.stm"))
    if not files:raise FileNotFoundError(a.root)
    results=[inspect(p) for p in files]
    data={"schema":"northstar.tlou2.stm_metadata.v1","streams":results,
          "decoded_actor_tracks":0}
    if a.output:
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(data,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"verified_streams":len(results),"versions":sorted({x["version"] for x in results}),
                      "decoded_actor_tracks":0}))
if __name__=="__main__":main()
