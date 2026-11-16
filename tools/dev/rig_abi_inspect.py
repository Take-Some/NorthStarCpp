"""Analyse Abby YCD/mesh bone pose with the NorthStar row-vector conventions."""
from pathlib import Path
import struct, zlib, xml.etree.ElementTree as ET, math, sys
ROOT=Path(__file__).resolve().parents[2]
def unzip(path):
 b=path.read_bytes()
 assert b[:4]==b'NEF8' and b[4]==2
 c=struct.unpack_from('<Q',b,16)[0]
 return zlib.decompress(b[-c:],-15)
def eye():
 return [[float(i==j) for j in range(4)] for i in range(4)]
def matmul(a,b):
 return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def xyz_matrix(pos,q):
 x,y,z,w=q
 # Original YMT/YCD quaternion is conjugated on import; a NorthStar
 # row-major matrix of the conjugate is numerically Rcolumn(original).
 m=eye()
 m[0][0]=1-2*(y*y+z*z);m[0][1]=2*(x*y-z*w);m[0][2]=2*(x*z+y*w)
 m[1][0]=2*(x*y+z*w);m[1][1]=1-2*(x*x+z*z);m[1][2]=2*(y*z-x*w)
 m[2][0]=2*(x*z-y*w);m[2][1]=2*(y*z+x*w);m[2][2]=1-2*(x*x+y*y)
 m[0][1],m[1][0]=m[1][0],m[0][1]
 m[0][2],m[2][0]=m[2][0],m[0][2]
 m[1][2],m[2][1]=m[2][1],m[1][2]
 m[3][:3]=pos
 return m
def collect_skeleton():
 xml=ET.fromstring(unzip(ROOT/'sharedAssets/characters/abby/models/abby.ymt'))
 joints=[]
 for j in xml.iter('Joint'):
  a=j.attrib
  joints.append(dict(name=a['name'],parent=int(a['parent_index']),
   p=tuple(float(a['t'+c]) for c in 'xyz'),
   q=tuple(float(a['q'+c]) for c in 'xyzw')))
 return joints
def load_clip(needle):
 data=unzip(ROOT/'sharedAssets/characters/abby/animations/mm-explore.ycd')
 ver,n,tab,strings,pay=struct.unpack_from('<IIQQQ',data,0)
 for i in range(n):
  entry=tab+i*64;off=struct.unpack_from('<I',data,entry+8)[0]
  name=data[strings+off:strings+off+256].split(bytes([0]))[0].decode()
  if name!=needle:continue
  tracks,frames=struct.unpack_from('<II',data,entry+16)
  start=struct.unpack_from('<Q',data,entry+40)[0]
  ids=struct.unpack_from('<'+str(tracks)+'I',data,start)
  samples=[struct.unpack_from('<10f',data,start+4*tracks+40*k)
           for k in range(tracks*frames)]
  return name,ids,samples,frames
 raise KeyError(needle)
if __name__=='__main__':
 joints=collect_skeleton()
 name,tracks,samples,frames=load_clip('abby-mm-explore-idle')
 print('CLIP',name,'tracks',len(tracks),'frames',frames)
 keyed={ident:samples[i] for i,ident in enumerate(tracks)}
 global_m=[]
 for i,j in enumerate(joints):
  s=keyed.get(i)
  pp=tuple(s[:3]) if s else j['p']
  qq=tuple(s[3:7]) if s else j['q']
  m=xyz_matrix(pp,qq)
  if j['parent']>=0:m=matmul(m,global_m[j['parent']])
  global_m.append(m)
 raw=unzip(ROOT/'sharedAssets/characters/abby/models/abby.ydd')
 off=struct.unpack_from('<Q',raw,32)[0]
 meshmat=struct.unpack_from('<16f',raw,off+8)
 # mesh_to_skeleton diag(-1,+1,-1), same inverse.
 for i,j in enumerate(joints):
  if i<35 and (i<6 or any(x in j['name'] for x in ('clavicle','shoulder','elbow','wrist','hand_prop'))):
   m=global_m[i]
   pp=m[3][:3]
   mesh=(-pp[0],pp[1],-pp[2])
   print(f"{i:3} {j['name']:<24} local={str(tuple(round(v,3) for v in j['p'])):<28} SKEL={tuple(round(v,3) for v in pp)} MESH={tuple(round(v,3) for v in mesh)}")
 print('RIGHT HAND MATRIX:')
 for row in global_m[31]:print(' ',[round(v,4) for v in row])
 print('TRACK KEYS (shoulder, elbow wrist):')
 for ident in [18,19,20,21,22,23,24,25,26,27,28,29,31]:
  print(ident,joints[ident]['name'],'YCD=',[round(v,3) for v in keyed.get(ident,())])
