#!/usr/bin/env python3
"""Finalize a real, already-converted Ellie bundle into engine sharedAssets.

Never synthesizes geometry, skeletons, materials, textures or animations.
Dry-run by default; --apply copies validated native assets into characters/ellie.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import struct
import sys
import tempfile
from pathlib import Path

ENGINE_SHARED = Path(__file__).resolve().parents[1] / "engine/northstarcpp/resources/sharedAssets"
ELLIE = ENGINE_SHARED / "characters/ellie"
PREFAB = ENGINE_SHARED / "prefabs/characters/ellie.nprefab"
NAME_PATTERN = re.compile(r"^[a-zA-Z0-9_][a-zA-Z0-9_.-]*$")
REQUIRED = {
    "models/ellie.ydd": 2,
    "models/ellie.ymt": 10,
    "materials/ellie.ymat": 4,
    "textures/ellie.ytd": 1,
}
OPTIONAL = {"models/ellie.ydr": 2}
MODEL_REF = "characters/ellie/models/ellie.ydd"
YMAT_REF = "characters/ellie/materials/ellie.ymat"
YTD_REF = "characters/ellie/textures/ellie.ytd"
YMT_REF = "characters/ellie/models/ellie.ymt"


def check_nef8(path: Path, kind: int):
    """Check NEF8 kind, declared ranges, raw-deflate payload and BLAKE3 digest."""
    import zlib
    from blake3 import blake3

    data = path.read_bytes()
    if len(data) < 128 or data[:4] != b"NEF8":
        raise ValueError(f"Invalid NEF8: {path}")
    actual = struct.unpack_from("<H", data, 8 if data[4] == 1 else 6)[0]
    if actual != kind:
        raise ValueError(f"{path.name}: expected kind {kind}, got {actual}")
    if data[4] == 1:
        header, codec = struct.unpack_from("<HH", data, 6)[0], struct.unpack_from("<H", data, 10)[0]
        offset, count, expanded = struct.unpack_from("<QQQ", data, 16)
        if header != 128 or codec != 1 or offset < 128 or len(data)-offset != count:
            raise ValueError(f"{path.name}: invalid V1 header/lengths")
        checksum = data[64:96]
    elif data[4] == 2:
        flags = struct.unpack_from("<H", data, 8)[0]
        count, expanded = struct.unpack_from("<QQ", data, 16)
        offset = len(data) - count
        if not flags & 1 or count == 0 or offset < 64:
            raise ValueError(f"{path.name}: invalid V2 header/lengths")
        checksum = data[32:64] if flags & 4 else None
    else:
        raise ValueError(f"{path.name}: unsupported NEF8 version")
    if not 0 < expanded < 1024*1024*1024:
        raise ValueError(f"{path.name}: invalid expanded payload size")
    digest = blake3()
    decoder = zlib.decompressobj(-15)
    got = 0
    payload = memoryview(data)[offset:]
    for i in range(0, len(payload), 1024*1024):
        block = decoder.decompress(payload[i:i+1024*1024])
        got += len(block)
        if got > expanded: raise ValueError("NEF8 inflated beyond declared limit")
        digest.update(block)
    tail = decoder.flush()
    got += len(tail)
    digest.update(tail)
    if not decoder.eof or got != expanded or decoder.unused_data:
        raise ValueError(f"{path.name}: invalid or truncated NEF8 compressed body")
    if checksum is not None and digest.digest() != checksum:
        raise ValueError(f"{path.name}: NEF8 digest mismatch")
    return data


def get_ycd_entries(data: bytes, path: Path):
    # Native YCD v2 ListFile metadata is JSON directly after 32-byte header.
    if data[:4] != b"NEF8" or data[4] != 2:
        raise ValueError(f"{path}: cannot inspect clip metadata")
    try:
        metadata, _ = json.JSONDecoder().raw_decode(data[32:].decode("utf-8", errors="ignore"))
    except Exception as exc:
        raise ValueError(f"YCD header metadata is not readable in {path}: {exc}") from exc
    if metadata.get("content_kind") != "ycd_clip_dictionary":
        raise ValueError(f"{path}: expected native YCD clip dictionary")
    result = {}
    for entry in metadata.get("entries", []):
        name = entry.get("name")
        if not name:
            raise ValueError(f"{path}: an unnamed YCD clip")
        if name in result:
            raise ValueError(f"{path}: duplicate YCD clip name {name}")
        # Prevent accidentally importing Abby's clips as Ellie's native skeleton.
        for dependency in entry.get("dependencies", []):
            if dependency.get("kind") != "skeleton":
                continue
            ref = dependency.get("reference", "").lower()
            if "abby" in ref or ("ellie" not in ref):
                raise ValueError(f"{path.name}@{name}: skeleton dependency is not Ellie: {ref}")
        result[name] = entry
    if not result:
        raise ValueError(f"{path}: no native clips found")
    return result


def parse_animation_spec(spec: str):
    # e.g. idle=mm-explore.ycd@ellie-mm-explore-idle
    if "=" not in spec or "@" not in spec:
        raise ValueError("Animation must be state=filename.ycd@clip-name")
    state, asset = spec.split("=", 1)
    filename, clip = asset.split("@", 1)
    if not re.fullmatch(r"[a-z][a-z0-9_-]*", state):
        raise ValueError(f"Invalid animation state {state!r}")
    if not NAME_PATTERN.fullmatch(filename) or not filename.lower().endswith(".ycd"):
        raise ValueError(f"Invalid YCD filename {filename!r}")
    if not clip or any(x in clip for x in ("\\", "/", "\0", "\r", "\n")):
        raise ValueError("Invalid YCD clip name")
    return state, filename, clip


def validate_bundle(source: Path, animation_args: list[str]):
    paths = {}
    for rel, kind in {**REQUIRED, **OPTIONAL}.items():
        file = source / rel
        if not file.is_file():
            if rel in REQUIRED:
                raise FileNotFoundError(f"Missing required native resource: {file}")
            continue
        check_nef8(file, kind)
        paths[rel] = file
    ycd_entries = {}
    anim_dir = source / "animations"
    if anim_dir.is_dir():
        for file in sorted(anim_dir.glob("*.ycd")):
            if file.is_symlink() or not file.is_file() or not NAME_PATTERN.fullmatch(file.name):
                raise ValueError(f"Unsafe animation source {file}")
            data = check_nef8(file, 11)
            ycd_entries[file.name] = get_ycd_entries(data, file)
            paths["animations/" + file.name] = file
    animation_map = {}
    for spec in animation_args:
        state, filename, clip = parse_animation_spec(spec)
        if state in animation_map:
            raise ValueError(f"Duplicate animation state {state}")
        if filename not in ycd_entries:
            raise ValueError(f"Animation file missing from input: {filename}")
        if clip not in ycd_entries[filename]:
            raise ValueError(f"Animation clip missing in {filename}: {clip}")
        animation_map[state] = {
            "state": state,
            "source": "characters/ellie/animations/" + filename,
            "clip": clip,
        }
    return paths, animation_map


def descriptor(anim_map: dict):
    return {
        "schema": "northstar.character.v1",
        "id": "characters/ellie",
        "display_name": "Ellie",
        "model": MODEL_REF,
        "skeleton": YMT_REF,
        "appearance": {"material": YMAT_REF, "textures": YTD_REF},
        "animations": list(anim_map.values()),
    }


def prefab_text(anim_map: dict):
    lines = [
        "NorthStarCpp.Scene 3",
        'scene "Ellie"',
        "streaming 150 220 1",
        "stream_budget 64 64",
        'layer "Default" 1',
        'entity 1 0 "Ellie" "Default" "" "" "' + MODEL_REF +
        '" 0 0 0 1.00 67A0C0 0 0 0 0 0 1 1 1 0 1000000 1 0',
    ]
    default = anim_map.get("idle")
    if default:
        ref = default["source"] + "@" + default["clip"]
        # Scene V3 stores arbitrary binary component payload as lowercase hex.
        lines.append('component 1 "northstar.animation.default" 1 "' +
                     ref.encode("utf8").hex() + '"')
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", required=True, type=Path,
                    help="Directory of the converted NATIVE NorthStar bundle (not game PAKs or GLB)")
    ap.add_argument("--animation", action="append", default=[],
                    help="state=file.ycd@clip, repeatable (e.g. idle=mm-explore.ycd@ellie-idle)")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--overwrite", action="store_true",
                    help="Replace existing Ellie binary assets (no backups; use only for controlled reimport)")
    args = ap.parse_args()

    try:
        source = args.source.resolve(strict=True)
        if not source.is_dir() or source == ELLIE.resolve():
            raise ValueError("Source must be an independent directory")
        paths, animations = validate_bundle(source, args.animation)
        for rel in paths:
            dst = ELLIE / rel
            if dst.exists() and not args.overwrite:
                raise FileExistsError(f"Already exists (specify --overwrite): {dst}")
        mf = ELLIE / "ellie.character.json"
        pf = PREFAB
        result = {
            "status": "READY_FOR_COPY" if not args.apply else "IMPORTED",
            "source": str(source),
            "destination": str(ELLIE),
            "assets": list(paths),
            "animations": list(animations),
            "prefab": str(pf),
        }
        if args.apply:
            # Files verified before first destination mutation; always use a stage.
            with tempfile.TemporaryDirectory(prefix="ellie-stage-", dir=ENGINE_SHARED) as stage_name:
                stage = Path(stage_name)
                for rel, source_file in paths.items():
                    target = stage / rel
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source_file, target)
                    check_nef8(target, 11 if target.suffix==".ycd" else
                               {".ydd":2,".ydr":2,".ymt":10,".ymat":4,".ytd":1}[target.suffix])
                for rel in paths:
                    dest = ELLIE / rel
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    os.replace(stage / rel, dest)
            # Update manifest/prefab only once all assets have been copied.
            mf.parent.mkdir(parents=True, exist_ok=True)
            pf.parent.mkdir(parents=True, exist_ok=True)
            mf.write_text(json.dumps(descriptor(animations), ensure_ascii=False, indent=2)+"\n",
                          encoding="utf8")
            pf.write_text(prefab_text(animations), encoding="utf8")
        print(json.dumps(result,ensure_ascii=False,indent=2))
        return 0
    except (OSError, ValueError, KeyError) as exc:
        print("ELLIE NATIVE IMPORT BLOCKED:", exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
