"""NEF8 Scene ListFile v2 — independently compressed streaming-cell chunks.

Metadata and per-cell integrity are checked before inflating individual blocks.
The full reader additionally verifies the global BLAKE3 hash.
"""
from __future__ import annotations
import struct
import zlib
from blake3 import blake3

MAX_BYTES = 16 * 1024 * 1024
HEADER_SIZE = 128
TABLE_ENTRY_SIZE = 60
RECORD_ENTRY_SIZE = 28
MAX_RECORDS = 65536


def _deflate(raw: bytes) -> bytes:
    encoder = zlib.compressobj(9, zlib.DEFLATED, -15, 8, zlib.Z_DEFAULT_STRATEGY)
    return encoder.compress(raw) + encoder.flush()


def _inflate(raw: bytes, size: int) -> bytes:
    if not 16 <= size <= MAX_BYTES or not raw:
        raise ValueError("Invalid NEF8 chunk size")
    decoder = zlib.decompressobj(-15)
    try:
        out = decoder.decompress(raw, size + 1)
    except zlib.error as exc:
        raise ValueError("Corrupted NEF8 compressed chunk") from exc
    if len(out) != size or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("Truncated/overexpanded NEF8 chunk")
    return out


def _chunk_body(records: list[tuple[int, int, bytes, int, int]]) -> bytes:
    if not 1 <= len(records) <= MAX_RECORDS:
        raise ValueError("Empty NEF8 component chunk")
    offset = 16 + len(records) * RECORD_ENTRY_SIZE
    header = bytearray(b"NSLR" + struct.pack("<HHII", 2, 0, len(records),
                                               RECORD_ENTRY_SIZE))
    payload = bytearray()
    for kind, entity_id, body, version, ordinal in records:
        if not 1 <= kind <= 65535 or not 1 <= version <= 65535 or not body:
            raise ValueError("Invalid NEF8 typed component")
        if not 0 <= entity_id < 2**64 or not 0 <= ordinal < MAX_RECORDS:
            raise ValueError("Invalid NEF8 entity id or ordinal")
        if len(body) > MAX_BYTES - offset:
            raise ValueError("NEF8 chunk exceeds size limit")
        header += struct.pack("<HHQIIII", kind, version, entity_id, offset,
                              len(body), zlib.crc32(body), ordinal)
        payload += body
        offset += len(body)
    return bytes(header + payload)


