#!/usr/bin/env python3
"""Strict verified-transforms -> NorthStar NEF8 YCD v3 importer.

This is NOT an ICE or ANIMMAGICSTR decompressor. It only accepts REAL
per-frame tracks from a separate decoder, with SHA256-bound original input.
No bind-pose fillers, retarget guessed joints, or empty pose animations.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
from blake3 import blake3
from native_ice_header import parse_header
from native_stm_header import inspect as inspect_stm
from native_pak import NaughtyDogPak

MAX_BYTES = 64 * 1024 * 1024
VALID_NAME = re.compile(r"^[a-zA-Z0-9_.-]{1,128}$")
SCHEMA = "northstar.tlou2.decoded_joint_tracks.v1"


class ImportErrorInvariant(ValueError):
    pass


def ensure(condition: bool, explanation: str) -> None:
    if not condition:
        raise ImportErrorInvariant(explanation)


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def unpack_nef8(path: Path) -> bytes:
    source = path.read_bytes()
    ensure(len(source) >= 64 and source[:4] == b"NEF8" and source[4] == 2, "Invalid NEF8 YMT")
    zipped, plain = struct.unpack_from("<QQ", source, 16)
    ensure(0 < zipped <= len(source) - 64 and 0 < plain <= MAX_BYTES, "Invalid NEF8 size")
    decoded = zlib.decompress(source[-zipped:], -15)
    ensure(len(decoded) == plain and blake3(decoded).digest() == source[32:64],
           "Invalid source YMT body checksum")
    return decoded


def skeleton_names(path: Path) -> list[str]:
    root = ET.fromstring(unpack_nef8(path))
    joints = root.findall("./Skeleton/Joint")
    ensure(1 <= len(joints) <= 4096, "Invalid YMT joint count")
    names = []
    for i, node in enumerate(joints):
        ensure(int(node.attrib["index"]) == i, "Noncontiguous YMT joint order")
        name = node.attrib["name"]
        ensure(name and name not in names, "Invalid or duplicate YMT joint name")
        names.append(name)
    return names


def valid_sample(sample: object, clip_name: str, joint_name: str) -> tuple[float, ...]:
    ensure(isinstance(sample, list) and len(sample) == 10, "Each pose requires 10 float values")
    ensure(all(isinstance(n, (int, float)) and not isinstance(n, bool) for n in sample),
           "Pose data must contain numeric components")
    pose = tuple(float(x) for x in sample)
    ensure(all(math.isfinite(x) and abs(x) <= 10000 for x in pose),
           "Nonfinite or out-of-range bone key in " + clip_name + "/" + joint_name)
    q_mag = math.sqrt(sum(x * x for x in pose[3:7]))
    ensure(abs(q_mag - 1.0) < .025, "Nonunit source quaternion: " + clip_name + "/" + joint_name)
    ensure(all(0 <= x <= 1000 for x in pose[7:10]),
           "Invalid animation scale in " + clip_name + "/" + joint_name)
    return pose


def build(document: dict) -> tuple[bytes, dict]:
    ensure(document.get("schema") == SCHEMA, "Expected explicitly decoded joint track schema")
    decoder = document.get("decoder")
    ensure(isinstance(decoder, dict) and decoder.get("verified") is True and
           isinstance(decoder.get("name"), str) and decoder["name"].strip(),
           "Only verified decoder output may be imported")
    ensure(document.get("coordinate_basis") == "source_native_ymt",
           "Decoder must declare the original YMT coordinate/quaternion basis")
    source = Path(document["source_file"])
    rig_path = Path(document["skeleton_ymt"])
    ensure(source.suffix.lower() in (".iceanim", ".stm"),
           "Source must be original ICE clip or ANIMMAGICSTR scene stream")
    logical = document.get("logical_path")
    skeleton_ref = document.get("skeleton_reference")
    ensure(isinstance(logical, str) and re.fullmatch(r"[A-Za-z0-9_/-]+\.ycd", logical) is not None and
           ".." not in logical and not logical.startswith("/"), "Unsafe logical YCD asset path")
    ensure(isinstance(skeleton_ref, str) and re.fullmatch(r"[A-Za-z0-9_/-]+\.ymt", skeleton_ref) is not None and
           ".." not in skeleton_ref and not skeleton_ref.startswith("/"), "Unsafe logical YMT reference")
    ensure(source.is_file() and rig_path.is_file(), "Source animation stream or YMT absent")
    ensure(file_hash(source) == document["source_sha256"], "Native source SHA256 mismatch")
    ensure(file_hash(rig_path) == document["skeleton_sha256"], "Native YMT source SHA256 mismatch")
    ice = None
    if source.suffix.lower() == ".iceanim":
        ice = parse_header(source.read_bytes())
        ensure(ice["kind"] == "compressed_ice_clip",
               "External streaming stubs are not decodable ICE")
    else:
        stream = inspect_stm(source)
        scene_pak = Path(document["source_scene_pak"])
        ensure(scene_pak.is_file() and file_hash(scene_pak) == document["source_scene_pak_sha256"],
               "Missing or modified original cinematic actor PAK")
        actor_ref = document.get("source_anim_reference")
        ensure(isinstance(actor_ref, str) and actor_ref.startswith("isaac="),
               "Original cinematic actor reference must explicitly identify Isaac")
        ensure(sum(entry["name"] == actor_ref for entry in NaughtyDogPak(scene_pak).typed("ANIM")) == 1,
               "Missing or duplicate Isaac ANIM reference in source PAK")
    names = skeleton_names(rig_path)
    index = {name: i for i, name in enumerate(names)}
    raw_clips = document.get("clips")
    ensure(isinstance(raw_clips, list) and len(raw_clips) == 1,
           "One original ICE buffer must map to exactly one output clip")
    clip = raw_clips[0]
    name = clip.get("name")
    ensure(isinstance(name, str) and VALID_NAME.fullmatch(name) is not None,
           "Unsafe or invalid native YCD clip name")
    frames, rate = clip.get("frames"), clip.get("sample_rate_hz")
    ensure(isinstance(frames, int) and not isinstance(frames, bool) and
           1 <= frames <= 12000 and
           (ice is None or frames == ice["frame_count"]), "Source frame count mismatch")
    ensure(isinstance(rate, (int, float)) and not isinstance(rate, bool) and
           math.isfinite(float(rate)) and 1 <= float(rate) <= 240 and
           (ice is None or abs(float(rate) - ice["sample_rate_hz"]) < 1e-4),
           "Source sample rate does not match decoded tracks")
    loop = clip.get("loop")
    ensure(type(loop) is bool, "Loop state must be explicit, not inferred from clip name")
    root_mode = clip.get("root_motion")
    ensure(root_mode in ("preserve", "none"),
           "Root Motion in-place/capsule extraction is not integrated; preserve or none only")
    tracks = clip.get("tracks")
    ensure(isinstance(tracks, list) and 1 <= len(tracks) <= len(names) and
           len(tracks) * frames <= 1_500_000, "Invalid track count or output sample extent")
    joint_ids = []
    source_samples = []
    for track in tracks:
        joint = track.get("joint")
        ensure(isinstance(joint, str) and joint in index, "Unknown YMT joint: " + str(joint))
        id_ = index[joint]
        ensure(id_ not in joint_ids, "Duplicate YMT joint track: " + joint)
        samples = track.get("samples")
        ensure(isinstance(samples, list) and len(samples) == frames,
               "Incomplete or fabricated key count on " + joint)
        joint_ids.append(id_)
        source_samples.append([valid_sample(s, name, joint) for s in samples])
    # A clip without key variation is permitted only as an explicit static clip.
    dynamic = any(
        any(abs(x-y) > 1e-5 for x,y in zip(track[0],sample))
        for track in source_samples for sample in track[1:])
    static = clip.get("static")
    ensure(type(static) is bool and (dynamic or static),
           "No animated channels: static clips must be explicitly declared")
    ensure(not (dynamic and static), "An animated clip must not be marked static")
    if root_mode == "preserve":
        ensure("root" in [track["joint"] for track in tracks],
               "Preserving Root Motion requires an explicitly decoded root joint")
    if static and root_mode == "preserve":
        ensure(False, "Static clip cannot claim motion-bearing root")
    if root_mode == "none":
        for spec, samples in zip(tracks, source_samples):
            if spec["joint"] == "root":
                ensure(all(all(abs(a-b)<1e-5 for a,b in zip(samples[0][:3], s[:3]))
                           for s in samples[1:]),
                       "Root translation is animated; root_motion=none would hide it")
    n = len(tracks)
    track_bytes = bytearray(struct.pack("<" + "I" * n, *joint_ids))
    for frame in range(frames):
        for track in source_samples:
            track_bytes.extend(struct.pack("<10f", *track[frame]))
    ensure(len(track_bytes) == n * (4 + 40 * frames),
           "Incorrect packed YCD track byte count")
    strings = name.encode("ascii") + b"\0"
    string_offset = 48 + 64
    payload_offset = (string_offset + len(strings) + 15) & ~15
    body = bytearray(payload_offset)
    struct.pack_into("<IIQQQQ", body, 0, 3, 1, 48, string_offset, 0, payload_offset)
    body[string_offset:string_offset+len(strings)] = strings
    struct.pack_into("<II", body, 48+16, n, frames)
    duration = (frames-1)/rate if frames>1 else 1/rate
    struct.pack_into("<ff", body, 48+24, duration, rate)
    struct.pack_into("<I", body, 48+32, int(loop))
    struct.pack_into("<QQ", body, 48+40, len(body), len(track_bytes))
    body.extend(track_bytes)
    ensure(len(body) <= MAX_BYTES, "YCD uncompressed dictionary > 64 MiB")
    raw = bytes(body)
    # A real decoder's verification claim is provenance, not independent proof.
    meta = {
        "schema": "newengine.asset.list_file.header_metadata",
        "logical_path": document["logical_path"],
        "content_kind": "ycd_clip_dictionary",
        "authored_by": "NorthStarCpp verified keyframe importer",
        "decoder": decoder["name"],
        "source_format": "ANIMMAGICSTR" if ice is None else "ICE",
        "source_anim_reference": document.get("source_anim_reference") if ice is None else None,
        "source_ice_sha256": document["source_sha256"],
        "source_ymt_sha256": document["skeleton_sha256"],
        "root_motion_mode": root_mode,
        "entries": [{"name":name,"dependencies":[{
            "kind":"skeleton","reference":document["skeleton_reference"]}]}],
    }
    compressed = zlib.compress(raw, level=9, wbits=-15)
    hdr = bytearray(64)
    hdr[:4] = b"NEF8"
    struct.pack_into("<BBHHHIQQ", hdr, 4, 2, 6, 11, 7, 2, 1, len(compressed), len(raw))
    hdr[32:64] = blake3(raw).digest()
    blob = bytes(hdr) + json.dumps(meta, ensure_ascii=False, separators=(",",":")).encode("utf-8") + compressed
    ensure(zlib.decompress(blob[-len(compressed):], -15) == raw, "YCD roundtrip failed")
    return blob, {
        "clip": name, "joint_tracks": n, "frames": frames,
        "rate": rate, "root_motion_mode": root_mode,
        "keyed_joint_names": [t["joint"] for t in tracks],
        "source_ice_sha256": document["source_sha256"],
        "output_sha256": hashlib.sha256(blob).hexdigest(),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", type=Path, required=True, help="Decoder-produced verified track JSON")
    ap.add_argument("--output", type=Path, help="Output YCD (not source ICE)")
    ap.add_argument("--write", action="store_true", help="Opt-in to writing a real YCD")
    args = ap.parse_args()
    if args.write and args.output is None:
        ap.error("--write requires --output")
    document = json.loads(args.input.read_text(encoding="utf-8"))
    blob, report = build(document)
    if args.write:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        if args.output.exists() and args.output.read_bytes() != blob:
            raise FileExistsError(f"Refusing to replace existing YCD: {args.output}")
        if not args.output.exists():
            tmp = args.output.with_suffix(".ycd.tmp")
            tmp.write_bytes(blob)
            ensure(tmp.read_bytes() == blob, "Staged YCD differs from encoded payload")
            os.replace(tmp, args.output)
        report["output"] = str(args.output)
    else:
        report["status"] = "verified_in_memory_no_output"
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, KeyError, OSError, struct.error, ET.ParseError) as error:
        print("YCD IMPORT FAILED:", error, file=sys.stderr)
        raise SystemExit(2)
