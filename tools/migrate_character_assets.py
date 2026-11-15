#!/usr/bin/env python3
"""Migrate Abby to sharedAssets/characters/abby with hash-verified rollback.

--apply stages one canonical copy of each resource, moving it atomically where
Windows permits. If another process prevents a rename, it creates and checks
a temporary compatible copy and retains the old file until --finalize.
--verify checks the canonical layout; --finalize deletes only identical,
unlocked legacy aliases. No NEF8 payload is modified.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path

ASSETS = (
    ("models/abby/abby.ydd", "characters/abby/models/abby.ydd"),
    ("models/abby/abby.ymt", "characters/abby/models/abby.ymt"),
    ("materials/character_abby.ymat", "characters/abby/materials/abby.ymat"),
    ("textures/abby.ytd", "characters/abby/textures/abby.ytd"),
    ("animations/abby/fall-medium.ycd", "characters/abby/animations/fall-medium.ycd"),
    ("animations/abby/mm-explore.ycd", "characters/abby/animations/mm-explore.ycd"),
    ("animations/abby/traversal.ycd", "characters/abby/animations/traversal.ycd"),
)


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            sha.update(chunk)
    return sha.hexdigest()


def verify_manifest(root: Path) -> None:
    manifest = root / "characters/abby/abby.character.json"
    if not manifest.is_file() or manifest.stat().st_size > 32768:
        raise RuntimeError("Missing or oversized character asset descriptor")
    doc = json.loads(manifest.read_text(encoding="utf-8"))
    if doc.get("schema") != "northstar.character.v1" or doc.get("id") != "characters/abby":
        raise RuntimeError("Incorrect character schema or namespace")
    resources = [
        doc["model"], doc["skeleton"],
        doc["appearance"]["material"], doc["appearance"]["textures"],
        *(entry["source"] for entry in doc["animations"])
    ]
    states = set()
    for entry in doc["animations"]:
        state = entry["state"]
        if state in states or not state or not entry.get("clip"):
            raise RuntimeError("Duplicate or incomplete animation binding")
        states.add(state)
    canonical = {new for _, new in ASSETS}
    for ref in resources:
        if not isinstance(ref, str) or ref not in canonical or not (root / ref).is_file():
            raise RuntimeError(f"Broken character descriptor resource: {ref!r}")
    print(f"PASS characters/abby manifest ({len(resources)} resource references)")


def cleanup_dirs(root: Path) -> None:
    for suffix in ("models/abby", "animations/abby"):
        stale = root / suffix
        if stale.is_dir() and not any(stale.iterdir()):
            stale.rmdir()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--apply", action="store_true", help="Move or safely copy sources.")
    mode.add_argument("--verify", action="store_true", help="Verify all canonical resources.")
    mode.add_argument("--finalize", action="store_true", help="Remove verified legacy aliases when unlocked.")
    parser.add_argument("--assets", type=Path, help="SharedAssets root.")
    args = parser.parse_args()
    root = (args.assets or Path(__file__).resolve().parents[1] / "sharedAssets").resolve()
    if not root.is_dir():
        raise RuntimeError(f"Missing sharedAssets root: {root}")
    work = [(root / old, root / new) for old, new in ASSETS]

    if args.verify or args.finalize:
        aliases = 0
        for old, new in work:
            if not new.is_file() or new.is_symlink():
                raise RuntimeError(f"Invalid namespaced resource: {new}")
            if old.exists():
                if old.is_symlink() or digest(old) != digest(new):
                    raise RuntimeError(f"Legacy asset is not identical: {old}")
                if args.finalize:
                    try:
                        old.unlink()
                        print(f"REMOVED ALIAS {old.relative_to(root).as_posix()}")
                    except PermissionError:
                        print(f"LOCKED ALIAS {old.relative_to(root).as_posix()}")
                        aliases += 1
                else:
                    print(f"LEGACY ALIAS {old.relative_to(root).as_posix()}")
                    aliases += 1
            print(f"PASS {new.relative_to(root).as_posix()} ({new.stat().st_size} bytes)")
        verify_manifest(root)
        if args.finalize:
            cleanup_dirs(root)
        print(f"Namespace verified; outstanding legacy aliases: {aliases}")
        return 0 if aliases == 0 or args.verify else 3

    for old, new in work:
        if new.exists() or not old.is_file() or old.is_symlink():
            raise RuntimeError(f"Unsafe or already applied migration: {old} -> {new}")
        if old.stat().st_dev != root.stat().st_dev:
            raise RuntimeError(f"Outside sharedAssets volume: {old}")
    if not args.apply:
        for old, new in work:
            print(f"DRY RUN {old.relative_to(root).as_posix()} -> {new.relative_to(root).as_posix()}")
        return 0

    originals = {old: digest(old) for old, _ in work}
    moved: list[tuple[Path, Path, bool]] = []
    locked = 0
    try:
        for old, new in work:
            new.parent.mkdir(parents=True, exist_ok=True)
            copied = False
            try:
                os.replace(old, new)
            except PermissionError:
                # A renderer/Player may hold an opened YTD on Windows.
                # Never kill that process and never overwrite a live original.
                shutil.copy2(old, new)
                copied = True
                locked += 1
            moved.append((old, new, copied))
            actual = digest(new)
            if actual != originals[old]:
                raise RuntimeError(f"SHA-256 mismatch: {new}")
            print(f"PASS {new.relative_to(root).as_posix()} sha256={actual}" +
                  (" [legacy alias retained: locked]" if copied else ""))
    except Exception:
        for old, new, copied in reversed(moved):
            if not new.exists():
                continue
            if copied:
                new.unlink()
            elif not old.exists():
                old.parent.mkdir(parents=True, exist_ok=True)
                os.replace(new, old)
        raise

    cleanup_dirs(root)
    verify_manifest(root)
    print(f"Canonical migration complete; {len(work)} hashes verified; locked aliases: {locked}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
