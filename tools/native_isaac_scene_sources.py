#!/usr/bin/env python3
"""Preserve authentic Isaac-specific TLOU2 Remastered cinematic animation sources.

Each Isaac ANIM entry in the Forward Base "Isaac's Refusal" sequences references
a cinematic stream. This utility stages original PAK metadata and ANIMMAGICSTR
STM payloads as an isolated, hash-verified research set, not playable YCD.
Scene STM files contain additional actors; tracks are NOT individually decoded.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

from native_pak import NaughtyDogPak

DEFAULT_INPUT = Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\archives\world-forward-base")
DEFAULT_OUTPUT = Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\animations\isaac\cinematic_streams")
STM_MAGIC = b"ANIMMAGICSTR"


def sha256(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open("rb") as inp:
        for block in iter(lambda: inp.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def preserve(src: Path, dst: Path, dry_run: bool) -> dict:
    if not src.is_file():
        raise FileNotFoundError(src)
    origin = sha256(src)
    if not dry_run:
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.is_file():
            if sha256(dst) != origin:
                raise FileExistsError(f"Different output file exists; refusing overwrite: {dst}")
        else:
            tmp = dst.with_suffix(dst.suffix + ".tmp")
            shutil.copyfile(src, tmp)
            if sha256(tmp) != origin:
                tmp.unlink(missing_ok=True)
                raise IOError(f"Copy hash differs from source: {src}")
            tmp.replace(dst)
    return {"filename": src.name, "bytes": src.stat().st_size, "sha256": origin}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input-root", type=Path, default=DEFAULT_INPUT)
    ap.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--scan-only", action="store_true")
    args = ap.parse_args()
    actor = args.input_root / "actor97"
    streams = args.input_root / "animstream97"
    banks = sorted(actor.glob("seq-fob-isaacs-refusal=*.pak"))
    if not banks:
        print(f"Isaac cinematic PAK banks not found: {actor}", file=sys.stderr)
        return 2
    records = []
    for bank in banks:
        pak = NaughtyDogPak(bank)
        entries = [
            {"name": a["name"], "source_resource_offset": a["offset"]}
            for a in pak.typed("ANIM") if a["name"].lower().startswith("isaac=")
        ]
        if not entries:
            continue
        seq_name = bank.stem
        matches = sorted(streams.glob(seq_name + ".cin-stream*.stm"))
        if len(matches) != 1:
            raise ValueError(f"Expected one cinematic STM in {seq_name}; found {len(matches)}")
        stream = matches[0]
        with stream.open("rb") as src:
            head = src.read(16)
        if head[4:16] != STM_MAGIC:
            raise ValueError(f"Unexpected native cinematic stream magic: {stream}")
        dest = args.output_root / seq_name
        record = {
            "sequence": seq_name,
            "isaac_anim_entries": entries,
            "isaac_entry_count": len(entries),
            "other_actor_anim_count": len(pak.typed("ANIM")) - len(entries),
            "anim_stream_resource_names": [x["name"] for x in pak.typed("ANIM_STREAM")],
            "source_pak": preserve(bank, dest / bank.name, args.scan_only),
            "source_scene_stm": preserve(stream, dest / stream.name, args.scan_only),
            "status": "original_cinematic_scene_source_preserved; ISAAC TRACKS NOT DECODED",
        }
        records.append(record)
        print(seq_name, "isaac entries:", len(entries), "stream bytes:", record["source_scene_stm"]["bytes"])
    manifest = {
        "schema": "northstar.tlou2.isaac.cinematic_source_preservation.v1",
        "source_game": "The Last of Us Part II Remastered PC",
        "source_archive": "world-forward-base.psarc",
        "status": "native_PAK_and_ANIMMAGICSTR_STM_sources; no individual Isaac YCD",
        "isaac_referenced_anim_entries": sum(x["isaac_entry_count"] for x in records),
        "copied_scene_streams": len(records),
        "note": "STM contains multiple actors. No claiming a frame-accurate Isaac stream decode.",
        "sequences": records,
    }
    if not args.scan_only:
        args.output_root.mkdir(parents=True, exist_ok=True)
        (args.output_root / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    print("SUMMARY " + json.dumps({k: manifest[k] for k in
          ("status", "isaac_referenced_anim_entries", "copied_scene_streams")}))
    return 0 if records else 3


if __name__ == "__main__":
    raise SystemExit(main())
