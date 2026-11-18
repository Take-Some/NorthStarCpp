#!/usr/bin/env python3
"""Author world-space mobile CityRoot and articulated visual track entities."""
from __future__ import annotations
import ast
import shutil
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
p=ROOT/"tools/create_moving_cities_prototype.py"
s=p.read_text("utf8")
back=p.with_name(p.name+".bak-pre-bastion-world-drive-20261117")
if not back.exists():shutil.copy2(p,back)
def swap(before,after):
 global s
 if s.count(before)!=1:raise RuntimeError(f"Expected exactly one scene generator fragment: {before[:96]} count={s.count(before)}")
 s=s.replace(before,after,1)

swap("from tools.dev.moving_cities_terrain import TerrainSettings, tiles, outcrops, value_noise",
     "from tools.dev.moving_cities_terrain import TerrainSettings, tiles, outcrops, value_noise\nfrom tools.dev.bastion_tread_kinematics import LINK_COUNT,TRACK_X,WHEEL_X,WHEEL_Y,WHEEL_Z,belt_pose")
swap('visible=True, collision=True, asset=""):', 'visible=True, collision=True, asset="", rotation=(0,0,0)):')
swap('_text(asset) + _text("") + _vec(0, 0, 0) + _vec(1, 1, 1)',
     '_text(asset) + _text("") + _vec(*rotation) + _vec(1, 1, 1)')
swap('''    city_visuals=(
        "bastion_outer_hull", "bastion_armor_belt", "bastion_topdeck",
        "bastion_walkway", "bastion_tread_belts", "bastion_tread_shoes",
        "bastion_wheels", "bastion_wheel_hubs", "bastion_superstructure",
        "bastion_roofwork", "bastion_steel_ribs", "bastion_amber_marks",
        "bastion_windows", "bastion_pipework")''',
'''    city_visuals=(
        "bastion_outer_hull", "bastion_armor_belt", "bastion_topdeck",
        "bastion_walkway", "bastion_tread_belts",
        "bastion_wheels", "bastion_superstructure",
        "bastion_roofwork", "bastion_steel_ribs", "bastion_amber_marks",
        "bastion_windows", "bastion_pipework")''')
insert='''    # Native animated drive: 48 visible grousers per side, 7 wheel hubs per
    # side. Every group has its own world transform; rotation/position are
    # sampled from actual CityRoot odometry, NEVER elapsed animation time.
    for side in (-1,1):
        tag="L" if side<0 else "R"
        for link in range(LINK_COUNT):
            y,z,angle=belt_pose(link,0.)
            add("BASTION TREAD %s %02d" % (tag,link),
                side*TRACK_X,y,z,1.0,0x353331,lift=False,collision=False,
                rotation=(angle,0,0),
                asset="models/rustsea/bastion_tread_link.ydr")
        for wheel,z in enumerate(WHEEL_Z):
            add("BASTION ROADWHEEL %s %02d" % (tag,wheel),
                side*WHEEL_X,WHEEL_Y,z,1.0,0x645344,lift=False,
                collision=False,asset="models/rustsea/bastion_wheel_spokes.ydr")
    # Project-native, alpha-blended dust cutouts: objects start below terrain
    # and are later placed at world-space track contacts by gameplay.
    for i in range(24):
        add("BASTION DRIVE DUST %02d" % i,0.,-20.,0.,3.,
            0x8C7257,lift=False,collision=False,
            asset="models/rustsea/bastion_dust_puff.ydr")

'''
swap("    # Non-colliding enemy geometry is authored into the Scene asset.",
     insert+"    # Non-colliding enemy geometry is authored into the Scene asset.")
swap('if len(models)!=42 or len(textures)!=4:',
     'if len(models)!=45 or len(textures)!=5:')
swap('"state":"prototype-bastion-art-v2","genre":"first-person-chase"',
     '"state":"prototype-mobile-city-world-drive","genre":"first-person-chase"')
swap('"bastion_art_models":len(city_visuals),',
     '"bastion_art_models":len(city_visuals),\n                     "tread_links":LINK_COUNT*2,\n                     "roadwheels":len(WHEEL_Z)*2,\n                     "drive_dust":24,')
ast.parse(s)
p.write_text(s,"utf8")
print("Bastion scene generator updated for world motion + 96 links + 14 wheels + 24 dust puffs")
