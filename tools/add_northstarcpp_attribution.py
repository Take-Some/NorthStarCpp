#!/usr/bin/env python3
"""Add NorthStarCpp project attribution alongside original copyright notices.

Historical copyright notices remain unchanged. Run --check before --apply.
A ZIP containing each original changed file is written before mutations.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "engine" / "northstarcpp" / "modules" / "foundation"
OLD_NOTICE = re.compile(rb"(?i)\bCopyright\s*\(\s*C\s*\)\s*GSC[ \t]+Game[ \t]+World")
SIGNATURE = "2026 Kayla's Systems — Kayla Verner"
MARKER = "NorthStarCpp project: " + SIGNATURE
MARKER_UTF8 = MARKER.encode("utf-8")
MARKER_CP1251 = MARKER.encode("cp1251")
COMMENT_NOTICE = re.compile(rb"^[ \t]*//[ \t]*Copyright[ \t]*\([ \t]*C[ \t]*\)[ \t]*GSC", re.I)


def candidates() -> tuple[list[tuple[Path, bytes, bytes]], list[str]]:
    changes = []
    skipped = []
    for path in SOURCE.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in {
            ".h", ".hpp", ".cpp", ".c", ".cc", ".cxx", ".inl", ".inc"
        }:
            continue
        original = path.read_bytes()
        if not OLD_NOTICE.search(original):
            continue
        if MARKER_UTF8 in original or MARKER_CP1251 in original:
            continue
        if original.startswith((b"\xff\xfe", b"\xfe\xff")):
            skipped.append(path.relative_to(ROOT).as_posix() + ": UTF-16 original")
            continue
        lines = original.splitlines(keepends=True)
        position = next((i for i, line in enumerate(lines)
                         if i < 20 and COMMENT_NOTICE.search(line)), None)
        if position is None:
            skipped.append(path.relative_to(ROOT).as_posix() + ": no leading // copyright")
            continue
        legacy_bytes = any(b >= 128 for b in original)
        try:
            original.decode("utf-8-sig")
            encoding = "utf-8"
        except UnicodeDecodeError:
            encoding = "cp1251" if legacy_bytes else "utf-8"
        line = lines[position]
        ending = b"\r\n" if line.endswith(b"\r\n") else b"\n"
        prefix = b"// "
        addition = prefix + MARKER.encode(encoding) + ending
        rewritten = b"".join(lines[:position + 1]) + addition + b"".join(lines[position + 1:])
        changes.append((path, original, rewritten))
    return changes, skipped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    if not SOURCE.is_dir():
        parser.error("NorthStarCpp foundation source tree is missing")
    changes, skipped = candidates()
    archive_name = None
    if args.apply and changes:
        backups = ROOT / ".northstarcpp" / "backups"
        backups.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        archive = backups / ("original-attribution-" + stamp + ".zip")
        with ZipFile(archive, "x", ZIP_DEFLATED, compresslevel=6) as zf:
            for path, before, _ in changes:
                zf.writestr(path.relative_to(ROOT).as_posix(), before)
            zf.writestr("NOTES.txt", "Original source files before adding project-maintainer attribution. Copyright notices must be retained.\n")
        archive_name = archive.relative_to(ROOT).as_posix()
        for path, before, after in changes:
            # Only these narrowly qualified comment-line additions are written.
            with tempfile.NamedTemporaryFile("wb", dir=path.parent, delete=False,
                                             prefix=".northstarcpp-attribution-") as tmp:
                tmp.write(after)
                temp_name = tmp.name
            try:
                os.replace(temp_name, path)
            finally:
                if os.path.exists(temp_name):
                    os.unlink(temp_name)
    print(json.dumps({
        "mode": "apply" if args.apply else "check",
        "signature": SIGNATURE,
        "files_" + ("updated" if args.apply else "pending"): len(changes),
        "skipped": skipped[:25],
        "skipped_count": len(skipped),
        "archive": archive_name,
        "original_copyright_notices": "unchanged",
    }, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
