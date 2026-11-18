#!/usr/bin/env python3
"""Verified ICE clip *metadata* reader. Does not decode compressed joint samples.

Headers are read from original TLOU2 PC source buffers. Refuses to equate
frame count / timing with decoded bone tracks. Streaming headers with no local
payload are explicitly classified as external stream references.
"""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
import math
import struct
from pathlib import Path

DEFAULT_ROOT = Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\animations\ellie")
PREFIX = bytes.fromhex("02014341")


class IceFormatError(ValueError):
    pass


def parse_header(blob: bytes) -> dict:
    if len(blob) < 64 or blob[:4] != PREFIX:
        raise IceFormatError("Not a complete native ICE header")
    total = struct.unpack_from("<I", blob, 8)[0]
    version, frame_count = struct.unpack_from("<HH", blob, 12)
    rate = struct.unpack_from("<f", blob, 28)[0]
    frames_float = struct.unpack_from("<f", blob, 40)[0]
    time_step = struct.unpack_from("<f", blob, 44)[0]
    if total != len(blob) or len(blob) > 20 * 1024 * 1024:
        raise IceFormatError("ICE byte extent does not match the source")
    if not math.isfinite(rate) or not 1.0 <= rate <= 240.0:
        raise IceFormatError("Invalid ICE sample frequency")
    if not math.isfinite(time_step) or abs(time_step - 1.0 / rate) > 1e-3:
        raise IceFormatError("Inconsistent ICE frame time")
    if len(blob) == 64 and version == 0 and frame_count == 0:
        return {
            "kind": "external_stream_reference",
            "codec_version": version,
            "frame_count": 0,
            "sample_rate_hz": rate,
            "duration_seconds": None,
            "payload_decoded": False,
        }
    if version not in {1, 2, 6, 7} or len(blob) < 144 or frame_count < 1 or frame_count > 12000:
        raise IceFormatError("Unsupported ICE version/frame count or truncated payload")
    if not math.isfinite(frames_float) or abs(frames_float - (frame_count - 1)) > 1.1:
        raise IceFormatError("ICE duration does not agree with frame count")
    # The local 0x18 header section-table offset is verified but *not*
    # interpreted as animation channels. Those remain codec-dependent.
    sections_offset = struct.unpack_from("<I", blob, 24)[0]
    if sections_offset < 48 or sections_offset > len(blob):
        raise IceFormatError("Invalid ICE section table offset")
    return {
        "kind": "compressed_ice_clip",
        "codec_version": version,
        "frame_count": frame_count,
        "sample_rate_hz": rate,
        "duration_seconds": round((frame_count - 1) / rate, 9),
        "duration_frame_count": frames_float,
        "section_table_offset": sections_offset,
        "payload_decoded": False,
    }


def inventory(root: Path) -> dict:
    if not root.is_dir():
        raise FileNotFoundError(root)
    found = sorted(root.rglob("*.iceanim"))
    if not found:
        raise IceFormatError("No original ICE clips found")
    versions = collections.Counter()
    statuses = collections.Counter()
    errors = []
    records = []
    for path in found:
        raw = path.read_bytes()
        try:
            info = parse_header(raw)
            versions[str(info["codec_version"])] += 1
            statuses[info["kind"]] += 1
            records.append({
                "path": path.relative_to(root).as_posix(),
                "sha256": hashlib.sha256(raw).hexdigest(),
                "size": len(raw),
                **info
            })
        except IceFormatError as exc:
            errors.append({"path": str(path.relative_to(root)), "error": str(exc)})
    return {
        "schema": "northstar.tlou2.ice.metadata.v1",
        "source_root": str(root),
        "clip_count": len(found),
        "compressed_clip_count": statuses["compressed_ice_clip"],
        "external_stream_reference_count": statuses["external_stream_reference"],
        "codec_versions": dict(sorted(versions.items())),
        "errors": errors,
        "decoded_joint_tracks": 0,
        "clips": records,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--output", type=Path, help="Manifest path (never use source game folder)")
    args = parser.parse_args()
    report = inventory(args.root)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "clips"}, ensure_ascii=False, indent=2))
    return 1 if report["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
