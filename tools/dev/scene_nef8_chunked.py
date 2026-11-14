"""Bounded reader and deterministic starter-template writer for NEF8 Scene ListFile v2.

The native implementation in scene_nef8_chunked.cpp is authoritative.
The Python writer deliberately authors global-only Scene V3 template records.
The reader supports cell-specific compressed blocks and entity records.
"""
from __future__ import annotations

import struct
import zlib
from blake3 import blake3

MAX_SIZE = 16 * 1024 * 1024
HEADER_SIZE = 128
MAX_INDEX = 1024 * 1024
CHUNK_ENTRY_SIZE = 60
RECORD_ENTRY_SIZE = 28
MAX_RECORDS = 65536
MAX_CHUNKS = 8193


def _raw_compress(data: bytes) -> bytes:
    z = zlib.compressobj(9, zlib.DEFLATED, -15)
    return z.compress(data) + z.flush()


def _raw_decompress(data: bytes, expected: int) -> bytes:
    if not (1 <= expected <= MAX_SIZE and len(data) <= MAX_SIZE):
        raise ValueError("Invalid compressed Scene chunk size")
    z = zlib.decompressobj(-15)
    try:
        output = z.decompress(data, expected + 1)
    except zlib.error as exc:
        raise ValueError("Corrupted Scene chunk compression") from exc
    if (len(output) != expected or not z.eof or z.unused_data or
            z.unconsumed_tail):
        raise ValueError("Invalid Scene chunk expanded length")
    return output


def pack_chunked_templates(records: list[tuple[int, int, bytes]]) -> bytes:
    """Produce a single global NSCK chunk for small Python starter templates."""
    if not (1 <= len(records) <= MAX_RECORDS and records[0][0] == 1):
        raise ValueError("Scene template requires a document record first")
    plain = bytearray(b"NSLR" + struct.pack(
        "<HHII", 2, 0, len(records), RECORD_ENTRY_SIZE))
    payload = bytearray()
    cursor = 16 + RECORD_ENTRY_SIZE * len(records)
    for ordinal, (kind, stable_id, body) in enumerate(records):
        if not (1 <= kind <= 6 and 0 <= stable_id < 2**64
                and isinstance(body, bytes) and len(body) > 0
                and cursor + len(body) <= MAX_SIZE):
            raise ValueError("Invalid template scene component")
        plain += struct.pack(
            "<HHQIIII", kind, 1, stable_id, cursor, len(body),
            zlib.crc32(body), ordinal)
        payload += body
        cursor += len(body)
    plain += payload
    if len(plain) > MAX_SIZE:
        raise ValueError("Scene template exceeds uncompressed size limit")
    zipped = _raw_compress(bytes(plain))
    table_size = 16 + CHUNK_ENTRY_SIZE
    payload_offset = HEADER_SIZE + table_size
    if payload_offset + len(zipped) > MAX_SIZE:
        raise ValueError("Scene template exceeds compressed size limit")
    table = bytearray(b"NSCK" + struct.pack("<HHII", 2, 0, 1, CHUNK_ENTRY_SIZE))
    table += struct.pack(
        "<BBHIQII32sI", 0, 0, 0, len(records), payload_offset,
        len(zipped), len(plain), blake3(plain).digest(), zlib.crc32(zipped))
    assert len(table) == table_size
    header = bytearray(HEADER_SIZE)
    header[:4] = b"NEF8"
    struct.pack_into("<6H", header, 4, 1, HEADER_SIZE, 6, 2, 2, 0)
    struct.pack_into("<6Q", header, 16, payload_offset, len(zipped),
                     len(plain), len(records), HEADER_SIZE, table_size)
    header[64:96] = blake3(plain).digest()
    header[96:112] = blake3(table).digest()[:16]
    struct.pack_into("<Q", header, 112, 2)
    return bytes(header) + bytes(table) + zipped


