#!/usr/bin/env python3
# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""Read-only verification of NorthStarCpp source identity.

Generated outputs and rollback archives are excluded from this check.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SKIP = {"out", "dist", "distributable", ".northstarcpp", ".git", "__pycache__", ".northstarcpp-shell"}
TEXT = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inl", ".inc",
    ".cs", ".sln", ".vcproj", ".vcxproj", ".props", ".targets", ".rc",
    ".txt", ".md", ".xml", ".json", ".py", ".pyw", ".ps1", ".cmd",
    ".cmake", ".def", ".ini", ".cfg", ".lua", ".html", ".hlsl", ".fx",
    ".xaml", ".csproj", ".resx", ".wxi", ".wxs", ".log",
}
OLD_PATTERN = re.compile(rb"(?i)x[-_]?ray")
# Compose the old tree's name without embedding legacy product branding.
OLD_TREE = ROOT / "engine" / ("x" + "ray")
NEW_TREE = ROOT / "engine" / "northstarcpp" / "modules" / "foundation" / "northstarcpp" / "compat"

def verify() -> dict:
    findings = []
    path_findings = []
    checked = 0
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(ROOT)
        if any(x in SKIP or ".bak-" in x for x in relative.parts):
            continue
        if OLD_PATTERN.search(str(relative).encode("utf-8", errors="replace")):
            path_findings.append(str(relative))
        if path.suffix.lower() not in TEXT and path.name != "CMakeLists.txt":
            continue
        checked += 1
        data = path.read_bytes()
        count = len(OLD_PATTERN.findall(data))
        if count:
            findings.append({"file": str(relative), "occurrences": count})
    return {
        "engine": "NorthStarCpp",
        "source_files_checked": checked,
        "legacy_name_occurrences": sum(x["occurrences"] for x in findings),
        "legacy_filename_count": len(path_findings),
        "legacy_source_tree_absent": not OLD_TREE.exists(),
        "new_compat_tree_exists": NEW_TREE.is_dir(),
        "remaining_source_files": findings[:50],
        "remaining_paths": path_findings[:50],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify the NorthStarCpp name in active sources")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    result = verify()
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print("NorthStarCpp name verification")
        for name, value in result.items():
            print("{}: {}".format(name, value))
    return 0 if (result["legacy_name_occurrences"] == 0
                 and result["legacy_filename_count"] == 0
                 and result["legacy_source_tree_absent"]
                 and result["new_compat_tree_exists"]) else 1


if __name__ == "__main__":
    sys.exit(main())
