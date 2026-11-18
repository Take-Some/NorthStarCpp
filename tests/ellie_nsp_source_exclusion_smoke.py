#!/usr/bin/env python3
"""Regression: do not pack raw proprietary PAK, VRAM, ICE sources into NSP.

The independent native NEF8 YDD/YMT/YMAT/YTD remain packable.
"""
from pathlib import Path
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from pack_nsp import inventory

with tempfile.TemporaryDirectory(prefix="northstar-ellie-pack-QA-") as d:
    source=Path(d)
    original=[
        "characters/ellie/source/actor97/ellie-head.pak",
        "characters/ellie/source/native_vram/ellie-head.vram",
        "characters/ellie/source/native_animations/a.iceanim",
        "characters/abby/source/private.pak",
    ]
    native=[
        "characters/ellie/models/ellie.ymt",
        "characters/ellie/models/ellie_preview.ydd",
        "characters/ellie/materials/ellie.ymat",
        "characters/ellie/textures/ellie.ytd",
    ]
    for name in original+native:
        p=source/name
        p.parent.mkdir(parents=True,exist_ok=True)
        p.write_bytes(name.encode("utf-8"))
    packed={name for name,_,_,_ in inventory(source)}
    assert packed==set(native),f"Raw character sources leaked into NSP inventory: {packed}"
print("PASS: proprietary character source files never included in runtime NSP; native converted YDD/YMT/YMAT/YTD remain eligible")
