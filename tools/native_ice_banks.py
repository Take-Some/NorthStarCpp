#!/usr/bin/env python3
"""Inventory and export original, compressed Naughty Dog ICE animation clips.

Reads local TLOU2 Remastered PAKs; does not decode compressed bone keys,
retarget a skeleton, or claim that clips are playable NorthStar YCD assets.
Exports to a private directory outside the public NorthStarCpp repository.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

from native_pak import NaughtyDogPak, PakError

ARCHIVE_ROOT = Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\archives\sp-common\actor97")
DEST_ROOT = Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\animations")
ICE_MAGIC_PREFIX = bytes.fromhex("02014341")


def clip_name(value: str, index: int) -> str:
    stem = re.sub(r"[^a-z0-9_-]+", "_", value.lower()).strip("_")
    return stem[:120] or f"unnamed_{index:04d}"


def run_bank(path: Path, output: Path, scan: bool, overwrite: bool, character: str) -> dict:
    pak = NaughtyDogPak(path)
    all_anims = pak.typed("ANIM")
    anims = [a for a in all_anims if a["name"].lower().startswith("isaac=")] if character == "isaac" else all_anims
    raw = pak.typed("ANIM_RAWCHANNELS")
    report = {
        "bank": path.name,
        "source": str(path.resolve()),
        "source_sha256": hashlib.sha256(pak.data).hexdigest(),
        "native_format": "Naughty Dog ICE compressed animation source",
        "decoded_to_ycd": False,
        "anim_resource_count": len(anims),
        "other_actors_in_bank": len(all_anims) - len(anims),
        "rawchannels_resource_count": len(raw),
        "clips": [],
    }
    seen = set()
    failures = []
    for i, anim in enumerate(anims):
        name = anim["name"]
        try:
            base = anim["offset"]
            payload = pak.pointer(base + 56)
            rawchannels = pak.pointer(base + 64)
            n = pak.u32(payload + 8)
            if not 64 <= n <= 20 * 1024 * 1024 or payload + n > len(pak.data):
                raise PakError(f"invalid compressed payload size {n}")
            chunk = pak.data[payload : payload + n]
            if chunk[:4] != ICE_MAGIC_PREFIX:
                raise PakError(f"unexpected ICE format prefix {chunk[:8].hex()}")
            h = hashlib.sha256(chunk).hexdigest()
            stem = clip_name(name, i)
            if stem in seen:
                stem = f"{stem}_{i:04d}"
            seen.add(stem)
            filename = stem + ".iceanim"
            if not scan:
                output.mkdir(parents=True, exist_ok=True)
                target = output / filename
                if target.is_file():
                    if hashlib.sha256(target.read_bytes()).hexdigest() != h:
                        if not overwrite:
                            raise FileExistsError(f"different file exists: {target}")
                    else:
                        report["clips"].append({
                            "name": name, "file": filename, "sha256": h,
                            "bytes": n, "source_ice_header8": chunk[:8].hex(), "source_data_offset": payload,
                            "source_rawchannels_offset": rawchannels,
                            "source_flags": pak.u32(base + 52),
                            "version": pak.u32(base + 48),
                        })
                        continue
                tmp = target.with_suffix(".iceanim.tmp")
                tmp.write_bytes(chunk)
                tmp.replace(target)
            report["clips"].append({
                "name": name, "file": filename, "sha256": h,
                "bytes": n, "source_ice_header8": chunk[:8].hex(), "source_data_offset": payload,
                "source_rawchannels_offset": rawchannels,
                "source_flags": pak.u32(base + 52),
                "version": pak.u32(base + 48),
            })
        except (OSError, ValueError) as exc:
            failures.append({"index": i, "name": name, "error": str(exc)})
    report["invalid_clips"] = failures
    report["valid_clip_count"] = len(report["clips"])
    report["total_original_bytes"] = sum(c["bytes"] for c in report["clips"])
    if not scan:
        output.mkdir(parents=True, exist_ok=True)
        (output / "manifest.json").write_text(
            json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    return report


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--character", choices=("ellie", "isaac"), required=True)
    ap.add_argument("--input-root", type=Path, default=ARCHIVE_ROOT)
    ap.add_argument("--output-root", type=Path, default=DEST_ROOT)
    ap.add_argument("--scan-only", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--bank", action="append", default=[], help="Exact PAK filename(s)")
    args = ap.parse_args()
    if not args.input_root.is_dir():
        print(f"Input directory missing: {args.input_root}", file=sys.stderr)
        return 2
    banks = (sorted(args.input_root.glob("seq-fob-isaac*.pak")) if args.character == "isaac"
             else sorted(args.input_root.glob(f"anim-{args.character}-*.pak")))
    if args.bank:
        selected = set(args.bank)
        banks = [b for b in banks if b.name in selected]
    if not banks:
        print(f"No dedicated anim-{args.character}-*.pak bank found; no character clips exported.")
        return 3
    results = []
    for path in banks:
        try:
            report = run_bank(
                path, args.output_root / args.character / path.stem,
                args.scan_only, args.force, args.character
            )
            results.append(report)
            print(
                f"{path.name}: {report['valid_clip_count']}/{report['anim_resource_count']} "
                f"ICE clips, {report['total_original_bytes']} bytes, "
                f"{len(report['invalid_clips'])} invalid"
            )
        except (OSError, ValueError) as exc:
            results.append({"bank": path.name, "error": str(exc)})
            print(f"ERROR {path.name}: {exc}", file=sys.stderr)
    summary = {
        "schema": "northstar.tlou2.original_ice_archive.v1",
        "character": args.character, "scan_only": args.scan_only,
        "banks": len(results),
        "valid_clips": sum(x.get("valid_clip_count", 0) for x in results),
        "invalid_clips": sum(len(x.get("invalid_clips", [])) for x in results),
        "unreadable_banks": [x for x in results if "error" in x],
        "total_original_bytes": sum(x.get("total_original_bytes", 0) for x in results),
        "decoded_ycd_count": 0,
        "note": "Original compressed ICE buffers only; no verified decoder or bone retargeting",
    }
    if not args.scan_only:
        out = args.output_root / args.character
        out.mkdir(parents=True, exist_ok=True)
        (out / "manifest.json").write_text(
            json.dumps({"summary": summary, "banks": [
                {"bank": x["bank"], "valid_clips": x.get("valid_clip_count", 0),
                 "invalid_clips": x.get("invalid_clips", []),
                 "total_original_bytes": x.get("total_original_bytes", 0)}
                for x in results]}, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    print("SUMMARY " + json.dumps(summary, ensure_ascii=False))
    return 1 if summary["unreadable_banks"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
