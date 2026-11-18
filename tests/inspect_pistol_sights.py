"""Inspect actual calibrated pistol YDD vertices; does not modify the asset."""
from pathlib import Path
import struct,zlib,json,math,collections,xml.etree.ElementTree as ET
p=Path(__file__).resolve().parents[1]/"engine/northstarcpp/resources/sharedAssets/models/weapon/pistol/pistol.ydd"
buf=p.read_bytes()
assert buf[:4]==b"NEF8"
compressed, expanded=struct.unpack_from("<QQ",buf,16)
body=zlib.decompress(buf[-compressed:],-15)
assert len(body)==expanded and struct.unpack_from("<I",body,0)[0] in (4,5)
kind=struct.unpack_from("<I",body,0)[0]
strings,strings_size,geometry=struct.unpack_from("<QQQ",body,16)
count,version=struct.unpack_from("<II",body,geometry)
assert version==1
def name(o):
    off=strings+o
    return body[off:body.index(b"\0",off)].decode("utf8")
print("native_kind",kind,"parts",count,"shape",struct.unpack_from("<6f",body,76))
cursor=geometry+72
slide_points=None
for k in range(count):
    nref,mref,vertices,indices=struct.unpack_from("<4I",body,cursor)
    stride=80
    header_bytes=52 if kind==5 else 48
    vbase=cursor+header_bytes
    points=[struct.unpack_from("<3f",body,vbase+j*32) for j in range(vertices)]
    if k==0:slide_points=points
    if not all(all(math.isfinite(v) for v in p) for p in points):
        raise RuntimeError("Invalid points")
    xs=[p[0] for p in points];ys=[p[1] for p in points];zs=[p[2] for p in points]
    print("PART",k,name(nref),"v",vertices,"indices",indices)
    print(" BOUNDS",[(round(min(v),5),round(max(v),5)) for v in [xs,ys,zs]])
    for zone,(z0,z1) in {"rear":(-.05,.065),"middle":(.065,.13),"front":(.13,.24)}.items():
        subset=[p for p in points if z0<=p[2]<=z1 and abs(p[0])<.025]
        if not subset:continue
        ytop=max(p[1] for p in subset)
        tops=sorted({(round(p[0],5),round(p[1],5),round(p[2],5)) for p in subset if p[1]>=ytop-.002},key=lambda x:x[2])
        print(zone,"count",len(subset),"top_y",round(ytop,5),"tops",tops[:24])
    print(" y_top_distribution",collections.Counter(round(p[1],3) for p in points if abs(p[0])<.025).most_common(18))
    if k in (0,1):
        for zone,zlow,zhigh in [('rear_tip',-.022,-.018),('rear_post',-.012,.0),('rear_forward',.02,.035),('front_tip',.175,.182)]:
            for xlow,xhigh in [(-.002,.002),(-.006,-.002),(.002,.006),(-.012,-.006),(.006,.012)]:
                q=[v for v in points if zlow<=v[2]<=zhigh and xlow<=v[0]<=xhigh]
                if q:print('SIGHT_CROSS_SECTION',k,zone,xlow,xhigh,'vertices',len(q),'top',round(max(v[1] for v in q),5))
    cursor=vbase+vertices*stride+indices*4
assert cursor==len(body),(cursor,len(body))
xml=r=Path(__file__).resolve().parents[1]/"engine/northstarcpp/resources/sharedAssets/characters/abby/animations/weapon_grips.xml"
profile=ET.parse(xml).getroot().find("./Profile[@class='pistol']")
assert profile is not None
sights={name:tuple(map(float,profile.attrib[name].split())) for name in ('rear_sight','front_sight')}
for name,point in sights.items():
    nearest=min(math.dist(point,p) for p in slide_points)
    print('AUTHORED_SIGHT',name,point,'nearest_mesh_vertex_m',nearest)
    assert nearest<.004, (name,nearest)
assert sights['front_sight'][2]-sights['rear_sight'][2]>.15
assert abs(sights['front_sight'][1]-sights['rear_sight'][1])<.003
print('PASS real pistol.YDD physical front/rear sight calibration')
