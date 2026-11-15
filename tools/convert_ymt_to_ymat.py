#!/usr/bin/env python3
"""One-time migration of NorthStarCpp material libraries from legacy .ymt to .ymat.

The transformation is structural:
 - NEF8 kind 10 (legacy material) becomes kind 4 (native material);
 - XML YmtMaterialLibrary/newviso.ymt schema becomes YmatMaterialLibrary/northstarcpp.ymat;
 - header metadata references and model YDD dependency references are updated;
 - body digests are recalculated with BLAKE3, header offsets recomputed;
 - an on-disk immutable backup of every source is retained.

Uses Python only as an offline asset conversion tool; no Python dependency is
introduced into the C++ runtime or build distribution.
Requires the blake3 package for cryptographic consistency with the native reader.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import zlib

try:
    from blake3 import blake3
except ImportError:
    raise SystemExit("Conversion requires Python package 'blake3' (pip install blake3)")

ROOT = Path(__file__).resolve().parents[1]
BACKUP = ROOT / ".northstarcpp" / "backups" / "ymat-canonical-2026-11-13"
MATERIALS = (
    "sharedAssets/materials/skydome",
    "sharedAssets/materials/fps/game_ready_world",
)
MODEL = "sharedAssets/models/skydome.ydd"
ROOT_SIZE = 128


class ConversionError(RuntimeError):
    pass


def read_nef8(raw: bytes) -> tuple[bytearray, dict, bytes]:
    if len(raw) < ROOT_SIZE or raw[:4] != b"NEF8":
        raise ConversionError("Not a NEF8 file")
    version, header, kind, codec, schema, reserved = struct.unpack_from("<HHHHHH", raw, 4)
    offset, compressed, uncompressed, entries, meta_offset, meta_size = struct.unpack_from("<6Q", raw, 16)
    if (version, header, codec, schema, reserved) != (1, 128, 1, 1, 0):
        raise ConversionError("Unsupported NEF8 file")
    if offset < 128 or compressed != len(raw) - offset or offset > len(raw):
        raise ConversionError("Invalid NEF8 ranges")
    if (meta_size and (meta_offset < 128 or meta_size > offset - meta_offset)) or (
        not meta_size and meta_offset != 0
    ):
        raise ConversionError("Invalid NEF8 metadata")
    body = zlib.decompress(raw[offset:], -15)
    if len(body) != uncompressed or blake3(body).digest() != raw[64:96]:
        raise ConversionError("Invalid NEF8 body digest")
    metadata = json.loads(raw[meta_offset:meta_offset + meta_size]) if meta_size else {}
    return bytearray(raw[:128]), metadata, body


def write_nef8(header: bytearray, metadata: dict, body: bytes, kind: int) -> bytes:
    meta = json.dumps(metadata, sort_keys=True, ensure_ascii=False,
                      separators=(",", ":")).encode("utf-8")
    compressor = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS)
    encoded = compressor.compress(body) + compressor.flush()
    offset = ROOT_SIZE + len(meta)
    struct.pack_into("<H", header, 8, kind)
    struct.pack_into("<6Q", header, 16, offset, len(encoded), len(body),
                     len(metadata.get("entries", [])), ROOT_SIZE, len(meta))
    header[64:96] = blake3(body).digest()
    result = bytes(header) + meta + encoded
    check_h, check_meta, check_body = read_nef8(result)
    if check_body != body or check_meta != metadata or struct.unpack_from("<H", check_h, 8)[0] != kind:
        raise ConversionError("NEF8 round-trip mismatch")
    return result


def converted_material(raw: bytes, relative: str) -> bytes:
    header, metadata, xml = read_nef8(raw)
    if struct.unpack_from("<H", header, 8)[0] != 10:
        raise ConversionError("Expected historical material kind 10")
    if metadata.get("content_kind") != "ymt_material_library":
        raise ConversionError("Expected historical YMT material library")
    old_xml = xml.decode("utf-8")
    if ("<YmtMaterialLibrary" not in old_xml or
            'schema="newviso.ymt.material_library.v1"' not in old_xml):
        raise ConversionError("Unexpected material schema")
    from pack_native_ymat import build_ymat
    import xml.etree.ElementTree as ET
    document = ET.fromstring(old_xml.replace(
        "YmtMaterialLibrary", "YmatMaterialLibrary"))
    document.attrib["version"] = "2"
    document.attrib["schema"] = "northstarcpp.ymat.material_library.v2"
    xml = ET.tostring(document, encoding="utf-8", xml_declaration=True)
    return build_ymat(xml)


def converted_model(raw: bytes) -> bytes:
    header, metadata, body = read_nef8(raw)
    if struct.unpack_from("<H", header, 8)[0] != 2:
        raise ConversionError("Expected YDD kind 2")
    text = body.decode("utf-8")
    if text.count("materials/skydome.ymt@") < 1:
        raise ConversionError("Model does not contain expected YMT material references")
    text = text.replace("materials/skydome.ymt@", "materials/skydome.ymat@")
    body = text.encode("utf-8")
    for dependency in metadata.get("dependencies", []):
        dependency["reference"] = dependency["reference"].replace(".ymt@", ".ymat@")
    for entry in metadata.get("entries", []):
        for dependency in entry.get("dependencies", []):
            dependency["reference"] = dependency["reference"].replace(".ymt@", ".ymat@")
    metadata["policy"] = [
        "YDD declares material slots as .ymat@entry",
        "runtime mesh bytes are DTO projection; render receives packets",
    ]
    return write_nef8(header, metadata, body, 2)


def verify() -> dict:
    results = {}
    for stem in MATERIALS:
        old = ROOT / (stem + ".ymt")
        path = ROOT / (stem + ".ymat")
        if old.exists() or not path.is_file():
            raise ConversionError(f"Material file is not exclusively YMАТ: {stem}")
        h, metadata, body = read_nef8(path.read_bytes())
        if struct.unpack_from("<H", h, 8)[0] != 4 or metadata:
            raise ConversionError(f"Non-native YMАТ material: {stem}")
        if (b"YmtMaterialLibrary" in body or b".ymt@" in body or
                b"northstarcpp.ymat.material_library.v2" not in body):
            raise ConversionError(f"Historical material schema still present: {stem}")
        results[stem] = {"size": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    model = ROOT / MODEL
    h, metadata, body = read_nef8(model.read_bytes())
    if b"skydome.ymt" in body or "skydome.ymt" in json.dumps(metadata):
        raise ConversionError("Skydome drawable still references the historical material")
    if b"materials/skydome.ymat@sky_cloud_clear" not in body:
        raise ConversionError("Skydome drawable lost its material reference")
    results[MODEL] = {"size": model.stat().st_size,
                      "sha256": hashlib.sha256(model.read_bytes()).hexdigest()}
    return results


def apply() -> dict:
    source = {rel+".ymt": ROOT / (rel+".ymt") for rel in MATERIALS}
    source[MODEL] = ROOT / MODEL
    if any(not p.is_file() for p in source.values()):
        raise ConversionError("Missing source material or drawable dictionary")
    if any((ROOT / (stem + ".ymat")).exists() for stem in MATERIALS):
        raise ConversionError("YMAT destination already exists, refusing to overwrite")
    converted = {}
    for rel in MATERIALS:
        old = rel+".ymt"
        converted[rel+".ymat"] = converted_material(source[old].read_bytes(), old)
    converted[MODEL] = converted_model(source[MODEL].read_bytes())

    BACKUP.mkdir(parents=True, exist_ok=True)
    for rel, path in source.items():
        dest = BACKUP / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            raise ConversionError("Existing migration backup cannot be overwritten")
        shutil.copy2(path, dest)
        if dest.read_bytes() != path.read_bytes():
            raise ConversionError("Backup is not identical")
    # Write and verify first; retire the old two files only after all writes succeed.
    try:
        for rel, raw in converted.items():
            dest = ROOT / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(raw)
        for stem in MATERIALS:
            h, m, b = read_nef8((ROOT / (stem + ".ymat")).read_bytes())
            if m or struct.unpack_from("<H", h, 8)[0] != 4 or (
                b"northstarcpp.ymat.material_library.v2" not in b
            ):
                raise ConversionError("New YMАТ file verification failed")
        for stem in MATERIALS:
            (ROOT / (stem + ".ymt")).unlink()
        return verify()
    except Exception:
        for rel, original in source.items():
            original.write_bytes((BACKUP / rel).read_bytes())
        for stem in MATERIALS:
            (ROOT / (stem + ".ymat")).unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--apply", action="store_true")
    group.add_argument("--verify", action="store_true")
    opts = parser.parse_args()
    try:
        result = verify() if opts.verify else apply()
        print(json.dumps({"operation": "verify" if opts.verify else "apply",
                          "files": result, "backup": str(BACKUP)}, indent=2))
        return 0
    except (ConversionError, ValueError, KeyError, OSError, zlib.error) as exc:
        parser.exit(2, f"YMAT migration failure: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
