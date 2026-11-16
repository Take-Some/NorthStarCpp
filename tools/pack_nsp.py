#!/usr/bin/env python3
"""Deterministic NorthStar Pack (.nsp v1) builder and verifier."""
from __future__ import annotations
import argparse
import os
import subprocess
from xml.etree import ElementTree as ET
from pathlib import Path
import struct
import tempfile
import zlib

MAGIC = b"NSPK\r\n\x1a\n"
HEADER = struct.Struct("<8sIIQQII")
RECORD = struct.Struct("<HHQQII")
MAX_FILE = 256 * 1024 * 1024
MAX_INDEX = 64 * 1024 * 1024
MAX_FILES = 1_000_000
EXCLUDE_NAMES = {".git", "__pycache__", ".DS_Store"}
ALIGN = 16


def safe_path(text: str) -> None:
    raw = text.encode("utf-8", "strict")
    if not raw or len(raw) > 4096 or "\\" in text or "\x00" in text:
        raise ValueError(f"Invalid virtual asset path: {text!r}")
    for segment in text.split("/"):
        if not segment or segment.endswith((".", " ")) or len(segment.encode()) > 4096:
            raise ValueError(f"Invalid virtual asset component: {text!r}")
        if any(c in segment for c in ':<>"|?*') or any(ord(c) < 32 for c in segment):
            raise ValueError(f"Invalid virtual asset component: {text!r}")


def inventory(source: Path) -> list[tuple[str, Path, int, int]]:
    if not source.is_dir():
        raise FileNotFoundError(f"Shared assets source is missing: {source}")
    files = []
    for path in source.rglob("*"):
        relative = path.relative_to(source)
        if any(part in EXCLUDE_NAMES for part in relative.parts):
            continue
        if path.is_symlink():
            raise ValueError(f"Symlinks are not supported in NSP: {path}")
        if not path.is_file() or ".bak" in path.name.lower() or path.name.endswith(("~", ".tmp")):
            continue
        name = relative.as_posix()
        safe_path(name)
        size = path.stat().st_size
        if size > MAX_FILE:
            raise ValueError(f"Asset exceeds 256 MiB: {name}")
        crc = 0
        with path.open("rb") as handle:
            for part in iter(lambda: handle.read(1024 * 1024), b""):
                crc = zlib.crc32(part, crc)
        files.append((name, path, size, crc & 0xFFFFFFFF))
    files.sort(key=lambda x: x[0].encode("utf-8"))
    if len(files) > MAX_FILES:
        raise ValueError("Too many assets")
    if len({x[0].casefold() for x in files}) != len(files):
        raise ValueError("Case-insensitive virtual asset path collision")
    return files


def layout(files: list[tuple[str, Path, int, int]]) -> tuple[bytes, list[int]]:
    index_bytes = sum(RECORD.size + len(name.encode("utf-8")) for name, _, _, _ in files)
    if index_bytes > MAX_INDEX:
        raise ValueError("NSP index exceeds 64 MiB")
    base = HEADER.size + index_bytes
    offsets = []
    for _, _, size, _ in files:
        base = (base + ALIGN - 1) & ~(ALIGN - 1)
        offsets.append(base)
        base += size
    index = bytearray()
    for (name, _, size, crc), offset in zip(files, offsets):
        encoded = name.encode("utf-8")
        if len(encoded) > 65535:
            raise ValueError(f"Asset name too long: {name}")
        index += RECORD.pack(len(encoded), 0, offset, size, crc, 0)
        index += encoded
    return bytes(index), offsets


def build_qt_icon_resource(source: Path, directory: Path, rcc: Path) -> Path:
    """Create external Qt resource data, preserving existing :/ URLs.

    The generated .rcc is packed INSIDE NSP, not embedded in any executable.
    """
    icon_root = source / "icons"
    document = ET.Element("RCC", version="1.0")
    groups = (
        ("assetBrowser", "/northstar/icons", "assetBrowser/"),
        ("playIcons", "/northstar/play", ""),
    )
    for folder, prefix, alias_prefix in groups:
        images = sorted((icon_root / folder).glob("*.png"))
        if not images:
            raise ValueError(f"Required Qt icon group missing: {folder}")
        group = ET.SubElement(document, "qresource", prefix=prefix)
        for image in images:
            record = ET.SubElement(group, "file",
                                   alias=alias_prefix + image.name)
            record.text = image.resolve().as_posix()
    qrc = directory / "northstar_ui_icons.qrc"
    bundle = directory / "northstar_ui_icons.rcc"
    ET.ElementTree(document).write(qrc, encoding="utf-8", xml_declaration=True)
    result = subprocess.run(
        [str(rcc), "--binary", "-o", str(bundle), str(qrc)],
        capture_output=True, text=True, errors="replace", timeout=60)
    if result.returncode or not bundle.is_file() or bundle.stat().st_size == 0:
        raise RuntimeError("Qt RCC compilation failed: " +
                           (result.stderr or result.stdout)[-2000:])
    return bundle


