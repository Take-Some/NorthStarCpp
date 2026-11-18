"""Validate real KaylaNightCity Ellie/Isaac retargeted YCD joint tracks.

Static asset acceptance, not visual/GPU QA. Does not rewrite project assets.
The clips are retargets from Abby, not original Ellie/Isaac ICE motion.
"""
from __future__ import annotations
import json
import math
import struct
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.dev.retarget_nightcity_npc_ycd import OUT,unpack,skeleton

def check(character:str):
    file=OUT/(character+"_locomotion.ycd")
    raw=unpack(file)
    version,count,table,strings,_,payload=struct.unpack_from("<IIQQQQ",raw)
    assert version in (2,3) and count in range(1,32) and table==48
    assert 48+count*64<=strings<=payload<=len(raw)
    bone_count=len(skeleton(character))
    result={}
    for entry in range(count):
        offset=table+entry*64
        name_at=strings+struct.unpack_from("<I",raw,offset+8)[0]
        assert strings<=name_at<payload
        end=raw.index(b"\0",name_at,payload)
        name=raw[name_at:end].decode("utf8")
        count_tracks,count_frames=struct.unpack_from("<II",raw,offset+16)
        duration,rate=struct.unpack_from("<ff",raw,offset+24)
        sample_pos,length=struct.unpack_from("<QQ",raw,offset+40)
        assert count_tracks>10 and count_frames>10
        assert rate>10 and duration>.1 and sample_pos>=payload
        assert length==count_tracks*(4+40*count_frames)
        assert sample_pos+length<=len(raw)
        indices=struct.unpack_from("<"+"I"*count_tracks,raw,sample_pos)
        assert len(set(indices))==len(indices)
        assert max(indices)<bone_count
        data_at=sample_pos+count_tracks*4
        # Tracks are stored frame-major, one 10xfloat transform per bone.
        def track(frame,track_id):
            return struct.unpack_from("<10f",raw,
                data_at+(frame*count_tracks+track_id)*40)
        moving=0
        for track_id in range(count_tracks):
            a=track(0,track_id)
            b=track(count_frames//2,track_id)
            c=track(count_frames-1,track_id)
            assert all(math.isfinite(v) for v in a+b+c)
            # Either joint translation, rotation or scale changes.
            if max(abs(x-y) for x,y in zip(a,b))>.0001 or (
                max(abs(x-y) for x,y in zip(a,c))>.0001):
                moving+=1
        assert moving>=8,(character,name,"non-animated tracks",moving)
        result[name]={"tracks":count_tracks,"animatedTracks":moving,
                      "frames":count_frames,"duration":round(duration,3),
                      "sampleRateHz":round(rate,2)}
    assert {"npc-idle","npc-walk"}<=set(result)
    return result

if __name__=="__main__":
    report={person:check(person) for person in ("ellie","isaac")}
    print(json.dumps(report,ensure_ascii=False,indent=2))
