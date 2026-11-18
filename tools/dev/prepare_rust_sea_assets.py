#!/usr/bin/env python3
"""Fetch and stage CC0 art for The Rust Sea generated project.

Run explicitly: python tools/dev/prepare_rust_sea_assets.py
The original archives are not stored; selected original Kenney OBJ geometry
and named Poly Haven 1K texture maps are included with source provenance.
No 3rd-party asset is redistributed as a standalone commercial asset pack.
"""
from __future__ import annotations
import hashlib
import io
import json
import sys
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.pack_native_ymat import build_ymat

DEST = ROOT / "tools/templates/moving_cities/assets"
HEADER = {"User-Agent": "Mozilla/5.0 (TheRustSeaAssetImport/1.0)", "Referer": "https://kenney.nl"}
PACKS = {
    "factory": {
        "id": "6173736574732f666163746f72792d6b6974",
        "source": "https://kenney.nl/assets/factory-kit",
        "models": [
            "catwalk-straight", "catwalk-corner", "catwalk-stairs",
            "machine", "machine-fortified", "machine-bed", "pipe-large",
            "pipe-large-bend", "pipe-large-valve", "crane", "crane-lift",
            "hopper-high-round", "conveyor-long", "cog-a",
        ],
    },
    "city": {
        "id": "6173736574732f636974792d6b69742d696e647573747269616c",
        "source": "https://kenney.nl/assets/city-kit-industrial",
        "models": ["building-a", "building-c", "building-h",
                   "chimney-basic", "chimney-large",
                   "detail-tank-large", "water-tower",
                   "shipping-container-a"],
    }
}
TEXTURES = {
    "dry_ground_01": ("diff", "nor_dx", "rough"),
    "gravelly_sand": ("diff", "nor_dx", "rough"),
    "rusty_metal_04": ("diff", "nor_dx", "rough", "metal"),
    "rusty_metal_grid": ("diff", "nor_dx", "rough"),
}
def fetch(url: str, *, referer: str = "https://kenney.nl") -> bytes:
    h = dict(HEADER, Referer=referer)
    with urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=45) as response:
        data = response.read(20*1024*1024+1)
        if len(data)>20*1024*1024:
            raise ValueError("Asset exceeds 20 MiB download bound: "+url)
        return data
def atomic(path: Path, raw: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name+".part")
    tmp.write_bytes(raw)
    tmp.replace(path)
def build_xml() -> bytes:
    root=ET.Element("YmatMaterialLibrary",
                    version="2", schema="northstarcpp.ymat.material_library.v2")
    definitions = {
        # material: texture, metallic, roughness, diffuse tint multiplier
        "ground_dry": ("dry_ground_01", 0., .91, "1,1,1,1"),
        "ground_gravel": ("gravelly_sand", 0., .94, "1,1,1,1"),
        "steel_corroded": ("rusty_metal_04", .62, .83, ".68,.62,.57,1"),
        "steel_grate": ("rusty_metal_grid", .68, .79, ".55,.55,.51,1"),
        "hull_dark": ("rusty_metal_04", .55, .91, ".24,.27,.29,1"),
        "hull_armor": ("rusty_metal_04", .64, .85, ".45,.36,.30,1"),
        "deck_worn": ("rusty_metal_grid", .58, .94, ".34,.35,.34,1"),
        "tread_dark": ("rusty_metal_04", .36, .94, ".15,.16,.16,1"),
        "city_cladding": ("rusty_metal_04", .48, .86, ".52,.45,.38,1"),
        "warning_amber": ("rusty_metal_04", .38, .75, ".85,.50,.20,1"),
        "glass_dark": ("rusty_metal_04", .10, .42, ".08,.16,.20,1"),
        "dust_translucent": ("dust_cloud", 0., 1., ".80,.70,.57,.75"),
    }
    for mat,(name,metal,rough,tint) in definitions.items():
        node=ET.SubElement(root,"Material",name=mat,shader="pbr.default")
        ET.SubElement(node,"Surface",blend=("alpha" if mat=="dust_translucent" else "opaque"),two_sided=("true" if mat=="dust_translucent" else "false"),alpha_cutoff="0")
        textures=ET.SubElement(node,"Textures")
        for slot,suffix in [("base_color","diff"),("normal","nor_dx"),
                             ("roughness","rough"),("metallic","metal")]:
            if suffix not in TEXTURES.get(name,("diff",)):continue
            ET.SubElement(textures,"Texture",slot=slot,
                ref=f"textures/rustsea/{name}.ytd@{suffix}")
        params=ET.SubElement(node,"Params")
        for pname,typ,value in [("base_color","color",tint),
                                 ("normal_scale","float","1"),
                                 ("roughness","float",str(rough)),
                                 ("metallic","float",str(metal))]:
            ET.SubElement(params,"Param",name=pname,type=typ,value=value)
        ex=ET.SubElement(node,"Extensions")
        ET.SubElement(ex,"Property",name="shading_model").text="pbr"
        ET.SubElement(ex,"Property",name="surface_domain").text="surface"
    ET.indent(root,space="  ")
    return (b'<?xml version="1.0" encoding="utf-8"?>\n'+
            ET.tostring(root,encoding="utf-8")+b"\n")