def pack(source: Path, output: Path, *, qt_rcc: Path | None = None) -> tuple[int, int]:
    files = inventory(source)
    with tempfile.TemporaryDirectory(prefix="northstar-ui-rcc-") as ui_temp:
        if qt_rcc is not None:
            generated = build_qt_icon_resource(source, Path(ui_temp), qt_rcc)
            extra = "ui/northstar_ui_icons.rcc"
            size = generated.stat().st_size
            with generated.open("rb") as src:
                checksum = 0
                for chunk in iter(lambda: src.read(1024 * 1024), b""):
                    checksum = zlib.crc32(chunk, checksum)
            files.append((extra, generated, size, checksum & 0xFFFFFFFF))
            files.sort(key=lambda item: item[0].encode("utf-8"))
            if len({item[0].casefold() for item in files}) != len(files):
                raise ValueError("Generated Qt resource collides with source asset")
        return _write_pack(files, output)


def _write_pack(files: list[tuple[str, Path, int, int]],
                output: Path) -> tuple[int, int]:
    index, offsets = layout(files)
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=output.name + ".", suffix=".tmp", dir=output.parent)
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(HEADER.pack(MAGIC, 1, 0, len(index), HEADER.size + len(index), len(files), 0))
            out.write(index)
            for (_, path, size, _), offset in zip(files, offsets):
                out.write(b"\0" * (offset - out.tell()))
                written = 0
                with path.open("rb") as src:
                    for chunk in iter(lambda: src.read(1024 * 1024), b""):
                        out.write(chunk)
                        written += len(chunk)
                if written != size:
                    raise ValueError(f"Asset changed during packing: {path}")
            out.flush()
            os.fsync(out.fileno())
        verify(Path(temp_name))
        os.replace(temp_name, output)
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)
    return len(files), output.stat().st_size


def verify(path: Path) -> tuple[int, int]:
    with path.open("rb") as inp:
        header = inp.read(HEADER.size)
        if len(header) != HEADER.size:
            raise ValueError("Truncated NSP header")
        magic, version, flags, length, payload, count, reserved = HEADER.unpack(header)
        total = path.stat().st_size
        if (magic != MAGIC or version != 1 or flags or reserved or
                length > MAX_INDEX or count > MAX_FILES or
                payload < HEADER.size + length or payload > total or
                count * RECORD.size > length):
            raise ValueError("Invalid NSP header")
        index = inp.read(length)
        if len(index) != length:
            raise ValueError("Truncated NSP index")
        cursor = 0
        last = b""
        end = payload
        for i in range(count):
            if cursor + RECORD.size > length:
                raise ValueError("Truncated index entry")
            name_length, method, offset, size, crc, flags = RECORD.unpack_from(index, cursor)
            cursor += RECORD.size
            raw = index[cursor:cursor+name_length]
            if not name_length or len(raw) != name_length or method or flags:
                raise ValueError("Invalid index record")
            name = raw.decode("utf-8")
            safe_path(name)
            cursor += name_length
            if (i and raw <= last) or offset < end or size > MAX_FILE or size > total - offset:
                raise ValueError("Invalid index order or payload bounds")
            last = raw
            end = offset + size
            inp.seek(offset)
            checksum = 0
            remaining = size
            while remaining:
                chunk = inp.read(min(remaining, 1024 * 1024))
                if not chunk:
                    raise ValueError("Truncated NSP payload")
                checksum = zlib.crc32(chunk, checksum)
                remaining -= len(chunk)
            if (checksum & 0xFFFFFFFF) != crc:
                raise ValueError(f"Checksum mismatch: {name}")
        if cursor != length:
            raise ValueError("Trailing index bytes")
        return count, total


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--verify", type=Path)
    ap.add_argument("--qt-rcc", type=Path, help="Qt resource compiler for UI icons")
    args = ap.parse_args()
    if args.verify:
        count, size = verify(args.verify)
        print(f"Verified NSP v1: {count} assets, {size} bytes")
    else:
        if not args.root or not args.out:
            ap.error("--root and --out are required for packing")
        count, size = pack(args.root, args.out, qt_rcc=args.qt_rcc)
        print(f"Packed NSP v1: {count} assets, {size} bytes -> {args.out}")


if __name__ == "__main__":
    main()