def pack_listfile(records: list[tuple[int, int, bytes]],
                  entity_cells: dict[int, str] | None = None,
                  cell_order: list[str] | None = None,
                  record_versions: dict[int,int] | None = None) -> bytes:
    if not 1 <= len(records) <= MAX_RECORDS or records[0][0] != 1:
        raise ValueError("NEF8 Scene ListFile requires the document record")
    entity_cells = entity_cells or {}
    record_versions = record_versions or {}
    grouped: dict[str, list[tuple[int,int,bytes,int,int]]] = {"": []}
    for cell in cell_order or []:
        if not cell or len(cell.encode("utf8")) > 96 or cell in grouped:
            raise ValueError("Invalid or duplicate NEF8 cell")
        grouped[cell] = []
    for ordinal, (kind, entity_id, body) in enumerate(records):
        cell = entity_cells.get(entity_id, "") if (kind == 2 or kind >= 7) else ""
        if cell not in grouped:
            raise ValueError("NEF8 component refers to unknown cell")
        grouped[cell].append((kind, entity_id, body,
                              record_versions.get(kind,1), ordinal))
    chunks = []
    for name, components in grouped.items():
        if not components:
            continue
        raw = _chunk_body(components)
        compressed = _deflate(raw)
        chunks.append((name, len(components), raw, compressed))
    if not chunks or chunks[0][0] != "":
        raise ValueError("Missing global Scene ListFile chunk")
    if len(chunks) > 8193:
        raise ValueError("Too many Scene ListFile chunks")
    index_bytes = 16 + sum(TABLE_ENTRY_SIZE + len(name.encode("utf8"))
                            for name, _, _, _ in chunks)
    if index_bytes > 1048576:
        raise ValueError("Scene cell index too large")
    offset = HEADER_SIZE + index_bytes
    table = bytearray(b"NSCK" + struct.pack("<HHII", 2, 0,
                                              len(chunks), TABLE_ENTRY_SIZE))
    concat = bytearray()
    for name, n, raw, compressed in chunks:
        utf8 = name.encode("utf8")
        table += struct.pack("<BBHIQII", int(bool(utf8)), 0, len(utf8), n,
                             offset, len(compressed), len(raw))
        table += blake3(raw).digest()
        table += struct.pack("<I", zlib.crc32(compressed))
        table += utf8
        concat += raw
        offset += len(compressed)
    if len(table) != index_bytes or offset > MAX_BYTES or len(concat) > MAX_BYTES:
        raise ValueError("Scene ListFile exceeds size limit")
    header = bytearray(HEADER_SIZE)
    header[:4] = b"NEF8"
    struct.pack_into("<6H", header, 4, 1, HEADER_SIZE, 6, 2, 2, 0)
    struct.pack_into("<6Q", header, 16, HEADER_SIZE + index_bytes,
                     sum(len(c) for _, _, _, c in chunks), len(concat),
                     len(records), HEADER_SIZE, index_bytes)
    header[64:96] = blake3(concat).digest()
    header[96:112] = blake3(table).digest()[:16]
    struct.pack_into("<Q", header, 112, 2)
    return bytes(header) + bytes(table) + b"".join(c for _, _, _, c in chunks)


def _chunks(data: bytes):
    if not HEADER_SIZE+16 < len(data) <= MAX_BYTES:
        raise ValueError("Invalid NEF8 Scene ListFile size")
    if struct.unpack_from("<6H", data, 4) != (1, HEADER_SIZE, 6, 2, 2, 0):
        raise ValueError("Wrong chunked NEF8 Scene ListFile schema")
    offset, compressed, expanded, count, table_offset, table_size = struct.unpack_from(
        "<6Q", data, 16)
    if (table_offset != HEADER_SIZE or not 16 <= table_size <= 1048576 or
        offset != HEADER_SIZE + table_size or
        offset + compressed != len(data) or
        not 1 <= expanded <= MAX_BYTES or not 1 <= count <= MAX_RECORDS or
        struct.unpack_from("<Q", data, 112)[0] != 2):
        raise ValueError("Invalid chunked NEF8 envelope")
    table = data[HEADER_SIZE:offset]
    if blake3(table).digest()[:16] != data[96:112]:
        raise ValueError("Scene chunk index BLAKE3 mismatch")
    if table[:4] != b"NSCK":
        raise ValueError("Invalid NSCK chunk index")
    major, minor, chunk_count, stride = struct.unpack_from("<HHII", table, 4)
    if (major, minor, stride) != (2, 0, TABLE_ENTRY_SIZE) or not 1 <= chunk_count <= 8193:
        raise ValueError("Unsupported scene chunk index schema")
    chunks = []
    pos = 16
    next_offset = offset
    total_expanded = total_records = 0
    seen = set()
    for i in range(chunk_count):
        if pos + TABLE_ENTRY_SIZE > len(table):
            raise ValueError("Truncated Scene chunk entry")
        kind, reserved, name_len, records, chunk_offset, clen, rlen = struct.unpack_from(
            "<BBHIQII", table, pos)
        digest = table[pos+24:pos+56]
        crc = struct.unpack_from("<I", table, pos+56)[0]
        pos += TABLE_ENTRY_SIZE
        if name_len > 96 or pos+name_len > len(table):
            raise ValueError("Invalid streaming cell name")
        name = table[pos:pos+name_len].decode("utf8")
        pos += name_len
        if ((i == 0 and (kind, name) != (0, "")) or
            (i > 0 and (kind != 1 or not name or name in seen)) or reserved):
            raise ValueError("Invalid or duplicate streaming cell")
        seen.add(name)
        if (not 1 <= records <= MAX_RECORDS or chunk_offset != next_offset or
            not clen or rlen < 16 or chunk_offset+clen > len(data)):
            raise ValueError("Invalid streaming cell chunk range")
        chunks.append((name, records, chunk_offset, clen, rlen, digest, crc))
        next_offset += clen
        total_expanded += rlen
        total_records += records
    if (pos != len(table) or next_offset != len(data) or
        total_expanded != expanded or total_records != count):
        raise ValueError("Scene chunk index totals mismatch")
    return chunks, count


