"""NorthStar Scene V3 NEF8 ListFile authoring for Python project templates.

New scenes use the engine's common NEF8 envelope, kind=6, and individually
indexed, typed component records. Existing NSCN v1 files remain readable by
the native C++ loader. This module intentionally writes only starter scenes.
"""
from __future__ import annotations

import re
import shlex
import struct
import zlib
from blake3 import blake3

MAX_SCENE_BYTES = 16 * 1024 * 1024
HEADER_SIZE = 128
ENTRY_SIZE = 24
KIND_SCENE = 6


def _text(value: str) -> bytes:
    raw = value.encode("utf-8")
    if len(raw) > 65535:
        raise ValueError("Scene ListFile string too long")
    return struct.pack("<H", len(raw)) + raw


def _vec(x: float, y: float, z: float) -> bytes:
    return struct.pack("<fff", float(x), float(y), float(z))


def _pack_listfile_v1(records: list[tuple[int, int, bytes]]) -> bytes:
    """Create the canonical per-chunk NEF8 v2 envelope for scene templates.

    Python starter templates have no streaming cells: their records live in
    one global chunk. Native C++ handles general per-cell scene authoring.
    """
    from .scene_nef8_chunked import pack_chunked_templates
    return pack_chunked_templates(records)


def _validate_nef8_scene_v1(data: bytes) -> int:
    """Bounded integrity/structure check for build_game and migration tools.

    Does not replace semantic validation by the native Scene codec.
    Returns the number of typed components.
    """
    if len(data) >= HEADER_SIZE and data[:4] == b"NEF8" and struct.unpack_from("<H", data, 10)[0] == 2:
        from .scene_nef8_chunked import read_chunked_records
        return len(read_chunked_records(data))
    if len(data) < HEADER_SIZE + 1 or len(data) > MAX_SCENE_BYTES or data[:4] != b"NEF8":
        raise ValueError("Not a bounded NEF8 Scene ListFile")
    if struct.unpack_from("<6H", data, 4) != (1, HEADER_SIZE, KIND_SCENE, 1, 1, 0):
        raise ValueError("Wrong NEF8 scene kind, schema or compression")
    start, compressed, expanded, count, meta_off, meta_len = struct.unpack_from("<6Q", data, 16)
    if (start != HEADER_SIZE or compressed != len(data)-HEADER_SIZE or
            not 1 <= expanded <= MAX_SCENE_BYTES or not 1 <= count <= 65536 or
            meta_off or meta_len or struct.unpack_from("<Q", data, 112)[0] != 1):
        raise ValueError("Invalid NEF8 scene envelope")
    stream = zlib.decompressobj(-15)
    try:
        body = stream.decompress(data[HEADER_SIZE:], expanded + 1)
    except zlib.error as exc:
        raise ValueError("Corrupted Scene ListFile compression") from exc
    if (len(body) != expanded or not stream.eof or
            stream.unused_data or stream.unconsumed_tail):
        raise ValueError("Truncated or overexpanded NEF8 scene")
    if blake3(body).digest() != data[64:96]:
        raise ValueError("Scene ListFile BLAKE3 mismatch")
    if len(body) < 16 or body[:4] != b"NSLR":
        raise ValueError("Invalid scene record table")
    major, minor, directory_count, stride = struct.unpack_from("<HHII", body, 4)
    if ((major, minor, directory_count, stride) != (1, 0, count, ENTRY_SIZE) or
            16 + count * ENTRY_SIZE > len(body)):
        raise ValueError("Invalid Scene ListFile record directory")
    cursor = 16 + count * ENTRY_SIZE
    for index in range(count):
        kind, version, stable_id, offset, length, crc = struct.unpack_from(
            "<HHQIII", body, 16 + index * ENTRY_SIZE)
        if not 1 <= kind <= 6 or version != 1 or offset != cursor or not length:
            raise ValueError("Invalid typed Scene ListFile entry")
        if length > len(body)-cursor:
            raise ValueError("Scene ListFile entry outside data")
        if zlib.crc32(body[cursor:cursor+length]) != crc:
            raise ValueError("Scene ListFile component CRC mismatch")
        if kind != 2 and stable_id != 0:
            raise ValueError("Unexpected stable ID on non-entity record")
        cursor += length
    if cursor != len(body):
        raise ValueError("Trailing Scene ListFile bytes")
    return count


