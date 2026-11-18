#!/usr/bin/env python3
"""Sky/cloud extraction QA: 22 PAKs -> 101 real DDS -> 99 PNGs -> 2 YTDs.

No renderer or full-res visual parity claims. Pure file-level verification.
"""
from pathlib import Path
import hashlib,json,struct,sys,xml.etree.ElementTree as ET,zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from import_ellie_shared_assets import check_nef8
from native_ytd import validate_ytd
from pack_nsp import inventory
from native_sky import FMT,COMPRESSED

BASE=ROOT/"engine/northstarcpp/resources/sharedAssets/textures/sky"
m=json.loads((BASE/"source/sky.manifest.json").read_text("utf8"))
assert m["original_sky_paks"]==22
assert m["source_vram_descriptors"]==198
assert m["source_unique_textures"]==101
assert len(m["original_textures"])==101
expected_roles={"sky":57,"clouds":42}
for role,count in expected_roles.items():
    p=BASE/(role+".ytd")
    blob=check_nef8(p,1)
    result=validate_ytd(blob,count)
    assert result["textures"]==count
    assert hashlib.sha256(blob).hexdigest()==m["native"][role]["sha256"]
    assert result["mip_levels"]==m["native"][role]["mip_levels"]
    assert result["pixel_bytes"]==m["native"][role]["pixel_bytes"]
    assert m["native"][role]["source_textures"]==count

xml=ET.parse(BASE/"sky_sources.xml").getroot()
assert xml.tag=="NorthStarSkySources"
presets=xml.findall("Preset")
assert len(presets)==22
original_by_name={p["id"]:p for p in m["presets"]}
assert len(original_by_name)==22
refcount=0
seen_roles={"sky":set(),"clouds":set()}
for preset in presets:
    ident=preset.attrib["id"]
    assert ident in original_by_name
    original=original_by_name[ident]
    entries=preset.findall("Binding")
    assert len(entries)==len(original["bindings"])
    for element,expected in zip(entries,original["bindings"]):
        assert element.attrib["slot"]==expected["slot"]
        assert element.attrib["hash64"]==expected["hash64"]
        role=element.attrib["category"]
        if role not in seen_roles:
            assert "texture" not in element.attrib
            continue
        assert element.attrib["texture"]==(
            f"textures/sky/{role}.ytd@tex_{expected['hash64']}")
        seen_roles[role].add(expected["hash64"])
        refcount+=1
assert {k:len(v) for k,v in seen_roles.items()}==expected_roles

originals={d["hash64"]:d for d in m["original_textures"]}
assert len(originals)==101
valid_pngs=0
for h,entry in originals.items():
    dds=Path(entry["lossless_dds"])
    raw=dds.read_bytes()
    assert hashlib.sha256(raw).hexdigest()==entry["dds_sha256"]
    assert len(raw)>=148 and raw[:4]==b"DDS "
    assert struct.unpack_from("<I",raw,4)[0]==124
    assert struct.unpack_from("<I",raw,76)[0]==32
    assert raw[84:88]==b"DX10"
    assert struct.unpack_from("<I",raw,128)[0]==entry["original_dxgi"]
    assert struct.unpack_from("<II",raw,12)==(entry["height"],entry["width"])
    assert struct.unpack_from("<I",raw,28)[0]==entry["mip_count"]
    fmt=entry["original_dxgi"]
    expected_pixel_bytes=0
    width,height=entry["width"],entry["height"]
    for i in range(entry["mip_count"]):
        if fmt in COMPRESSED:
            expected_pixel_bytes+=((width+3)//4)*((height+3)//4)*FMT[fmt]
        else:expected_pixel_bytes+=width*height*FMT[fmt]
        width=max(1,width//2);height=max(1,height//2)
    assert len(raw)==148+expected_pixel_bytes
    if fmt==95:
        # The original original HDR compressed BC6H blocks MUST be preserved.
        assert raw[148:164]!=bytes(16)
    for role in entry["roles"]:
        if role not in seen_roles:continue
        pngs=list((BASE/"source/previews"/role).glob("*__"+h+".png"))
        assert len(pngs)==1,(role,h)
        png=pngs[0].read_bytes()
        assert png[:8]==b"\x89PNG\r\n\x1a\n"
        assert struct.unpack_from(">II",png,16)==(entry["width"],entry["height"])
        # First IDAT contents are scanlines with PNG filter type 0.
        idat_offset=33
        assert png[idat_offset+4:idat_offset+8]==b"IDAT"
        n=struct.unpack_from(">I",png,idat_offset)[0]
        scan=zlib.decompress(png[idat_offset+8:idat_offset+8+n])
        assert len(scan)==entry["height"]*(entry["width"]*4+1)
        assert all(scan[row*(entry["width"]*4+1)]==0 for row in range(entry["height"]))
        assert any(b for b in scan)
        valid_pngs+=1
assert valid_pngs==99

# Static inventory policy must not bundle source research DDS and PNG.
import tempfile
with tempfile.TemporaryDirectory(prefix="nsp-sky-qa-") as td:
    root=Path(td)
    names=[
        "textures/sky/source/dds/private.dds",
        "textures/sky/source/previews/clouds/private.png",
        "textures/sky/sky.ytd",
        "textures/sky/clouds.ytd",
    ]
    for name in names:
        path=root/name
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(name.encode("utf-8"))
    packed={name for name,_,_,_ in inventory(root)}
    assert packed==set(names[2:]),packed

print("PASS: 22 original sky PAKs, 198 VRAM entries, 101 lossless DDS (BC6H HDR preserved), "
      "57 sky + 42 cloud YTD records, 99 decoded PNG previews, source material refs complete, "
      "source DDS excluded from NSP")