def main():
    provenance = {"license": "CC0 1.0 Universal", "assets": [], "external_files": []}
    for key,pack in PACKS.items():
        info=json.loads(fetch("https://kenney.nl/download-link/"+pack["id"],
                              referer=pack["source"]))
        archive=fetch(info["url"],referer=pack["source"])
        with zipfile.ZipFile(io.BytesIO(archive)) as zipfile_obj:
            members=set(zipfile_obj.namelist())
            original=zipfile_obj.read("License.txt").decode("utf8","replace")
            if "CC0" not in original and "public domain" not in original.lower():
                raise RuntimeError("Unexpected Kenney license for "+key)
            for model in pack["models"]:
                member=f"Models/OBJ format/{model}.obj"
                if member not in members:raise FileNotFoundError(member)
                raw=zipfile_obj.read(member).decode("utf8")
                # These are authored meshes, not cubes. The material is
                # overridden by project YMAT, so original MTL refs are omitted.
                cleaned="\n".join(x for x in raw.splitlines()
                   if not x.startswith(("mtllib ","usemtl ")))+"\n"
                out=DEST/"models/industrial"/f"{key}_{model}.obj"
                atomic(out,cleaned.encode("utf8"))
                provenance["assets"].append({
                    "file":out.relative_to(DEST).as_posix(),
                    "source":pack["source"],"archive_entry":member,
                    "sha256":hashlib.sha256(cleaned.encode("utf8")).hexdigest(),
                })
    for name,maps in TEXTURES.items():
        for kind in maps:
            file=f"{name}_{kind}_1k.jpg"
            url=f"https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/{name}/{file}"
            raw=fetch(url,referer="https://polyhaven.com/a/"+name)
            if not raw.startswith(b"\xff\xd8\xff"):raise ValueError("Not JPEG: "+url)
            out=DEST/"textures/rustsea"/f"{name}_{kind}.jpg"
            atomic(out,raw)
            provenance["assets"].append({
                "file":out.relative_to(DEST).as_posix(),
                "source":"https://polyhaven.com/a/"+name,
                "download":url,"sha256":hashlib.sha256(raw).hexdigest(),
            })
    patch='''# Original CC0-linked Rust Sea ground plane (1x1 m, Y-up)
o rust_sea_ground_patch
v -0.5 0 -0.5
v -0.5 0 0.5
v 0.5 0 0.5
v 0.5 0 -0.5
vt 0 0
vt 0 1
vt 1 1
vt 1 0
vn 0 1 0
f 1/1/1 2/2/1 3/3/1
f 1/1/1 3/3/1 4/4/1
'''
    atomic(DEST/"models/terrain/wasteland_patch.obj",patch.encode("utf8"))
    xml=build_xml()
    atomic(DEST/"materials/rustsea.ymat.xml",xml)
    atomic(DEST/"materials/rustsea.ymat",build_ymat(xml))
    provenance["canonical_materials"]="materials/rustsea.ymat.xml"
    provenance["compiled_materials"]="materials/rustsea.ymat"
    provenance["notes"]=[
      "Kenney: https://kenney.nl/assets/factory-kit; https://kenney.nl/assets/city-kit-industrial",
      "Poly Haven texture normals use nor_dx (DirectX green-channel convention).",
      "Original source OBJ meshes are left geometrically unmodified.",
      "The generator uses visually separate models and keeps existing collision geometry.",
      "CC0 sources are included in the game project only; no copyrighted film art."
    ]
    atomic(DEST/"rustsea_asset_provenance.json",
           (json.dumps(provenance,indent=2,ensure_ascii=False)+"\n").encode("utf8"))
    print(json.dumps({"ok":True,"models":sum(1 for x in provenance["assets"] if x["file"].endswith(".obj")),
                      "texture_maps":sum(1 for x in provenance["assets"] if x["file"].endswith(".jpg")),
                      "destination":str(DEST)},ensure_ascii=False))
if __name__=="__main__":
    main()
