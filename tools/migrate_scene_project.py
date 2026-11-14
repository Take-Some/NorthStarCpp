#!/usr/bin/env python3
"""Migrate a NorthStarCpp project's legacy startup .nscene to NEF8 ListFile.

Does not modify or delete the original scene. Converts through the native
engine parser, verifies binary result, backs up the project manifest, then
atomically changes startup_scene to the new filename.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zlib
import struct
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.dev.scene_binary import validate_nef8_scene


def migrate(project: Path, converter: Path, dry_run: bool = False) -> Path:
    project = project.resolve(strict=True)
    manifest = project / "northstarcpp.project.json"
    if not manifest.is_file() or manifest.is_symlink():
        raise RuntimeError("Missing or redirected project manifest")
    details = json.loads(manifest.read_text(encoding="utf-8"))
    if details.get("engine") != "NorthStarCpp":
        raise RuntimeError("Unsupported project manifest")
    name = details.get("startup_scene", "main.nscene")
    if (not isinstance(name, str) or not name.endswith(".nscene") or
        "\\" in name or ":" in name or name.startswith("/") or
        any(part in ("", ".", "..") for part in name.split("/"))):
        raise RuntimeError("Project startup is not a valid legacy scene path")
    source = project.joinpath(*name.split("/"))
    target = source.with_suffix(".nscn")
    if source.resolve(strict=True).is_relative_to(project) is False:
        raise RuntimeError("Legacy scene is not inside the project")
    if source.is_symlink() or target.exists():
        raise RuntimeError("Unsafe legacy scene or binary output already exists")
    exe = converter.resolve(strict=True)
    if not exe.is_file():
        raise RuntimeError("Native scene converter executable not found")
    new_name = target.relative_to(project).as_posix()
    if dry_run:
        return target
    result = subprocess.run([str(exe), str(source), str(target)],
                            capture_output=True, text=True, timeout=90)
    if result.returncode != 0:
        raise RuntimeError("Native scene converter failed: " + result.stderr[-2000:])
    try:
        binary = target.read_bytes()
        if binary[:4] == b"NEF8":
            validate_nef8_scene(binary)
        elif binary[:4] == b"NSCN":
            # Explicit older converter: retain verification and compatibility.
            if len(binary) < 16:
                raise RuntimeError("Invalid NSCN legacy converter output")
            major, minor, size, checksum = struct.unpack_from("<HHII", binary, 4)
            if ((major, minor) != (1, 0) or size != len(binary)-16 or
                    zlib.crc32(binary[16:]) != checksum):
                raise RuntimeError("Legacy NSCN conversion output failed verification")
        else:
            raise RuntimeError("Scene converter returned unsupported format")
        backup = manifest.with_name(manifest.name + ".before-nscn")
        if backup.exists():
            raise RuntimeError("Manifest backup already exists; manual review required")
        shutil.copy2(manifest, backup)
        details["startup_scene"] = new_name
        content = (json.dumps(details, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        temp: str | None = None
        try:
            with tempfile.NamedTemporaryFile(dir=manifest.parent, prefix=".nscn-manifest-",
                                             suffix=".tmp", delete=False) as handle:
                temp = handle.name
                handle.write(content)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temp, manifest)
        finally:
            if temp:
                Path(temp).unlink(missing_ok=True)
    except Exception:
        target.unlink(missing_ok=True)
        raise
    return target


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--converter", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        result = migrate(args.project, args.converter, args.dry_run)
        print(("Would create: " if args.dry_run else "Migrated startup scene: ") + str(result))
    except Exception as error:
        parser.exit(1, "Scene migration failed: " + str(error) + "\n")
