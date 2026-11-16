#!/usr/bin/env python3
"""Import NorthStar NVRM model material slots into canonical XML YMAT (NEF8).
Output files are project-owned. Original YDR, YTD and model metadata remain unchanged.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zlib

ROLE = {
    "base_color": "base_color",
    "diffuse": "base_color",
    "normal": "normal",
    "roughness": "roughness",
    "metallic": "metallic",
    "emissive": "emissive",
}


def read_nvrm(path: Path) -> dict:
    source = path.read_bytes()
    if len(source) < 64 or (source[:4] != b"NEF8" or source[4] != 2 or source[5] != 8):
        raise ValueError(f"{path.name}: expected NEF8 v2.8")
    kind = struct.unpack_from("<H", source, 6)[0]
    compressed, expanded = struct.unpack_from("<QQ", source, 16)
    if kind != 6 or compressed <= 0 or compressed > len(source) - 64 or expanded > 64 << 20:
        raise ValueError(f"{path.name}: invalid NVRM envelope")
    body = zlib.decompress(source[-compressed:], -15)
    if len(body) != expanded or body[:4] != b"NVRM":
        raise ValueError(f"{path.name}: invalid NVRM body")
    metadata_bytes = struct.unpack_from("<I", body, 8)[0]
    geometry_bytes = struct.unpack_from("<Q", body, 12)[0]
    if len(body) != 20 + metadata_bytes + geometry_bytes:
        raise ValueError(f"{path.name}: corrupt NVRM metadata")
    document = json.loads(body[20:20 + metadata_bytes])
    if document.get("schema") != "northstar.model.runtime.v1":
        raise ValueError(f"{path.name}: unknown NVRM schema")
    return document


def pack_ymat(xml: bytes, count: int) -> bytes:
    compressor = zlib.compressobj(level=9, wbits=-15)
    compressed = compressor.compress(xml) + compressor.flush()
    header = bytearray(64)
    struct.pack_into("<4sBBHHHIQQ", header, 0, b"NEF8", 2, 8, 4, 1, 1,
                     count, len(compressed), len(xml))
    return bytes(header) + compressed


def translate_texture(reference: str, project_assets: Path) -> str | None:
    if "@" not in reference:
        return None
    path, name = reference.split("@", 1)
    if "/" in name or "\\" in name or not name:
        return None
    prefix = "textures/vehicles/mahir_mt28/"
    if path.startswith(prefix):
        path = "textures/mahir_mt28/" + path[len(prefix):]
    relative = Path(path)
    if relative.is_absolute() or any(x in {".", ".."} for x in relative.parts):
        return None
    if not (project_assets / relative).is_file():
        return None
    return f"{path}@{name}"


def make_library(manifest: dict, project_assets: Path) -> tuple[bytes, int, int]:
    root = ET.Element("YmatMaterialLibrary", {
        "version": "2", "schema": "northstarcpp.ymat.material_library.v2"})
    unresolved = 0
    texture_count = 0
    seen = set()
    for material in manifest.get("material_slots", []):
        slot = material["slot"]
        if not isinstance(slot, int) or slot < 0 or slot in seen:
            raise ValueError("Duplicate or invalid material slot")
        seen.add(slot)
        name = f"mat_{slot}"
        src_blend = material.get("blend_mode", "opaque")
        blend = "alpha" if src_blend in {"translucent", "alpha"} else (
            "masked" if src_blend == "masked" else "opaque")
        m = ET.SubElement(root, "Material", name=name, shader="standard_lit")
        surface = material.get("shading", {})
        ET.SubElement(m, "Surface", {
            "blend": blend,
            "two_sided": "false",
            "alpha_cutoff": str(float(surface.get("alpha_cutoff", 0.5 if blend == "masked" else 0)))
                             if blend == "masked" else "0",
        })
        textures = ET.SubElement(m, "Textures")
        used = set()
        for texture in material.get("textures", []):
            role = ROLE.get(texture.get("role", ""))
            if role is None or role in used:
                continue
            reference = translate_texture(texture.get("asset_ref", ""), project_assets)
            if reference is None:
                unresolved += 1
                continue
            ET.SubElement(textures, "Texture", slot=role, ref=reference)
            used.add(role)
            texture_count += 1
        params = ET.SubElement(m, "Params")
        for parameter in material.get("parameters", []):
            name = parameter.get("name", "")
            value = parameter.get("value", [])
            if name not in {"base_color", "roughness", "metallic"}:
                continue
            if isinstance(value, (int, float)):
                value = [value]
            if not isinstance(value, list):
                continue
            count = 4 if name == "base_color" else 1
            if len(value) != count:
                continue
            ET.SubElement(params, "Param", name=name,
                          type="float4" if count == 4 else "float",
                          value=",".join(str(float(v)) for v in value))
    if not seen:
        raise ValueError("Model has no material slots")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True), len(seen), texture_count, unresolved


def generate(model_folder: Path, project_assets: Path, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    result = {"models": 0, "materials": 0, "texture_bindings": 0,
              "missing_texture_references": 0, "outputs": []}
    for model in sorted(model_folder.glob("*.ydr")):
        if "shadow" in model.stem.lower() or "collider" in model.stem.lower():
            continue
        metadata = read_nvrm(model)
        xml, count, textures, unresolved = make_library(metadata, project_assets)
        destination = output / (model.stem + ".ymat")
        destination.write_bytes(pack_ymat(xml, count))
        result["models"] += 1
        result["materials"] += count
        result["texture_bindings"] += textures
        result["missing_texture_references"] += unresolved
        result["outputs"].append(destination.name)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--model", default="mahir_mt28")
    args = parser.parse_args()
    project = args.project.resolve()
    assets = project / "assets"
    result = generate(assets / "models" / args.model, assets, assets / "materials" / args.model)
    print(json.dumps({k: v for k,v in result.items() if k != "outputs"}, indent=2))


if __name__ == "__main__":
    main()
