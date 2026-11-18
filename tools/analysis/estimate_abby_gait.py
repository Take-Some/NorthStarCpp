#!/usr/bin/env python3
"""Estimate locomotion speed from *in-place* imported Abby footsteps.
Does not claim to measure original game-controller velocity.

Forward velocity is approximated by the median backward motion of the
foot/toe during ground-contact frames; gait-loop phase analysis is provided
for auditing. No original game files are modified.
"""
import json
import math
import statistics
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zlib

ROOT=Path(__file__).resolve().parents[2]
YMT=ROOT / "engine/northstarcpp/resources/sharedAssets/characters/abby/models/abby.ymt"
YCD=ROOT / "engine/northstarcpp/resources/sharedAssets/characters/abby/animations/mm-explore.ycd"
CLIPS=["abby-mm-explore-walk-loop-fw","abby-mm-explore-run-loop-fw","abby-mm-explore-sprint-loop-fw"]
FEET=["r_heel","l_heel","r_ball","l_ball","r_toe","l_toe","r_ankle","l_ankle"]

def unpack(file:Path):
    b=file.read_bytes()
    assert b[:4]==b'NEF8'
    compressed=struct.unpack_from('<Q',b,16)[0]
    return zlib.decompress(b[len(b)-compressed:],-15)

def qmul(a,b):
    aw,ax,ay,az=a;bw,bx,by,bz=b
    return (aw*bw-ax*bx-ay*by-az*bz,
            aw*bx+ax*bw+ay*bz-az*by,
            aw*by-ax*bz+ay*bw+az*bx,
            aw*bz+ax*by-ay*bx+az*bw)

def rotate(q,v):
    # Hamilton quaternion physical-space transform.
    w,x,y,z=q
    vx,vy,vz=v
    tx=2*(y*vz-z*vy);ty=2*(z*vx-x*vz);tz=2*(x*vy-y*vx)
    return (vx+w*tx+(y*tz-z*ty),
            vy+w*ty+(z*tx-x*tz),
            vz+w*tz+(x*ty-y*tx))

def local_from_attrs(a):
    return ((float(a['tx']),float(a['ty']),float(a['tz'])),
            (float(a['qw']),float(a['qx']),float(a['qy']),float(a['qz'])))

def build_bind():
    source=ET.fromstring(unpack(YMT))
    joints=source.findall('.//Joint')
    bind=[local_from_attrs(j.attrib) for j in joints]
    parents=[int(j.get('parent_index')) for j in joints]
    names={j.get('name'):i for i,j in enumerate(joints)}
    return bind,parents,names

def get_clips():
    body=unpack(YCD);version,count=struct.unpack_from('<II',body)
    table=struct.unpack_from('<Q',body,8)[0]
    strings=struct.unpack_from('<Q',body,16)[0]
    payload=struct.unpack_from('<Q',body,32)[0]
    assert version in (2,3) and table==48
    out={}
    for i in range(count):
        r=table+i*64
        no=struct.unpack_from('<I',body,r+8)[0]
        name=body[strings+no:payload].split(b'\0',1)[0].decode('utf8')
        if name not in CLIPS:continue
        tracks,frames=struct.unpack_from('<II',body,r+16)
        duration,fps=struct.unpack_from('<ff',body,r+24)
        start,size=struct.unpack_from('<QQ',body,r+40)
        joints=struct.unpack_from('<'+'I'*tracks,body,start)
        assert size==tracks*(4+frames*40)
        out[name]={"joints":joints,"frames":frames,"tracks":tracks,
                   "duration":duration,"fps":fps,"body":body,"offset":start+tracks*4}
    return out

def measure(clip,bind,parents,names):
    frames=clip['frames'];tracks=clip['tracks'];fps=clip['fps']
    needed=[names[n] for n in FEET]
    needed_ancestors=set()
    for idx in needed:
        while idx>=0:
            needed_ancestors.add(idx)
            idx=parents[idx]
    needed_ancestors=sorted(needed_ancestors)
    joint_to_track={id:i for i,id in enumerate(clip['joints'])}
    positions={name:[] for name in FEET}
    for frame in range(frames):
        poses={}
        for joint in needed_ancestors:
            k=joint_to_track.get(joint)
            if k is None:poses[joint]=bind[joint];continue
            offset=clip['offset']+(frame*tracks+k)*40
            tx,ty,tz,qx,qy,qz,qw,*_=struct.unpack_from('<10f',clip['body'],offset)
            poses[joint]=((tx,ty,tz),(qw,qx,qy,qz))
        world={}
        for joint in needed_ancestors:
            pos,q=poses[joint]
            parent=parents[joint]
            if parent<0:world[joint]=(pos,q)
            else:
                ppos,pq=world[parent]
                offset=rotate(pq,pos)
                world[joint]=((ppos[0]+offset[0],ppos[1]+offset[1],ppos[2]+offset[2]),qmul(pq,q))
        for name in FEET:
            positions[name].append(world[names[name]][0])
    report={"duration_s":round(clip['duration'],5),"fps":fps,
            "num_frames":frames,"measurements":{}}
    for name,series in positions.items():
        heights=[p[1] for p in series]
        low=min(heights)
        cut=low+0.06
        v=[(series[i][0]-series[i-1][0],series[i][2]-series[i-1][2])
           for i in range(1,len(series))]
        contact=[(dx*fps,dz*fps) for i,(dx,dz) in enumerate(v,1)
                 if max(heights[i],heights[i-1])<=cut]
        if not contact:
            continue
        speeds=[math.hypot(x,z) for x,z in contact]
        # Signed longitudinal component: infer forward X or Z from
        # dominant displacement during contact; negative = foot backwards.
        ax=max((0,1),key=lambda k:abs(statistics.median(v[k] for v in contact)))
        longitudinal=statistics.median(v[ax] for v in contact)
        report['measurements'][name]={
            "min_height":round(low,5),"max_height":round(max(heights),5),
            "height_span":round(max(heights)-low,5),
            "contact_frames":len(contact),"median_foot_contact_speed":round(statistics.median(speeds),4),
            "median_longitudinal_component":round(longitudinal,4),
            "primary_axis":"x" if ax==0 else "z",
            "p25_foot_speed":round(sorted(speeds)[len(speeds)//4],4),
            "p75_foot_speed":round(sorted(speeds)[3*len(speeds)//4],4)
        }
    return report

if __name__=='__main__':
    bind,parents,names=build_bind()
    clips=get_clips()
    print(json.dumps({"source_ycd":str(YCD),"source_ymt":str(YMT),
                      "method":"stance-foot displacement (NOT game-controller speed)",
                      "results":{n:measure(clips[n],bind,parents,names) for n in CLIPS}},
                     indent=2,ensure_ascii=False))
