#!/usr/bin/env python3
"""Port WeaponLab's authored pistol/rifle finger pose data into NorthStar XML.

The sole input is the existing NewViso endorphin weapon_rig configuration.
Retargeted joint NAMES are data, not code-side Abby conditionals.
"""
from pathlib import Path
import json,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
source=root.parent/'Shared'/'Content'/'physics'/'endorphin'/'humanoid.json'
target=root/'engine/northstarcpp/resources/sharedAssets/characters/abby/animations/weapon_grips.xml'
rig=json.loads(source.read_text(encoding='utf8'))['weapon_rig']
node=ET.Element('NorthStarWeaponGrips',{'version':'1',
    'source':'WeaponLab/Shared/Content/physics/endorphin/humanoid.json',
    'coordinate_space':'local_joint_delta_degrees'})
digits=('thumb','index','middle','ring','pinky')
for weapon in ('pistol','rifle'):
 # Use the existing WeaponLab weapon catalog support point relative
 # to the primary pistol/rifle grip, not invented camera offsets.
 import re
 catalog=(root.parent/'Shared/Content/scripts/newviso/gameplay/weapon_catalog.ysc').read_text(encoding='utf8')
 item_id='weapon.pistol.military' if weapon=='pistol' else 'weapon.rifle.mini14'
 fragment=catalog.split('id: "'+item_id+'"',1)[1].split('  {\n    id:',1)[0]
 grip_text=fragment.split('grip: [',1)[1].split(']',1)[0]
 support_text=fragment.split('support: [',1)[1].split(']',1)[0]
 assert grip_text and support_text, ('missing authored WeaponLab support target',item_id)
 grip=tuple(float(v.strip()) for v in grip_text.split(','))
 support=tuple(float(v.strip()) for v in support_text.split(','))
 difference=[support[i]-grip[i] for i in range(3)]
 profile=ET.SubElement(node,'Profile',{'class':weapon,
    'support_offset':' '.join(f'{v:.9g}' for v in difference)})
 if weapon=='pistol':
  # Recovered from the native pistol.ydd mesh: rear-notch and front-post
  # top vertices. The previous .068-m reference was inside the slide,
  # causing correct math against the WRONG (invisible) sight line.
  profile.attrib.update({'rear_sight':'0 0.09189 -0.020',
      'front_sight':'0 0.09238 0.178',
      'sight_eye_distance':'0.39',
      'ads_elbow_outward_bias':'0.43',
      'ads_elbow_drop_bias':'0.44',
      'ads_support_hand_stiffness':'1.20',
      'ads_forearm_visibility_guard':'0.235'})
 for side in ('right','left'):
  hand=rig[side]['grips'][weapon]
  hand_node=ET.SubElement(profile,'Hand',{'side':side,
     'wrist_offset':' '.join(str(x) for x in hand['wrist_offset']),
     'socket_offset':' '.join(str(x) for x in hand['socket_offset'])})
  prefix='r' if side=='right' else 'l'
  assert len(hand['joint_rotations'])==15
  for j in hand['joint_rotations']:
   name=j['joint']
   assert name.startswith('SKEL_') and name[5] in 'RL'
   idx=int(name[-2])
   seg=int(name[-1])
   assert 0<=idx<=4 and 0<=seg<=2
   asset_bone=f'{prefix}_{digits[idx]}{chr(ord("a")+seg)}'
   angles=j['rotation_degrees']
   ET.SubElement(hand_node,'Joint',{'name':asset_bone,
       'x':str(angles[0]),'y':str(angles[1]),'z':str(angles[2])})
ET.indent(node,space='  ')
target.parent.mkdir(parents=True,exist_ok=True)
target.write_text(ET.tostring(node,encoding='unicode')+'\n',encoding='utf8')
print(target.relative_to(root),target.stat().st_size)
