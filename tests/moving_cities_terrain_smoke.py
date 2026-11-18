#!/usr/bin/env python3
"""Sea of Rust integration tests: project-owned streamed terrain, no flat tiles."""
from __future__ import annotations
import json,struct,sys,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.create_moving_cities_prototype import DEST,files
from tools.dev.scene_binary import read_nef8_scene_records,validate_nef8_scene

def name_and_flags(data):
    n=struct.unpack_from("<H",data)[0]
    name=data[2:2+n].decode("utf8")
    return name,bool(data[-1])

def main():
    a,b=files(),files()
    assert a["main.nscn"]==b["main.nscn"],"Nondeterministic scene"
    stats=json.loads(a["gameplay/scene_stats.json"])
    terrain=json.loads(a["assets/streaming/procedural_terrain.json"])
    metadata=json.loads(a["gameplay/terrain.json"])
    assert (stats["entities"],stats["components"])==(347,354)
    assert stats["runtime_terrain_streaming"] is True
    assert stats["terrain_cells"]==stats["terrain_rocks"]==0
    assert terrain["enabled"] is True and terrain["seed"]==stats["terrain_seed"]
    assert terrain["follow_entity"]=="CITYROOT Bastion"
    assert 0<terrain["vertical_scale_m"]<=6
    assert terrain["radius_chunks"]==4 and terrain["chunk_width_m"]==64
    assert terrain["material"].startswith("materials/") and "@ground_dry" in terrain["material"]
    assert metadata["world_streaming"]=="dynamic_heightfield_chunks"
    assert metadata["terrain_type"]=="streamed_procedural_heightfield"
    rec=read_nef8_scene_records(a["main.nscn"])
    assert validate_nef8_scene(a["main.nscn"])==354
    names={}
    for kind,_,data in rec:
        if kind==2:
            name,collides=name_and_flags(data)
            assert name not in names,name
            names[name]=collides
    assert len(names)==347
    assert not any(n.startswith("WASTELAND CELL ") or
                   n.startswith("WASTELAND ROCK ") for n in names)
    assert names["BASTION Deck -3 -12"] is False
    assert names["CITYROOT Bastion"] is True
    assert names["CITYROOT Rust Wren"] is True
    assert sum(n.startswith("BASTION TREAD ") for n in names)==96
    assert sum(n.startswith("BASTION ROADWHEEL ") for n in names)==14
    assert sum(n.startswith("BASTION DRIVE DUST ") for n in names)==24
    assert sum(n.startswith("BASTION ART ") for n in names)==12
    assert sum(n.startswith("RUSTWREN ") for n in names)==50
    js=a["assets/scripts/main.js"]
    assert "Vehicle.configureRig" in js and "syncFromVehicle" in js
    assert "renderRelativeTo" not in js
    assert "Legacy finite wasteland must be removed" in js
    for rel in ("assets/vehicles/bastion.nvehicle",
                "assets/vehicles/rust_wren.nvehicle",
                "assets/streaming/procedural_terrain.json"):
        assert rel in a
    ydr=[n for n in a if n.endswith(".ydr")]
    ytd=[n for n in a if n.endswith(".ytd")]
    assert len(ydr)==45 and len(ytd)==5
    assert not any(n.endswith((".obj",".jpg",".png")) for n in a)
    for rel in ("main.nscn","gameplay/terrain.json","gameplay/scene_stats.json",
                "assets/scripts/main.js","assets/streaming/procedural_terrain.json"):
        raw=a[rel] if isinstance(a[rel],bytes) else a[rel].encode("utf8")
        assert (DEST/rel).read_bytes()==raw,rel
    ET.fromstring((DEST/"assets/ui/chase_hud.nui").read_text("utf8"))
    print(json.dumps({"pass":True,"scene_entities":347,"records":354,
                      "finite_ground_tiles":0,"seed":terrain["seed"],
                      "stream_radius":terrain["radius_chunks"],
                      "relief_m":terrain["vertical_scale_m"]}))
if __name__=="__main__":main()
