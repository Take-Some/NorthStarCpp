"""Validate actual generated NPC YCD locomotion has changing joint motion.

Does not mistake a skinned but stationary bind pose for a walking clip.
"""
import math,struct
from tools.dev.install_nightcity_npcs import ROOT
from tools.dev.retarget_nightcity_npc_ycd import unpack,skeleton,CLIPS
from tools.dev.scene_binary import read_nef8_scene_records
from tools.dev.install_nightcity_npcs import SCENE,name_of,encode_component

def read_ycd(slug):
 p=ROOT/"Projects/KaylaNightCity/assets/animations/npcs"/(slug+"_locomotion.ycd")
 d=unpack(p)
 v,entries,table,strings,_,payload=struct.unpack_from("<IIQQQQ",d)
 assert v==3 and entries==2
 result={}
 for i in range(entries):
  rec=table+64*i
  pos=struct.unpack_from("<I",d,rec+8)[0]+strings
  name=d[pos:d.index(b"\0",pos)].decode()
  tracks,frames=struct.unpack_from("<II",d,rec+16)
  rate=struct.unpack_from("<f",d,rec+28)[0]
  offset,size=struct.unpack_from("<QQ",d,rec+40)
  assert size==tracks*(4+frames*40) and tracks>=30
  ids=struct.unpack_from("<"+("I"*tracks),d,offset)
  result[name]=(d,tracks,frames,rate,offset+4*tracks,ids)
 return result

def rotations(clip, joint):
 d,tracks,frames,rate,start,ids=clip
 i=ids.index(joint)
 def xyz(f):
  row=start+(f*tracks+i)*40
  return struct.unpack_from("<ffff",d,row+12)
 q0=xyz(1);q1=xyz(frames//4);q2=xyz(frames//2)
 dot=lambda a,b:abs(sum(x*y for x,y in zip(a,b)))
 return 2*math.acos(max(-1,min(1,min(dot(q0,q1),dot(q0,q2)))))

def main():
 records=read_nef8_scene_records(SCENE.read_bytes())
 ids={name_of(raw):eid for typ,eid,raw in records if typ==2}
 for slug,label in (("ellie","NPC Ellie"),("isaac","NPC Isaac")):
  clips=read_ycd(slug)
  assert set(clips)==set(CLIPS)
  skel={v[0]:i for i,v in enumerate(skeleton(slug))}
  walk=clips["npc-walk"]
  assert walk[2]>50
  angles={name:round(rotations(walk,skel[name]),3)
          for name in ["l_upper_leg","r_upper_leg","l_knee","r_knee","l_shoulder","r_shoulder"]}
  assert max(angles[name] for name in ("l_upper_leg","r_upper_leg"))>0.08,angles
  assert max(angles[name] for name in ("l_knee","r_knee"))>0.06,angles
  expected=encode_component("engine.model.animation",1,
        ("animations/npcs/"+slug+"_locomotion.ycd@npc-idle").encode())
  assert any(typ==7 and eid==ids[label] and raw==expected
             for typ,eid,raw in records)
  print("PASS",slug,"authored walking joint angular motion (rad)",angles)
 print("PASS Native in-place YCD locomotion components for both NPCs")
if __name__=="__main__":main()
