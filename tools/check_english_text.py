#!/usr/bin/env python3
"""Check maintained NorthStarCpp source and documentation for Cyrillic text.

Third-party vendor sources, generated packages, archives, and user projects are
not translated automatically: their data, licensing, and encodings are independent
of the English-language NorthStarCpp interface.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {
    ".git", ".northstarcpp", "__pycache__", "out", "dist", "distributable",
    "external", "Projects",
}
EXTENSIONS = {
    ".md", ".txt", ".py", ".pyw", ".ps1", ".cmd", ".bat", ".json",
    ".yaml", ".yml", ".toml", ".ini", ".cfg", ".cpp", ".c", ".cc",
    ".cxx", ".h", ".hpp", ".inl", ".cs", ".xaml", ".xml", ".rc",
    ".cmake", ".lua", ".js", ".ts", ".tsx", ".jsx", ".html", ".css",
    ".sh", ".nscene",
}
CYRILLIC = re.compile(r"[\u0400-\u052f]")


def read_source(file: Path) -> str | None:
    content = file.read_bytes()
    if content.startswith((b"\xff\xfe", b"\xfe\xff")):
        return content.decode("utf-16", errors="replace")
    for codec in ("utf-8-sig", "cp1251"):
        try:
            return content.decode(codec)
        except UnicodeDecodeError:
            continue
    return None


def check() -> dict:
    scanned = 0
    violations = []
    for base, folders, files in os.walk(ROOT):
        folders[:] = [folder for folder in folders if folder not in SKIP_DIRS]
        for name in files:
            file = Path(base) / name
            if (file.suffix.lower() not in EXTENSIONS and name != "CMakeLists.txt"
                    or file.is_symlink() or file.stat().st_size > 4_000_000):
                continue
            text = read_source(file)
            if text is None:
                continue
            scanned += 1
            for number, line in enumerate(text.splitlines(), 1):
                if CYRILLIC.search(line):
                    violations.append({
                        "file": file.relative_to(ROOT).as_posix(),
                        "line": number,
                        "preview": line.strip()[:160],
                    })
    return {
        "engine": "NorthStarCpp",
        "language": "en",
        "files_scanned": scanned,
        "remaining_cyrillic_lines": len(violations),
        "violations": violations,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    result = check()
    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=True))
    else:
        print("NorthStarCpp English localization audit")
        print("Files scanned:", result["files_scanned"])
        print("Remaining Cyrillic lines:", result["remaining_cyrillic_lines"])
        for issue in result["violations"][:25]:
            print("{}:{}: {}".format(issue["file"], issue["line"], issue["preview"]))
    return 1 if result["violations"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
