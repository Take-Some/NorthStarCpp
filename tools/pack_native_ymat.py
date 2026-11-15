#!/usr/bin/env python3
"""Package a project material descriptor into native NEF8 kind-30 .ymat.

JSON is accepted only as *source input* for a one-time build/import operation.
The runtime asset and Content Browser resource are exclusively native YMAT.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import struct
import sys
import zlib

from blake3 import blake3

HEADER_SIZE = 128
MAX_BODY = 512 * 1024


def srgb_channel(component: int) -> float:
    c = component / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def material_to_native(doc: dict) -> dict:
    if doc.get("schema") != 1:
        raise ValueError("Expected authored material schema 1")
    name = doc["name"]
    if not isinstance(name, str) or not name or len(name) > 100:
        raise ValueError("Invalid native YMAT material name")
    color = doc["base_color"]
    if not isinstance(color, str) or len(color) != 7 or not color.startswith("#"):
        raise ValueError("Invalid base color")
    components = [srgb_channel(int(color[i:i+2], 16)) for i in (1, 3, 5)]
    def unit(name: str) -> float:
        v = float(doc[name])
        if not math.isfinite(v) or v < 0 or v > 1:
            raise ValueError("Invalid material factor: " + name)
        return v
    blend_mode = doc.get("blend_mode", "opaque")
    if blend_mode not in ("opaque", "masked", "translucent"):
        raise ValueError("Unsupported blending")
    shaders = ("standard_lit", "unlit")
    if doc.get("shader") not in shaders:
        raise ValueError("Unsupported material shader")
    params = [
        {"name": "base_color", "type": "color", "value": [*components, 1.0]},
        {"name": "metallic", "type": "float", "value": unit("metallic")},
        {"name": "roughness", "type": "float", "value": unit("roughness")},
    ]
    materials = {
        "name": name,
        "shader": doc["shader"],
        "blend": "alpha" if blend_mode == "translucent" else blend_mode,
        "alpha_cutoff": float(doc.get("alpha_cutoff", 0.5 if blend_mode == "masked" else 0.0)),
        "two_sided": bool(doc.get("two_sided", False)),
        "textures": [
            {"slot": slot.replace("_texture", ""), "ref": doc[slot], "required": True}
            for slot in ("base_color_texture", "normal_texture", "roughness_texture",
                         "metallic_texture", "emissive_texture")
            if doc.get(slot)
        ],
        "params": params,
    }
    if "graph" in doc:
        if not isinstance(doc["graph"], dict):
            raise ValueError("Invalid graph")
        materials["graph"] = doc["graph"]
    materials["editor_source"] = json.dumps(
        doc, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    )  # Qt numeric values must survive lossless YMAT round-trip.
    return materials


def build_ymat(materials: list[dict]) -> bytes:
    if not materials or len(materials) > 128:
        raise ValueError("YMAT must contain between 1 and 128 materials")
    body = json.dumps(
        {"schema": "northstar.ymat.v1",
         "editor_schema": "northstar.ymat.editor.v1",
         "materials": materials},
        ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")
    if len(body) > MAX_BODY:
        raise ValueError("Compiled YMAT exceeds source size limit")
    compressor = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS)
    packed = compressor.compress(body) + compressor.flush()
    header = bytearray(HEADER_SIZE)
    header[:4] = b"NEF8"
    struct.pack_into("<HHHHHH", header, 4, 1, 128, 30, 1, 1, 0)
    struct.pack_into("<6Q", header, 16, 128, len(packed), len(body),
                     len(materials), 0, 0)
    header[64:96] = blake3(body).digest()
    struct.pack_into("<Q", header, 112, 1)
    return bytes(header) + packed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.suffix.lower() != ".ymat":
        parser.error("Output must have the .ymat extension")
    doc = json.loads(args.input.read_text(encoding="utf-8"))
    encoded = build_ymat([material_to_native(doc)])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    tmp = args.output.with_suffix(args.output.suffix + ".tmp")
    try:
        with tmp.open("wb") as f:
            f.write(encoded)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, args.output)
    finally:
        tmp.unlink(missing_ok=True)
    print(f"Native NEF8 kind-30 YMAT: {args.output} ({len(encoded)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
