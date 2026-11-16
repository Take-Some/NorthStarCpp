#!/usr/bin/env python3
"""Validate and summarize NorthStar NEF8 kind-7 NVRM model resources."""
import argparse
import json
import pathlib
import struct
import zlib


def u32(raw, offset):
    return struct.unpack_from("<I", raw, offset)[0]


def u64(raw, offset):
    return struct.unpack_from("<Q", raw, offset)[0]


def inspect(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"NEF8" or data[4] != 2 or data[5] != 8:
        raise ValueError("not an NEF8 v2.8 resource")
    if struct.unpack_from("<H", data, 6)[0] != 6:
        raise ValueError("expected NEF8 kind 6 model")
    compressed, expanded = u64(data, 16), u64(data, 24)
    if not 0 < compressed <= len(data) - 64 or expanded > 64 * 1024 * 1024:
        raise ValueError("invalid compressed model size")
    raw = zlib.decompress(data[-compressed:], -15)
    if len(raw) != expanded or len(raw) < 20 or raw[:4] != b"NVRM" or u32(raw, 4) != 1:
        raise ValueError("invalid NVRM body")
    json_size, geometry_size = u32(raw, 8), u64(raw, 12)
    if 20 + json_size + geometry_size != len(raw):
        raise ValueError("NVRM geometry length mismatch")
    manifest = json.loads(raw[20:20+json_size])
    if manifest.get("schema") != "northstar.model.runtime.v1":
        raise ValueError("unsupported NVRM schema")
    vertices = triangles = 0
    payload = raw[20+json_size:]
    for mesh in manifest["meshes"]:
        count = mesh["vertex_count"]
        if count <= 0 or count > 200000:
            raise ValueError("mesh vertex budget exceeded")
        for stream in mesh.get("streams", []):
            off, size = stream["offset"], stream["length"]
            if off < 0 or size < 0 or off + size > len(payload):
                raise ValueError("invalid stream range")
            if size != count * stream["stride"]:
                raise ValueError("invalid stream length")
        indices = mesh["index_buffer"]
        offset, size = indices["offset"], indices["length"]
        if indices["format"] != "u32" or size != indices["index_count"] * 4:
            raise ValueError("unsupported index format")
        if offset < 0 or size < 0 or offset + size > len(payload):
            raise ValueError("invalid indices")
        for primitive in mesh.get("primitives", []):
            first, count_indices = primitive["first_index"], primitive["index_count"]
            if first < 0 or count_indices <= 0 or count_indices % 3 or first + count_indices > indices["index_count"]:
                raise ValueError("invalid primitive range")
            triangles += count_indices // 3
        vertices += count
    return {"file": path.name, "meshes": len(manifest["meshes"]),
            "vertices": vertices, "triangles": triangles,
            "material_slots": len(manifest.get("material_slots", [])),
            "payload_bytes": len(payload)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    args = parser.parse_args()
    files = sorted(args.source.glob("*.ydr")) if args.source.is_dir() else [args.source]
    records = []
    for file in files:
        try:
            records.append(inspect(file))
        except Exception as error:
            print(f"ERROR {file.name}: {error}")
            raise SystemExit(1) from error
    print(json.dumps({"files": len(records),
                      "vertices": sum(r["vertices"] for r in records),
                      "triangles": sum(r["triangles"] for r in records),
                      "largest": sorted(records, key=lambda r: -r["triangles"])[:12]}, indent=2))


if __name__ == "__main__":
    main()
