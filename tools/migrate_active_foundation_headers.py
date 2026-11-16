#!/usr/bin/env python3
"""Promote reachable historical NorthStar headers to the maintained public API.

This migrates only the include closure of active sources. Historical files remain
unchanged for provenance; use --verify in CI to detect missing or diverged copies.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULES = ROOT / "engine/northstarcpp/modules"
LEGACY = MODULES / "core/northstarcpp"
API = MODULES / "api/include/northstarcpp"
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.MULTILINE)
EXTS = {".cpp", ".c", ".hpp", ".h", ".hxx", ".inl", ".inc"}
SKIP = {"core/northstarcpp"}
SCAN_ROOTS = [MODULES, ROOT / "tests", ROOT / "tools/shader_generator"]
FORCE_SEEDS = {"core/math_standalone.h"}

def text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")

def active_sources():
    for base in SCAN_ROOTS:
        for path in base.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in EXTS:
                continue
            if base == MODULES and path.is_relative_to(LEGACY):
                continue
            yield path

def legacy_path(include: str, source: Path, in_legacy: bool) -> Path | None:
    if include.startswith("northstarcpp/"):
        candidate = LEGACY / include[len("northstarcpp/"):]
    elif in_legacy:
        candidate = source.parent / include
        if not candidate.is_file():
            candidate = LEGACY / include
    else:
        return None
    candidate = candidate.resolve()
    if candidate.is_file() and candidate.is_relative_to(LEGACY):
        return candidate
    return None

def closure():
    seeds: set[Path] = set()
    for source in active_sources():
        for inc in INCLUDE.findall(text(source)):
            match = legacy_path(inc, source, False)
            if match:
                seeds.add(match)
    for relative in FORCE_SEEDS:
        src = LEGACY / relative
        if src.is_file():
            seeds.add(src.resolve())
    visited: set[Path] = set()
    pending = sorted(seeds)
    while pending:
        source = pending.pop()
        if source in visited:
            continue
        visited.add(source)
        for inc in INCLUDE.findall(text(source)):
            match = legacy_path(inc, source, True)
            if match and match not in visited:
                pending.append(match)
    return sorted(visited), sorted(seeds)

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--copy", action="store_true", help="Copy missing API headers (never overwrite divergent files)")
    parser.add_argument("--verify", action="store_true", help="Require all referenced legacy files to be staged identically")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    found, seeds = closure()
    missing, conflicts, copied = [], [], []
    for src in found:
        rel = src.relative_to(LEGACY)
        target = API / rel
        if target.exists():
            if digest(src) != digest(target):
                conflicts.append(rel.as_posix())
            continue
        if args.copy:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, target)
            copied.append(rel.as_posix())
        else:
            missing.append(rel.as_posix())
    report = {
        "direct_seed_count": len(seeds),
        "reachable_header_count": len(found),
        "copied_count": len(copied),
        "missing_count": len(missing),
        "conflict_count": len(conflicts),
        "copied": copied,
        "missing": missing,
        "conflicts": conflicts,
    }
    print(json.dumps(report, ensure_ascii=False, indent=2) if args.json else
          f"Reachable {len(found)}, copied {len(copied)}, missing {len(missing)}, conflicts {len(conflicts)}")
    return 1 if conflicts or (args.verify and missing) else 0

if __name__ == "__main__":
    raise SystemExit(main())
