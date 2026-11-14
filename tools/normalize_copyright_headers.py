#!/usr/bin/env python3
"""Normalize blank lines inside NorthStarCpp source copyright headers only.

The source text, original author/copyright lines, encoding and CRLF/LF style
are preserved byte-for-byte apart from removed blank comment-header rows.
Usage:
    python tools/normalize_copyright_headers.py --check
    python tools/normalize_copyright_headers.py --apply
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {"out", "dist", "distributable", ".northstarcpp", ".git",
            "__pycache__"}
SOURCE_EXTENSIONS = {".h", ".hpp", ".cpp", ".c", ".cc", ".cxx",
                     ".inl", ".inc", ".cs", ".py", ".rc", ".txt"}
BORDER = re.compile(rb"^[ \t]*//{24,}[ \t]*(?:\r?\n|$)")
FIELD = re.compile(
    rb"(?i)(?:Created[ \t]*:|Author[ \t]*:|Copyright|NorthStarCpp project:|Updated[ \t]*:)"
)
EMPTY = re.compile(rb"^[ \t]*(?://[ \t]*)?(?:\r?\n|$)")


def normalized(data: bytes) -> tuple[bytes, int]:
    lines = data.splitlines(keepends=True)
    start = next((i for i, line in enumerate(lines[:8]) if BORDER.fullmatch(line)), None)
    if start is None:
        return data, 0
    end = next((i for i in range(start + 1, min(len(lines), start + 30))
                if BORDER.fullmatch(lines[i])), None)
    if end is None or not any(FIELD.search(line) for line in lines[start + 1:end]):
        return data, 0
    to_remove = [i for i in range(start + 1, end) if EMPTY.fullmatch(lines[i])]
    if not to_remove:
        return data, 0
    skipped = set(to_remove)
    result = b"".join(line for i, line in enumerate(lines) if i not in skipped)
    return result, len(to_remove)


def candidates() -> list[tuple[Path, bytes, bytes, int]]:
    items = []
    for base, folders, files in os.walk(ROOT):
        folders[:] = [name for name in folders if name not in EXCLUDED]
        for name in files:
            file = Path(base) / name
            if file.suffix.lower() not in SOURCE_EXTENSIONS:
                continue
            if file.stat().st_size > 4_000_000 or file.is_symlink():
                continue
            before = file.read_bytes()
            after, count = normalized(before)
            if count:
                items.append((file, before, after, count))
    return sorted(items, key=lambda item: str(item[0]).lower())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument("--check", action="store_true", help="Preview changes")
    actions.add_argument("--apply", action="store_true", help="Back up and clean headers")
    args = parser.parse_args()

    changes = candidates()
    archive = None
    if args.apply and changes:
        backup_dir = ROOT / ".northstarcpp" / "backups"
        backup_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        zip_file = backup_dir / ("copyright-whitespace-" + stamp + ".zip")
        with ZipFile(zip_file, "x", ZIP_DEFLATED, compresslevel=6) as backup:
            for path, before, _after, _count in changes:
                backup.writestr(path.relative_to(ROOT).as_posix(), before)
        archive = str(zip_file.relative_to(ROOT))
        for path, _before, after, _count in changes:
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(
                    "wb", dir=path.parent, prefix=".northstarcpp-header-",
                    suffix=".tmp", delete=False
                ) as handle:
                    handle.write(after)
                    temporary = Path(handle.name)
                temporary.chmod(path.stat().st_mode)
                os.replace(temporary, path)
            finally:
                if temporary is not None and temporary.exists():
                    temporary.unlink()

    print(json.dumps({
        "mode": "apply" if args.apply else "check",
        "files_" + ("updated" if args.apply else "pending"): len(changes),
        "blank_lines_" + ("removed" if args.apply else "pending"):
            sum(count for _, _, _, count in changes),
        "copyright_author_and_project_text": "unchanged",
        "backup": archive
    }, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
