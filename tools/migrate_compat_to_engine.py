#!/usr/bin/env python3
# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""Relocate preserved historical engine sources into the owned engine tree.

This operation moves files without changing their contents or authorship.
Relocated source is NOT automatically compiled or feature-complete.
The manifest records a verified, immutable source inventory.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OLD = ROOT / "external/northstarcpp/compat"
NEW = ROOT / "engine/northstarcpp/modules/foundation/northstarcpp/compat"
MANIFEST = ROOT / ".northstarcpp/migrations/compat-foundation-v1.json"

def inventory(folder: Path) -> dict[str, dict]:
    data = {}
    for p in sorted(folder.rglob("*")):
        if p.is_file():
            relative = p.relative_to(folder).as_posix()
            data[relative] = {"bytes": p.stat().st_size,
                              "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
    return data

def run(verify: bool) -> int:
    if verify:
        if OLD.exists():
            raise RuntimeError("Legacy source directory reappeared outside the engine")
        if not MANIFEST.is_file() or not NEW.is_dir():
            raise RuntimeError("Migration record or engine foundation missing")
        saved = json.loads(MANIFEST.read_text(encoding="utf-8"))
        current = inventory(NEW)
        mismatches = [name for name, data in saved["files"].items()
                      if current.get(name) != data]
        if len(current) != len(saved["files"]):
            mismatches.append("file count differs")
        if mismatches:
            raise RuntimeError("Source migration verification failed: " + repr(mismatches[:10]))
        print(json.dumps({"verification": "PASS", "files": len(current),
                          "bytes": sum(row["bytes"] for row in current.values())}))
        return 0
    if MANIFEST.exists():
        raise RuntimeError("Migration already recorded. Use --verify")
    if not OLD.is_dir() or NEW.exists():
        raise RuntimeError("Expected existing legacy directory and absent destination")
    data = inventory(OLD)
    if not data:
        raise RuntimeError("Legacy tree is empty")
    NEW.parent.mkdir(parents=True, exist_ok=True)
    OLD.rename(NEW)  # same volume; no touching vendor contents/metadata
    if inventory(NEW) != data:
        NEW.rename(OLD)
        raise RuntimeError("Moved tree differs from source inventory; rollback performed")
    payload = {
        "schema_version": 1,
        "source": OLD.relative_to(ROOT).as_posix(),
        "destination": NEW.relative_to(ROOT).as_posix(),
        "state": "relocated_sources_not_ported",
        "note": "Copyright notices remain with the original sources. Shipping derived binaries requires rights review.",
        "files": data,
    }
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": "relocated", "files": len(data),
                      "bytes": sum(row["bytes"] for row in data.values()),
                      "manifest": str(MANIFEST.relative_to(ROOT))}))
    return 0

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--apply", action="store_true")
    group.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    try:
        raise SystemExit(run(args.verify))
    except (RuntimeError, OSError, ValueError) as exc:
        parser.exit(2, f"compat migration failed: {exc}\n")
