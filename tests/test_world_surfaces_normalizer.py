"""Deterministic native YTD repacking/repair regression for NorthStarCpp."""
from __future__ import annotations
from hashlib import sha256
from pathlib import Path
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.dev.normalize_world_surfaces import extract, load_input, normalize

EXPECTED_NAMES=[
    "ground_sand","ground_sand_normal","ground_sand_roughness",
    "rock_moss","rock_moss_normal","rock_moss_roughness",
    "terrain_forest_floor","terrain_forest_floor_normal",
    "terrain_forest_floor_roughness",
]
EXPECTED_MIPS=[12,10,12,11,9,10,11,8,10]
ORIGINAL_SHA256="334b740916ab50e99410116f9eb3dc8ca0343f2605bca88edab65c1379078a88"


def verify() -> None:
    source=ROOT/"sharedAssets/textures/world_surfaces.ytd"
    project_original=ROOT/"Projects/NorthStar_FPS_Test/assets/textures/world_surfaces.ytd"
    project_runtime=ROOT/"Projects/NorthStar_FPS_Test/assets/textures/world_surfaces_runtime.ytd"
    assert sha256(source.read_bytes()).hexdigest()==ORIGINAL_SHA256
    assert sha256(project_original.read_bytes()).hexdigest()==ORIGINAL_SHA256
    _,body=load_input(source)
    original,issues=extract(body)
    assert len(original)==9
    assert [item["name"] for item in original]==EXPECTED_NAMES
    assert issues,"Expected damaged YTD mip entries for regression"
    with tempfile.TemporaryDirectory(prefix="northstar-world-surfaces-") as tmp:
        built=Path(tmp)/"world_surfaces_runtime.ytd"
        report=normalize(source,built)
        assert report["textures"]
        assert [x["name"] for x in report["textures"]]==EXPECTED_NAMES
        assert [x["mips"] for x in report["textures"]]==EXPECTED_MIPS
        assert built.read_bytes()==project_runtime.read_bytes(), (
            "Runtime texture dictionary differs from deterministic normalized output")
        _,fixed_body=load_input(built)
        fixed,repairs=extract(fixed_body)
        assert len(fixed)==9
        assert not repairs, "Repaired dictionary remains inconsistent: "+str(repairs)
        for i,(item,recovered) in enumerate(zip(original,fixed)):
            assert item["name"]==recovered["name"]
            assert item["format"]==recovered["format"]==(102 if i%3==0 else 105)
            assert item["color"]==recovered["color"]==(2 if i%3==0 else 1)
            for (_,_,chunk),(_,_,new_chunk) in zip(item["mips"],recovered["mips"]):
                assert chunk==new_chunk
    # Regression never overwrites either original file.
    assert sha256(source.read_bytes()).hexdigest()==ORIGINAL_SHA256
    assert sha256(project_original.read_bytes()).hexdigest()==ORIGINAL_SHA256
    print("PASS: 9 original BCn maps, mip repair, deterministic YTD, unchanged sources")


if __name__=="__main__":
    verify()
