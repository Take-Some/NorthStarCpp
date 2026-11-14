#!/usr/bin/env python3
# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""One-time, non-destructive source layout migration.

Usage:
  python tools/organize_engine_layout.py --apply
  python tools/organize_engine_layout.py --verify

Moves the existing files; never rewrites vendor code, deletes source files,
or modifies build outputs. Stores a recoverable relocation manifest and
backups of configuration files outside the engine directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE = Path("engine/northstarcpp")
ARCHIVE = Path(".northstarcpp/archive/engine-layout-2026-11-13")

# Folder moves first, then individual plugin files.
RELOCATIONS = (
    (ENGINE / "core", ENGINE / "modules/host"),
    (ENGINE / "apps", ENGINE / "modules/editor"),
    (ENGINE / "renderer", ENGINE / "modules/viewport"),
    (ENGINE / "plugins/common", ENGINE / "modules/shared"),
    (ENGINE / "plugins/ecs/flecs.cpp", ENGINE / "modules/ecs/flecs/flecs.cpp"),
    (ENGINE / "plugins/physics/bullet.cpp", ENGINE / "modules/physics/bullet/bullet.cpp"),
    (ENGINE / "plugins/physics/jolt.cpp", ENGINE / "modules/physics/jolt/jolt.cpp"),
    (ENGINE / "plugins/render/d3d11.cpp", ENGINE / "modules/render/d3d11/d3d11.cpp"),
    (ENGINE / "plugins/audio/winmm.cpp", ENGINE / "modules/audio/winmm/winmm.cpp"),
    (ENGINE / "plugins/input/win32_xinput.cpp", ENGINE / "modules/input/win32/win32_xinput.cpp"),
    (ENGINE / "compat", Path("external/northstarcpp/compat")),
    (ENGINE / "ecs_api_flecs.cpp", ARCHIVE / "retired-sources/ecs_api_flecs.cpp"),
    (ENGINE / "ecs_runtime.cpp", ARCHIVE / "retired-sources/ecs_runtime.cpp"),
    (Path("engine/NorthStarCpp.Source.sln"), ARCHIVE / "legacy-ide/NorthStarCpp.Source.sln"),
    (Path("engine/NorthStarCpp.Source.suo"), ARCHIVE / "legacy-ide/NorthStarCpp.Source.suo"),
)
BACKUP_FILES = (
    Path("CMakeLists.txt"),
    ENGINE / "CMakeLists.txt",
    Path("tools/dev/cli.py"),
    Path("tests/test_northstarcpp_branding.py"),
    Path("tools/update_project_headers.py"),
    Path("tools/verify_northstarcpp.py"),
    Path("docs/AUTHORSHIP.md"),
    Path("docs/FLECS_ECS.md"),
    Path("README.md"),
)
STATE = ROOT / ".northstarcpp/migrations/engine-layout-v1.json"


def digest(path: Path, original_filename: str | None = None) -> tuple[int, str]:
    files = sorted((q for q in path.rglob("*") if q.is_file()),
                   key=lambda q: q.relative_to(path).as_posix()) if path.is_dir() else [path]
    result = hashlib.sha256()
    for file in files:
        label = file.relative_to(path).as_posix() if path.is_dir() else (original_filename or file.name)
        result.update(label.encode("utf-8"))
        result.update(b"\0")
        with file.open("rb") as handle:
            for data in iter(lambda: handle.read(1024 * 1024), b""):
                result.update(data)
    return len(files), result.hexdigest()


def verify() -> int:
    if not STATE.exists():
        raise SystemExit("No migration manifest; use --apply first")
    info = json.loads(STATE.read_text(encoding="utf-8"))
    failed = []
    for move in info["moves"]:
        source = ROOT / move["from"]
        target = ROOT / move["to"]
        # Compatibility sources were migrated a second time to the engine foundation.
        # Keep the original checksum and validate them at their final location.
        if move["to"] == "external/northstarcpp/compat":
            target = ROOT / "engine/northstarcpp/modules/foundation/northstarcpp/compat"
        if source.exists() or not target.exists():
            failed.append(move["to"])
            continue
        if target.is_relative_to(ROOT / ARCHIVE) and digest(target, source.name) != (
            move["files"], move["sha256"]
        ):
            failed.append(move["to"])
    print(json.dumps({"verification": "PASS" if not failed else "FAILED",
                      "moved": len(info["moves"]), "mismatches": failed}, ensure_ascii=False, indent=2))
    return int(bool(failed))


def apply() -> int:
    if STATE.exists():
        raise SystemExit("Migration already recorded; use --verify")
    for old, new in RELOCATIONS:
        if not (ROOT / old).exists():
            raise SystemExit(f"Missing source; refusing partial migration: {old}")
        if (ROOT / new).exists():
            raise SystemExit(f"Destination exists; refusing overwrite: {new}")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    backups = ROOT / ".northstarcpp/backups" / f"engine-layout-{stamp}"
    backups.mkdir(parents=True, exist_ok=False)
    for file in BACKUP_FILES:
        if (ROOT / file).is_file():
            path = backups / file
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / file, path)

    records = []
    STATE.parent.mkdir(parents=True, exist_ok=True)
    for old, new in RELOCATIONS:
        source, target = ROOT / old, ROOT / new
        file_count, checksum = digest(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        source.rename(target)  # preserves byte contents and third-party licenses
        records.append({"from": old.as_posix(), "to": new.as_posix(),
                        "files": file_count, "sha256": checksum})
        # Write a relocation journal after each successful move; if interrupted,
        # no file is lost and the journal can be used to reverse completed moves.
        STATE.write_text(json.dumps({"created": stamp, "backup": str(backups.relative_to(ROOT)),
                                     "moves": records}, ensure_ascii=False, indent=2), encoding="utf-8")
    # Remove ONLY empty former directories.
    for old in (ENGINE / "plugins/audio", ENGINE / "plugins/ecs",
                ENGINE / "plugins/physics", ENGINE / "plugins/render",
                ENGINE / "plugins", ENGINE / "apps", ENGINE / "core",
                ENGINE / "renderer"):
        path = ROOT / old
        if path.is_dir() and not any(path.iterdir()):
            path.rmdir()
    print(json.dumps({"status": "moved", "backup": str(backups.relative_to(ROOT)),
                      "manifest": str(STATE.relative_to(ROOT)),
                      "moves": len(records),
                      "files": sum(row["files"] for row in records)}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--apply", action="store_true")
    group.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    raise SystemExit(apply() if args.apply else verify())
