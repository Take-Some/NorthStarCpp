#!/usr/bin/env python3
"""Validate NEF8 Scene plugin component payload lengths; optionally repair.

NEF8 envelope validity alone does not validate the embedded plugin record
length. --repair rewrites only a mismatched u32 payload length, keeps every
record and its bytes intact, and saves a backup beside the scene.
"""
from __future__ import annotations
import argparse
import pathlib
import shutil
import struct
from tools.dev.scene_binary import (pack_listfile, read_nef8_scene_records,
                                    validate_nef8_scene)


def mismatches(records: list[tuple[int, int, bytes]]) -> list[tuple[int, str, int, int]]:
    problems = []
    for kind, entity, body in records:
        if kind != 7:
            continue
        if len(body) < 10:
            raise ValueError(f"Truncated Scene component record: {entity}")
        name_size = struct.unpack_from("<H", body, 0)[0]
        length_at = 2 + name_size + 4
        if name_size > 128 or length_at + 4 > len(body):
            raise ValueError(f"Invalid component name/version: {entity}")
        name = body[2:2 + name_size].decode("utf-8")
        declared = struct.unpack_from("<I", body, length_at)[0]
        actual = len(body) - (length_at + 4)
        if declared != actual:
            problems.append((entity, name, declared, actual))
    return problems


def run(path: pathlib.Path, repair: bool) -> None:
    data = path.read_bytes()
    records = read_nef8_scene_records(data)
    errors = mismatches(records)
    for entity, name, declared, actual in errors:
        print(f"[COMPONENT LENGTH] id={entity} name={name} "
              f"declared={declared} actual={actual}")
    if not errors:
        print(f"PASS: {path} ({len(records)} Scene records)")
        return
    if not repair:
        raise ValueError(f"{len(errors)} Scene component payload length mismatch(es)")
    fixed = []
    for kind, entity, body in records:
        if kind == 7:
            name_size = struct.unpack_from("<H", body)[0]
            length_at = 2 + name_size + 4
            actual = len(body) - (length_at + 4)
            body = body[:length_at] + struct.pack("<I", actual) + body[length_at + 4:]
        fixed.append((kind, entity, body))
    backup = path.with_suffix(path.suffix + ".before-component-length-fix.bak")
    if backup.exists():
        raise FileExistsError(f"Backup already exists: {backup}")
    new_bytes = pack_listfile(fixed, record_versions={2: 2})
    validate_nef8_scene(new_bytes)
    if mismatches(read_nef8_scene_records(new_bytes)):
        raise AssertionError("Repaired record failed round-trip")
    shutil.copy2(path, backup)
    tmp = path.with_suffix(path.suffix + ".repair.tmp")
    tmp.write_bytes(new_bytes)
    tmp.replace(path)
    print(f"REPAIRED: {path}; backup: {backup}; records: {len(fixed)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("scene", type=pathlib.Path)
    parser.add_argument("--repair", action="store_true")
    args = parser.parse_args()
    run(args.scene, args.repair)