def encode_legacy_template(text: str) -> bytes:
    """Import a small text starter scene and emit NEF8 typed components."""
    lines = text.splitlines()
    if not lines or lines[0] != "NorthStarCpp.Scene 2":
        raise ValueError("Unsupported project starter scene")
    objects: list[tuple[int, int, bytes]] = []
    for row in lines[1:]:
        if not row.strip():
            continue
        columns = shlex.split(row)
        if len(columns) != 9 or columns[0] != "cube":
            raise ValueError("Unsupported project starter object")
        _, name, sx, sy, sz, ssize, scolor, splayer, smass = columns
        pos = [float(sx), float(sy), float(sz)]
        size = float(ssize)
        mass = float(smass)
        color = int(scolor, 16)
        player = int(splayer)
        if (not re.fullmatch(r"[\w .+-]{1,96}", name) or
                color < 0 or color > 0xffffff or player not in (0, 1)):
            raise ValueError("Invalid project template values")
        i = len(objects) + 1
        body = (
            _text(name) + _vec(*pos) + struct.pack("<fIBfQQ", size, color,
                player, mass, i, 0) +
            _text("Default") + _text("") + _text("") + _text("") +
            _text("") + _vec(0, 0, 0) + _vec(1, 1, 1) +
            struct.pack("<ffBB", 0.0, 1000000.0, 1, 1)
        )
        objects.append((2, i, body))
    document = _text("Untitled") + struct.pack("<fffII", 150., 220., 1., 64, 64)
    return pack_listfile([(1, 0, document), *objects])


def _read_nef8_scene_records_v1(data: bytes) -> list[tuple[int, int, bytes]]:
    """Return bounded scene component records after validating the full container."""
    if len(data) >= HEADER_SIZE and data[:4] == b"NEF8" and struct.unpack_from("<H", data, 10)[0] == 2:
        from .scene_nef8_chunked import read_chunked_records
        return read_chunked_records(data)
    count = _validate_nef8_scene_v1(data)
    # Size and BLAKE3 integrity have already been verified with a bounded decode.
    expanded = struct.unpack_from("<Q", data, 32)[0]
    inflater = zlib.decompressobj(-15)
    body = inflater.decompress(data[HEADER_SIZE:], expanded + 1)
    result: list[tuple[int, int, bytes]] = []
    for i in range(count):
        kind, version, stable_id, offset, size, checksum = struct.unpack_from(
            "<HHQIII", body, 16 + i * ENTRY_SIZE)
        result.append((kind, stable_id, body[offset:offset + size]))
    return result


def scene_entity_references(data: bytes) -> list[tuple[str, str]]:
    """Extract (mesh asset, script) from a version-1 entity component record."""
    references: list[tuple[str, str]] = []
    for kind, _, payload in read_nef8_scene_records(data):
        if kind != 2:
            continue
        cursor = 0

        def read_text() -> str:
            nonlocal cursor
            if cursor + 2 > len(payload):
                raise ValueError("Truncated entity string")
            size = struct.unpack_from("<H", payload, cursor)[0]
            cursor += 2
            if cursor + size > len(payload):
                raise ValueError("Truncated entity string payload")
            result = payload[cursor:cursor + size].decode("utf-8")
            cursor += size
            return result

        read_text()  # entity name
        cursor += 12 + 4 + 4 + 1 + 4 + 8 + 8  # transform, proxy, state, IDs
        for _ in range(3):
            read_text()  # layer, streaming cell, room
        asset = read_text()
        script = read_text()
        references.append((asset, script))
    return references


# NEF8 v2 is the canonical writer; the legacy v1 parser is retained to
# support projects created before chunked streaming cells were introduced.
from tools.dev import scene_binary_v2 as _chunked


def pack_listfile(records: list[tuple[int,int,bytes]],
                  entity_cells: dict[int,str] | None = None,
                  cell_order: list[str] | None = None) -> bytes:
    return _chunked.pack_listfile(records, entity_cells, cell_order)


def validate_nef8_scene(data: bytes) -> int:
    if len(data)<14 or data[:4]!=b"NEF8":
        raise ValueError("Expected NEF8 Scene ListFile")
    schema=struct.unpack_from("<H",data,12)[0]
    if schema==1:
        return _validate_nef8_scene_v1(data)
    if schema==2:
        return _chunked.validate(data)
    raise ValueError("Unsupported NEF8 Scene ListFile schema")


def read_nef8_scene_records(data: bytes) -> list[tuple[int,int,bytes]]:
    validate_nef8_scene(data)
    if struct.unpack_from("<H",data,12)[0]==1:
        return _read_nef8_scene_records_v1(data)
    return _chunked.read_records(data)
