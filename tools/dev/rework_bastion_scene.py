#!/usr/bin/env python3
"""One-time source-level upgrade: coherent Bastion city visual art pass."""
from pathlib import Path
import ast
import shutil

ROOT=Path(__file__).resolve().parents[2]
p=ROOT/"tools/create_moving_cities_prototype.py"
s=p.read_text("utf8")
backup=p.with_name(p.name+".bak-before-bastion-art-v2")
if not backup.exists():shutil.copy2(p,backup)

def patch(old,new):
    global s
    if old==new or new in s: return
    if old not in s: raise RuntimeError("Expected authoring pattern missing: "+old[:110])
    s=s.replace(old,new,1)

patch('asset="models/rustsea/bastion_deck_grate.ydr")',
      'visible=False, collision=True)')
patch('add("BASTION Guard rail %s %s" % (x,z), x, 0, z, 1.65, 0xA57E42)',
      'add("BASTION Guard rail %s %s" % (x,z), x, 0, z, 1.65, 0xA57E42,visible=False,collision=False)')
patch('add("BASTION Boiler %s" % x, x, 0.1, -2, 3, 0x7A5D40)',
      'add("BASTION Boiler %s" % x, x, 0.1, -2, 3, 0x7A5D40,visible=False,collision=False)')
patch('add("BASTION Engine light %s" % x, x, 3.15, -2, 0.5, 0xE6A648)',
      'add("BASTION Engine light %s" % x, x, 3.15, -2, 0.5, 0xE6A648,visible=False,collision=False)')
patch('add("BASTION Command plinth", 0, 0, 8, 3, 0x384958)',
      'add("BASTION Command plinth", 0, 0, 8, 3, 0x384958,visible=False)')
patch('add("BASTION Helm Console", 0, 3.05, 8, 1.1, 0xE7B24F)',
      'add("BASTION Helm Console", 0, 3.05, 8, 1.1, 0xE7B24F,visible=False)')
patch('add("BASTION Beacon", 0, 4.3, 8, 0.42, 0x3EE0CA)',
      'add("BASTION Beacon", 0, 4.3, 8, 0.42, 0x3EE0CA,visible=False,collision=False)')
patch('add("BASTION Watchlight %s %s" % (x,z), x, 0.15, z, .65, 0xE7B24F)',
      'add("BASTION Watchlight %s %s" % (x,z), x, 0.15, z, .65, 0xE7B24F,visible=False,collision=False)')
if '# Native YDR art modules from CC0 Kenney source.' in s:
    start=s.index('    # Native YDR art modules from CC0 Kenney source.')
    end=s.index('    # Non-colliding enemy geometry',start)
    art='''    # Native YDR traction-city hull; the collision proxies remain invisible.
    city_visuals=(
        "bastion_outer_hull", "bastion_armor_belt", "bastion_topdeck",
        "bastion_walkway", "bastion_tread_belts", "bastion_tread_shoes",
        "bastion_wheels", "bastion_wheel_hubs", "bastion_superstructure",
        "bastion_roofwork", "bastion_steel_ribs", "bastion_amber_marks",
        "bastion_windows", "bastion_pipework")
    for model in city_visuals:
        add("BASTION ART "+model,0,0,0,1.0,0x80756A,
            lift=False,collision=False,
            asset="models/rustsea/"+model+".ydr")
    # Curated machinery imported from Kenney: interior detail, not city form.
    art_pieces=(
        (-13.2,-16.5,"factory_machine",2.3),
        (13.2,-12.5,"factory_machine-fortified",2.5),
        (-12.9,3.5,"factory_pipe-large-valve",2.4),
        (12.6,8.7,"factory_machine-bed",2.4),
        (-13.4,17.8,"factory_hopper-high-round",2.5),
        (12.8,20.2,"city_detail-tank-large",2.5),
        (-13.8,-21.4,"factory_conveyor-long",2.2),
        (13.7,-22.0,"city_shipping-container-a",2.3),
    )
    for i,(x,z,model,size) in enumerate(art_pieces):
        add("BASTION INDUSTRIAL Module %03d %s" % (i,model),
            x,0,z,size,0x66625A,collision=False,
            asset="models/rustsea/"+model+".ydr")

'''
    s=s[:start]+art+s[end:]
patch('rock["color"],lift=False,collision=False)',
      'rock["color"],lift=False,collision=False,\n            asset="models/rustsea/rustsea_boulder_"+("a","b","c")[\n                (rock["ix"]*17+rock["iz"]*29)%3]+".ydr")')
patch('"state":"prototype-art-pass","genre":"first-person-chase"',
      '"state":"prototype-bastion-art-v2","genre":"first-person-chase"')
patch('"native_rustsea_visuals":sum(1 for kind,uid,blob in records\n                        if kind==2 and b"models/rustsea/" in blob),',
      '"native_rustsea_visuals":sum(1 for kind,uid,blob in records\n                        if kind==2 and b"models/rustsea/" in blob),\n                     "bastion_art_models":len(city_visuals),')
patch('if len(models)!=25 or len(textures)!=4:',
      'if len(models)!=42 or len(textures)!=4:')
patch('art_root/"rustsea_native_textures.json"]',
      'art_root/"rustsea_native_textures.json",\n                 art_root/"bastion_art_pass.json"]')
ast.parse(s)
p.write_text(s,encoding="utf8")
print("Applied authored Bastion hull scene source: "+str(p))