def read_chunked_records(data: bytes) -> list[tuple[int, int, bytes]]:
    """Validate full v2 envelope/index/blocks and return records in authored order."""
    if not (HEADER_SIZE + 16 <= len(data) <= MAX_SIZE and data[:4] == b"NEF8"):
        raise ValueError("Invalid NEF8 chunked scene length or magic")
    if (struct.unpack_from("<6H", data, 4) != (1, HEADER_SIZE, 6, 2, 2, 0)
            or struct.unpack_from("<Q", data, 112)[0] != 2):
        raise ValueError("Unsupported NEF8 chunked scene header")
    (start, compressed, expanded, record_count,
     meta_start, meta_len) = struct.unpack_from("<6Q", data, 16)
    if not (1 <= record_count <= MAX_RECORDS and 1 <= expanded <= MAX_SIZE
            and meta_start == HEADER_SIZE and 16 <= meta_len <= MAX_INDEX
            and start == HEADER_SIZE + meta_len
            and start <= len(data) and compressed == len(data) - start):
        raise ValueError("Invalid Scene chunk index ranges")
    table = data[HEADER_SIZE:start]
    if blake3(table).digest()[:16] != data[96:112]:
        raise ValueError("Scene chunk table checksum mismatch")
    if table[:4] != b"NSCK" or struct.unpack_from("<HH", table, 4) != (2, 0):
        raise ValueError("Unsupported Scene chunk index version")
    count, stride = struct.unpack_from("<II", table, 8)
    if not (1 <= count <= MAX_CHUNKS and stride == CHUNK_ENTRY_SIZE):
        raise ValueError("Invalid Scene chunk count/stride")
    pos = 16
    next_offset = start
    records: dict[int, tuple[int, int, bytes]] = {}
    cell_names: set[str] = set()
    full_plain = bytearray()
    total_plain = 0
    total_records = 0
    for chunk_index in range(count):
        if pos + CHUNK_ENTRY_SIZE > len(table):
            raise ValueError("Truncated Scene chunk index")
        (kind, reserved, name_len, chunk_records, offset,
         zip_len, plain_len, digest, compressed_crc) = struct.unpack_from(
            "<BBHIQII32sI", table, pos)
        pos += CHUNK_ENTRY_SIZE
        if name_len > 96 or name_len > len(table) - pos:
            raise ValueError("Invalid Scene chunk cell name")
        name_bytes = table[pos:pos + name_len]
        pos += name_len
        try:
            cell = name_bytes.decode("utf-8")
        except UnicodeError as exc:
            raise ValueError("Invalid Scene cell name encoding") from exc
        if not ((chunk_index == 0 and kind == 0 and name_len == 0)
                or (chunk_index > 0 and kind == 1 and name_len > 0)):
            raise ValueError("Scene chunk category mismatch")
        if chunk_index and (not cell or "\x00" in cell or cell in cell_names):
            raise ValueError("Repeated or malformed Scene cell name")
        cell_names.add(cell)
        if (reserved or not 1 <= chunk_records <= MAX_RECORDS or
                offset != next_offset or zip_len == 0 or plain_len < 16 or
                plain_len > MAX_SIZE - total_plain or
                zip_len > len(data) - next_offset):
            raise ValueError("Scene chunk range/count is invalid")
        compressed_body = data[offset:offset + zip_len]
        if zlib.crc32(compressed_body) != compressed_crc:
            raise ValueError("Scene chunk compressed checksum mismatch")
        plain = _raw_decompress(compressed_body, plain_len)
        if blake3(plain).digest() != digest:
            raise ValueError("Scene chunk BLAKE3 mismatch")
        if plain[:4] != b"NSLR" or struct.unpack_from("<HH", plain, 4) != (2, 0):
            raise ValueError("Invalid Scene chunk record signature")
        count_in_chunk, entry_stride = struct.unpack_from("<II", plain, 8)
        if (count_in_chunk != chunk_records or entry_stride != RECORD_ENTRY_SIZE or
                16 + count_in_chunk * entry_stride > len(plain)):
            raise ValueError("Invalid Scene chunk record directory")
        cursor = 16 + count_in_chunk * entry_stride
        for i in range(count_in_chunk):
            k, version, entity_id, begin, size, crc, ordinal = struct.unpack_from(
                "<HHQIIII", plain, 16 + i * RECORD_ENTRY_SIZE)
            if (k < 1 or version == 0 or begin != cursor or size <= 0 or
                    size > len(plain) - cursor or ordinal >= MAX_RECORDS or
                    ordinal in records):
                raise ValueError("Invalid Scene record type/range/ordinal")
            body = plain[cursor:cursor + size]
            if zlib.crc32(body) != crc:
                raise ValueError("Scene record CRC mismatch")
            if k != 2 and k < 7 and entity_id:
                raise ValueError("Unexpected entity ID on non-entity Scene record")
            records[ordinal] = (k, entity_id, body)
            cursor += size
        if cursor != len(plain):
            raise ValueError("Trailing data in Scene record chunk")
        total_plain += plain_len
        total_records += count_in_chunk
        next_offset += zip_len
        full_plain += plain
    if (pos != len(table) or next_offset != len(data)
            or total_plain != expanded or total_records != record_count
            or len(records) != record_count or
            blake3(full_plain).digest() != data[64:96]):
        raise ValueError("NEF8 Scene chunk totals/integrity mismatch")
    if sorted(records) != list(range(record_count)):
        raise ValueError("Scene record ordinal gap")
    return [records[index] for index in range(record_count)]
