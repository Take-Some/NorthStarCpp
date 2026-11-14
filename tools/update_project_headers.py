#!/usr/bin/env python3
# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""Maintain NorthStarCpp-owned file headers without altering upstream attribution.

Run from anywhere:
    python tools/update_project_headers.py --apply
    python tools/update_project_headers.py --check

Excluded: SDK, external, generated outputs,
backups, upstream license notices, and third-party source files.
"""
from __future__ import annotations

import argparse
import datetime
import json
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AUTHOR = "Kayla Verner"
YEAR = datetime.date.today().year
OWNED_GLOBS = (
    "engine/northstarcpp/modules/**/*.cpp",
    "engine/northstarcpp/modules/**/*.hpp",
    "engine/northstarcpp/modules/**/*.h",
    "include/northstarcpp/*.hpp",
    "include/northstarcpp/*.h",
    "tests/*.cpp",
    "tests/*.py",
    "tools/dev/*.py",
    "tools/*.py",
)
OWNED_EXTENSIONS = {".cpp", ".hpp", ".h", ".py", ".pyw"}
THIRD_PARTY_ROOTS = ("external",)
PATTERN_LEGAL = re.compile(r"(?i)(copyright|spdx-license-identifier|all rights reserved|gsc game world)")


def eligible_files() -> list[Path]:
    return sorted({
        p for glob in OWNED_GLOBS for p in ROOT.glob(glob)
        if p.is_file() and p.suffix.lower() in OWNED_EXTENSIONS
    })


def header(path: Path) -> str:
    prefix = "#" if path.suffix.lower() in (".py", ".pyw") else "//"
    return (
        f"{prefix} NorthStarCpp\n"
        f"{prefix} Author: {AUTHOR}\n"
        f"{prefix} Updated: {YEAR}\n"
    )


def planned_update(path: Path) -> tuple[str, str] | None:
    original = path.read_text(encoding="utf-8-sig")
    if PATTERN_LEGAL.search(original[:3000]):
        return None
    expected = header(path)
    # Re-running updates the project year rather than duplicating the header.
    project_header = re.compile(
        r"(?://|#) NorthStarCpp\r?\n"
        r"(?://|#) Author: Kayla Verner\r?\n"
        r"(?://|#) Updated: \d{4}\r?\n"
    )
    found = project_header.search(original[:300])
    if found is not None:
        result = original[:found.start()] + expected + original[found.end():]
        return (original, result) if result != original else None
    if original.startswith("#!"):
        first, _, rest = original.partition("\n")
        result = first + "\n" + expected + rest
    else:
        result = expected + "\n" + original
    return original, result


def audit_third_party() -> dict[str, int]:
    counts = {}
    for directory in THIRD_PARTY_ROOTS:
        root = ROOT / directory
        count = 0
        legal_headers = 0
        for p in root.rglob("*"):
            if not p.is_file() or p.suffix.lower() not in OWNED_EXTENSIONS:
                continue
            count += 1
            try:
                with p.open("rb") as f:
                    prefix = f.read(2500).lower()
                if b"copyright" in prefix or b"gsc game world" in prefix:
                    legal_headers += 1
            except OSError:
                pass
        counts[directory] = count
        counts[directory + ":legal_headers"] = legal_headers
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--apply", action="store_true")
    group.add_argument("--check", action="store_true")
    options = parser.parse_args()
    changes: list[tuple[Path, str, str]] = []
    skipped_legal: list[str] = []
    for path in eligible_files():
        text = path.read_text(encoding="utf-8-sig")
        if PATTERN_LEGAL.search(text[:3000]):
            skipped_legal.append(path.relative_to(ROOT).as_posix())
            continue
        planned = planned_update(path)
        if planned is not None:
            changes.append((path, *planned))
    backup = None
    if options.apply and changes:
        backup_dir = ROOT / ".northstarcpp" / "backups"
        backup_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
        backup = backup_dir / f"project-headers-{stamp}.zip"
        with zipfile.ZipFile(backup, "x", compression=zipfile.ZIP_DEFLATED) as archive:
            for path, previous, _ in changes:
                archive.writestr(path.relative_to(ROOT).as_posix(), previous.encode("utf-8"))
        for path, _, new_text in changes:
            path.write_text(new_text, encoding="utf-8", newline="\n")
    result = {
        "mode": "apply" if options.apply else "check",
        "year": YEAR,
        "author": AUTHOR,
        "owned_files": len(eligible_files()),
        "files_" + ("modified" if options.apply else "requiring_update"): [
            p.relative_to(ROOT).as_posix() for p, _, _ in changes
        ],
        "skipped_existing_legal_notice": skipped_legal,
        "backup": str(backup) if backup else None,
        "third_party_audit": audit_third_party(),
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if options.check and changes else 0


if __name__ == "__main__":
    sys.exit(main())