def read_records(data: bytes) -> list[tuple[int,int,bytes]]:
    chunks, count = _chunks(data)
    sorted_records: list[tuple[int,int,int,bytes]] = []
    expanded_all = bytearray()
    for name, record_count, offset, clen, rlen, digest, crc in chunks:
        compressed = data[offset:offset+clen]
        if zlib.crc32(compressed) != crc:
            raise ValueError("Compressed Scene cell CRC mismatch")
        expanded = _inflate(compressed, rlen)
        if blake3(expanded).digest() != digest:
            raise ValueError("Scene cell BLAKE3 mismatch")
        expanded_all += expanded
        if expanded[:4] != b"NSLR":
            raise ValueError("Invalid Scene chunk records")
        major, minor, n, stride = struct.unpack_from("<HHII", expanded, 4)
        if (major, minor, n, stride) != (2, 0, record_count, RECORD_ENTRY_SIZE):
            raise ValueError("Scene chunk record directory mismatch")
        cursor = 16 + n * RECORD_ENTRY_SIZE
        if cursor > len(expanded):
            raise ValueError("Truncated Scene component index")
        for i in range(n):
            kind, version, entity, position, length, record_crc, ordinal = struct.unpack_from(
                "<HHQIIII", expanded, 16 + i * RECORD_ENTRY_SIZE)
            if (kind < 1 or not version or position != cursor or not length or
                length > len(expanded)-cursor or ordinal >= count):
                raise ValueError("Invalid Scene component bounds")
            payload = expanded[cursor:cursor+length]
            if zlib.crc32(payload) != record_crc:
                raise ValueError("NEF8 Scene ListFile component CRC mismatch")
            sorted_records.append((ordinal, kind, entity, payload))
            cursor += length
        if cursor != len(expanded):
            raise ValueError("Trailing Scene chunk component payload")
    if blake3(expanded_all).digest() != data[64:96]:
        raise ValueError("NEF8 Scene ListFile global BLAKE3 mismatch")
    if len(sorted_records) != count:
        raise ValueError("Scene component count mismatch")
    sorted_records.sort()
    if any(index != ordinal for index, (ordinal, _, _, _) in enumerate(sorted_records)):
        raise ValueError("Duplicate/missing Scene component ordinal")
    return [(kind, entity, payload) for _, kind, entity, payload in sorted_records]


def validate(data: bytes) -> int:
    return len(read_records(data))


def cell_records(data: bytes, cell: str):
    """Verify/inflate only a named cell; does not validate other chunks."""
    chunks, _ = _chunks(data)
    for name, record_count, offset, clen, rlen, digest, crc in chunks:
        if name != cell:
            continue
        compressed = data[offset:offset+clen]
        if zlib.crc32(compressed) != crc:
            raise ValueError("Cell compressed CRC mismatch")
        expanded = _inflate(compressed, rlen)
        if blake3(expanded).digest() != digest:
            raise ValueError("Cell BLAKE3 mismatch")
        # This function deliberately returns the component chunk bytes so
        # clients can decode only that indexed section without other cells.
        return expanded
    return None
