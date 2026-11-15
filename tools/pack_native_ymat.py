#!/usr/bin/env python3
"""Pack native XML YMAT into canonical NEF8 kind-4.

Input must be XML using the NorthStar YmatMaterialLibrary schema.
This tool never produces JSON-based kind-30 material libraries.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET
import zlib

from blake3 import blake3

MAX_BYTES = 64 * 1024 * 1024


def build_ymat(xml: bytes) -> bytes:
    if not xml or len(xml) > MAX_BYTES:
        raise ValueError("XML YMAT exceeds source size limit")
    root = ET.fromstring(xml)
    if root.tag != "YmatMaterialLibrary":
        raise ValueError("YMAT XML must contain YmatMaterialLibrary")
    if (root.attrib.get("schema") != "northstarcpp.ymat.material_library.v2"
            or root.attrib.get("version") != "2"):
        raise ValueError("Expected canonical YMAT XML schema v2")
    materials = root.findall("Material")
    if not materials or len(materials) > 65536:
        raise ValueError("Invalid XML YMAT material count")
    names = [m.attrib.get("name") for m in materials]
    if any(not name for name in names) or len(names) != len(set(names)):
        raise ValueError("XML YMAT requires unique material names")
    compressor = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS)
    payload = compressor.compress(xml) + compressor.flush()
    header = bytearray(128)
    header[:4] = b"NEF8"
    struct.pack_into("<HHHHHH", header, 4, 1, 128, 4, 1, 1, 0)
    struct.pack_into("<6Q", header, 16, 128, len(payload),
                     len(xml), len(materials), 0, 0)
    header[64:96] = blake3(xml).digest()
    struct.pack_into("<Q", header, 112, 1)
    return bytes(header) + payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True,
                        help="Canonical XML material library")
    parser.add_argument("--output", type=Path, required=True,
                        help="Native .ymat output")
    args = parser.parse_args()
    if args.input.suffix.lower() != ".xml":
        parser.error("YMAT authoring input must be XML")
    if args.output.suffix.lower() != ".ymat":
        parser.error("Material output must have the .ymat extension")
    encoded = build_ymat(args.input.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(".ymat.tmp")
    if temporary.exists():
        raise FileExistsError("Migration temporary file already exists")
    try:
        with temporary.open("xb") as out:
            out.write(encoded)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, args.output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"XML NEF8 YMAT kind 4: {args.output} ({len(encoded)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
