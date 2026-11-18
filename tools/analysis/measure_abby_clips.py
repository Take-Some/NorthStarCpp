#!/usr/bin/env python3
"""Read-only measurement of imported Abby NEF8/YCD motion tracks.

Measures *authored local joint displacement* in units/second, NOT gameplay
speed. Loop clips can be in-place, and their root translation is not always
a reliable representation of the original game's character controller.
"""
from __future__ import annotations
import json
import math
from pathlib import Path
import struct
import zlib

root = Path(__file__).resolve().parents[2]
path = root / "engine/northstarcpp/resources/sharedAssets/characters/abby/animations/mm-explore.ycd"
wanted = (
    "abby-mm-explore-walk-loop-fw",
    "abby-mm-explore-run-loop-fw",
    "abby-mm-explore-sprint-loop-fw",
    "abby-mm-explore-idle",
)
data = path.read_bytes()
if data[:4] != b"NEF8":
    raise ValueError("Not NEF8")
body_size = struct.unpack_from("<Q", data, 16)[0]
metadata_size = len(data) - 32 - body_size
metadata = json.loads(data[32:32 + metadata_size])
body = memoryview(zlib.decompress(data[32 + metadata_size:], -15))
if len(body) != struct.unpack_from("<Q", data, 24)[0] or metadata["content_kind"] != "ycd_clip_dictionary":
    raise ValueError("Invalid body or content kind")
version, count = struct.unpack_from("<II", body)
table, strings, payload = struct.unpack_from("<QQQ", body, 8)[0], struct.unpack_from("<Q", body, 16)[0], struct.unpack_from("<Q", body, 32)[0]
if version not in (2, 3) or count != len(metadata["entries"]) or table != 48:
    raise ValueError("Header mismatch")
output = {"source": str(path), "source_game": metadata.get("source"),
          "dictionary_clips": count, "measurements": []}
for i in range(count):
    record = table + i * 64
    offset = struct.unpack_from("<I", body, record + 8)[0]
    name_end = body[strings + offset:payload].tobytes().index(bytes([0]))
    name = body[strings + offset:strings + offset + name_end].tobytes().decode("utf8")
    if name not in wanted:
        continue
    tracks, frames = struct.unpack_from("<II", body, record + 16)
    duration, fps = struct.unpack_from("<ff", body, record + 24)
    start, size = struct.unpack_from("<QQ", body, record + 40)
    if size != tracks * (4 + frames * 40):
        raise ValueError(f"Invalid clip payload {name}")
    joint_ids = struct.unpack_from("<" + "I" * tracks, body, start)
    report = {"clip": name, "duration_seconds": duration,
              "sample_rate_hz": fps, "frames": frames, "tracks": tracks,
              "root_motion_candidates": []}
    pos_start = start + tracks * 4
    for index in range(tracks):
        joint_id = joint_ids[index]
        if joint_id not in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11):
            continue
        positions = [struct.unpack_from("<fff", body,
                        pos_start + (frame * tracks + index) * 40)
                     for frame in range(frames)]
        first, last = positions[0], positions[-1]
        dx, dy, dz = (last[k] - first[k] for k in range(3))
        horizontal = math.hypot(dx, dz)
        path_length = sum(
            math.hypot(positions[j][0]-positions[j-1][0],
                       positions[j][2]-positions[j-1][2])
            for j in range(1, len(positions))
        )
        report["root_motion_candidates"].append({
            "joint_id": joint_id, "first": [round(x, 5) for x in first],
            "last": [round(x, 5) for x in last],
            "horizontal_delta": round(horizontal, 5),
            "path_length": round(path_length, 5),
            "net_horizontal_units_per_s": round(horizontal / duration, 5),
            "sample_path_units_per_s": round(path_length / duration, 5)
        })
    output["measurements"].append(report)
print(json.dumps(output, indent=2, ensure_ascii=False))
