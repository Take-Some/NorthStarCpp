#!/usr/bin/env python3
"""Restore MT28 material identity from the source YDR model slot manifest.

The imported NVRM mesh stores original source material names in
material_slots[]. Existing YMAT XML preserved slot order and material
properties but exported anonymous mat_0, mat_1, ... identities.

Default: read-only preflight. --apply: preserve originals in a dated backup
outside game assets, write canonical NEF8 v1 with XML material library,
verify every replacement. Refuse ambiguous or conflicting data.
"""
from __future__ import annotations

import argparse
import copy
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import uuid
import xml.etree.ElementTree as ET
import zlib

from blake3 import blake3

REPO = Path(__file__).resolve().parents[1]
PROJECTS = ("KaylaNightCity", "NorthStarFirstPersonPlatform")
MATERIALS = Path("assets/materials/mahir_mt28")
MODELS = Path("assets/models/mahir_mt28")
MAX_BODY = 256 * 1024 * 1024
MATERIAL_RE = re.compile(rb'(<Material\s+name=")mat_(\d+)(")')


class RepairError(RuntimeError):
    pass


def require(ok: bool, message: str) -> None:
    if not ok:
        raise RepairError(message)


def expanded(data: bytes, expected: int) -> bytes:
    require(0 < expected <= MAX_BODY, "Invalid NEF8 uncompressed size")
    dec = zlib.decompressobj(-15)
    body = dec.decompress(data, MAX_BODY + 1)
    body += dec.flush()
    require(dec.eof and not dec.unused_data and len(body) == expected,
            "Invalid NEF8 DEFLATE stream or expanded length")
    return body


def unpack_nef8(data: bytes, kind: int) -> tuple[bytes, int]:
    require(data[:4] == b"NEF8", "Not a NorthStar NEF8 ListFile")
    major = data[4]
    if major == 2:
        require(len(data) >= 64 and data[5] in (6, 8),
                "Expected supported NEF8 v2.6/v2.8")
        prefix = 64
        k = struct.unpack_from("<H", data, 6)[0]
        flags = struct.unpack_from("<H", data, 8)[0]
        count = struct.unpack_from("<I", data, 12)[0]
        require(k == kind and flags & 1, "Unexpected NEF8 kind or compression")
        compressed, uncompressed = struct.unpack_from("<QQ", data, 16)
        require(0 < compressed <= len(data)-prefix, "Invalid NEF8 payload range")
        body = expanded(data[-compressed:], uncompressed)
        if flags & 4:
            require(blake3(body).digest() == data[32:64],
                    "Invalid original NEF8 v2 source body digest")
        return body, count
    require(major == 1 and len(data) >= 128, "Unsupported NEF8 version")
    k = struct.unpack_from("<H", data, 8)[0]
    offset, compressed, uncompressed, count = struct.unpack_from("<QQQQ", data, 16)
    require(k == kind and offset >= 128 and compressed == len(data) - offset,
            "Unexpected NEF8 v1 kind or payload range")
    body = expanded(data[offset:], uncompressed)
    require(blake3(body).digest() == data[64:96],
            "Invalid canonical NEF8 body digest")
    return body, count


def encode_xml_ymat(xml: bytes, count: int) -> bytes:
    require(0 < count <= 65536, "Invalid YMAT entry count")
    raw = zlib.compressobj(level=9, wbits=-15)
    payload = raw.compress(xml) + raw.flush()
    header = bytearray(128)
    header[:4] = b"NEF8"
    struct.pack_into("<HHHHH", header, 4, 1, 128, 4, 1, 1)
    struct.pack_into("<QQQQ", header, 16, 128, len(payload), len(xml), count)
    struct.pack_into("<Q", header, 112, 1)
    header[64:96] = blake3(xml).digest()
    output = bytes(header) + payload
    decoded, read_count = unpack_nef8(output, 4)
    require(decoded == xml and read_count == count, "Canonical YMAT roundtrip failed")
    return output


def model_slots(model_path: Path) -> tuple[list[dict], str]:
    data = model_path.read_bytes()
    body, _ = unpack_nef8(data, 6)
    require(body[:4] == b"NVRM" and len(body) >= 20,
            "Missing original runtime-model NVRM metadata: " + str(model_path))
    metadata_size = struct.unpack_from("<I", body, 8)[0]
    require(0 < metadata_size < len(body) - 20,
            "Invalid NVRM metadata length: " + str(model_path))
    info = json.loads(body[20:20+metadata_size])
    require(info.get("schema") == "northstar.model.runtime.v1",
            "Wrong runtime-model metadata schema: " + str(model_path))
    slots = info.get("material_slots")
    require(isinstance(slots, list) and 0 < len(slots) < 65536,
            "Model is missing authoritative source material slots: " + str(model_path))
    names = []
    for index, slot in enumerate(slots):
        name = slot.get("name")
        require(slot.get("slot") == index and isinstance(name, str) and
                re.fullmatch(r"[A-Za-z0-9_.-]{1,160}", name) is not None,
                "Invalid original material slot: " + str(model_path))
        names.append(name)
    require(len(set(names)) == len(names),
            "Ambiguous duplicate original material names: " + str(model_path))
    return slots, hashlib.sha256(data).hexdigest()


def prepare(project: str, ymat: Path) -> dict:
    model = REPO / "Projects" / project / MODELS / (ymat.stem + ".ydr")
    require(model.is_file(), "Missing matching original model: " + str(model))
    slots, model_sha = model_slots(model)
    old = ymat.read_bytes()
    xml, count = unpack_nef8(old, 4)
    require(xml.startswith(b"<?xml") and b"YmatMaterialLibrary" in xml[:200],
            "YMAT is not an original XML material library: " + str(ymat))
    original = ET.fromstring(xml)
    require(original.tag == "YmatMaterialLibrary" and
            original.get("schema") == "northstarcpp.ymat.material_library.v2",
            "Unexpected native XML schema: " + str(ymat))
    mats = original.findall("Material")
    require(count == len(slots) == len(mats), "Model/YMAT slot count mismatch: " + str(ymat))
    require(len(MATERIAL_RE.findall(xml)) in (0, len(slots)),
            "Partly anonymized material library: " + str(ymat))
    original_names = [m.get("name") for m in mats]
    expected_names = [s["name"] for s in slots]
    already_fixed = original_names == expected_names
    require(already_fixed or original_names == [f"mat_{i}" for i in range(len(slots))],
            "Model/YMAT identity already modified or ambiguous: " + str(ymat))
    glass = 0
    for index, (material, slot) in enumerate(zip(mats, slots)):
        surface = material.find("Surface")
        require(surface is not None and
                surface.get("blend") == slot.get("blend_mode"),
                "Source material blend mismatch at " + str(ymat) + f"@{index}")
        if slot.get("surface_domain") == "glass":
            require(surface.get("blend") == "alpha",
                    "Source glass has lost transparency at " + str(ymat) + f"@{index}")
            glass += 1
    if already_fixed:
        return dict(project=project, relative=str(ymat.relative_to(REPO)),
                    path=ymat, model_sha256=model_sha, original_sha256=hashlib.sha256(old).hexdigest(),
                    result_sha256=hashlib.sha256(old).hexdigest(), names=expected_names,
                    count=len(slots), glass=glass, changed=False, output=None, original=old)
    seen = []
    def convert(match: re.Match[bytes]) -> bytes:
        idx = int(match.group(2))
        require(idx == len(seen) and idx < len(expected_names),
                "YMAT XML slots are not ordered: " + str(ymat))
        seen.append(idx)
        return match.group(1) + expected_names[idx].encode("ascii") + match.group(3)
    corrected = MATERIAL_RE.sub(convert, xml)
    require(len(seen) == len(slots), "Material identity substitution incomplete")
    verified = ET.fromstring(corrected)
    require([m.get("name") for m in verified.findall("Material")] == expected_names,
            "Recovered identity does not match original YDR")
    comparison = copy.deepcopy(original)
    for material, name in zip(comparison.findall("Material"), expected_names):
        material.set("name", name)
    require(ET.tostring(comparison) == ET.tostring(verified),
            "Texture, blend, alpha or parameter data unexpectedly changed")
    output = encode_xml_ymat(corrected, len(slots))
    return dict(project=project, relative=str(ymat.relative_to(REPO)),
                path=ymat, model_sha256=model_sha,
                original_sha256=hashlib.sha256(old).hexdigest(),
                result_sha256=hashlib.sha256(output).hexdigest(),
                names=expected_names, count=len(slots),
                glass=glass, changed=True, output=output, original=old)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="Apply only after full source/target validation")
    parser.add_argument("--project", choices=["all", *PROJECTS], default="all")
    args = parser.parse_args()
    selected = PROJECTS if args.project == "all" else (args.project,)
    jobs = []
    for project in selected:
        folder = REPO / "Projects" / project / MATERIALS
        require(folder.is_dir(), "Missing project materials: " + str(folder))
        material_files = sorted(folder.glob("*.ymat"))
        require(material_files, "No YMAT files to validate: " + str(folder))
        jobs.extend(prepare(project, material) for material in material_files)
    count = sum(j["changed"] for j in jobs)
    print(f"MT28 verified: files={len(jobs)} identity_changes={count} "
          f"model_slots={sum(j['count'] for j in jobs)} "
          f"glass_slots={sum(j['glass'] for j in jobs)}")
    for job in jobs:
        if job["changed"]:
            print("RESTORE", job["relative"], "entries", job["count"], "glass", job["glass"])
    if not args.apply or not count:
        print("READ-ONLY CHECK" if not args.apply else "ALREADY REPAIRED")
        return 0
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    backup_root = REPO / ".northstarcpp" / "backups" / ("mt28-names-" + stamp)
    backup_root.mkdir(parents=True, exist_ok=False)
    changed = [j for j in jobs if j["changed"]]
    evidence = dict(schema=1, source="NVRM material_slots[] in original matching YDR",
                    created_utc=stamp, files=[], stage="backed_up")
    try:
        for job in changed:
            require(hashlib.sha256(job["path"].read_bytes()).hexdigest() ==
                    job["original_sha256"],
                    "Source material changed since preflight: " + job["relative"])
            dest = backup_root / job["relative"]
            dest.parent.mkdir(parents=True, exist_ok=True)
            require(not dest.exists(), "Backup collision: " + str(dest))
            with dest.open("xb") as f:
                f.write(job["original"])
            require(hashlib.sha256(dest.read_bytes()).hexdigest() ==
                    job["original_sha256"], "Backup integrity failure: " + str(dest))
            evidence["files"].append({k: job[k] for k in
                ("project", "relative", "model_sha256", "original_sha256",
                 "result_sha256", "count", "glass")})
        manifest = backup_root / "manifest.json"
        manifest.write_text(json.dumps(evidence, ensure_ascii=False, indent=2)+"\n",
                            encoding="utf-8")
        converted = []
        try:
            for job in changed:
                require(hashlib.sha256(job["path"].read_bytes()).hexdigest() ==
                        job["original_sha256"],
                        "Concurrent source modification: " + job["relative"])
                temp = job["path"].with_name(job["path"].name +
                                             ".tmp-" + uuid.uuid4().hex)
                try:
                    with temp.open("xb") as f:
                        f.write(job["output"])
                    require(hashlib.sha256(temp.read_bytes()).hexdigest() ==
                            job["result_sha256"], "Staged YMAT verification failed")
                    os.replace(temp, job["path"])
                finally:
                    if temp.exists():
                        temp.unlink()
                converted.append(job)
            for job in converted:
                require(hashlib.sha256(job["path"].read_bytes()).hexdigest() ==
                        job["result_sha256"],
                        "Post-write YMAT hash mismatch: " + job["relative"])
            evidence["stage"] = "verified"
            manifest.write_text(json.dumps(evidence, ensure_ascii=False, indent=2)+"\n",
                                encoding="utf-8")
            print("RESTORED",len(converted),"files; canonical NEF8 XML material libraries")
            print("BACKUP",backup_root)
        except Exception:
            for job in reversed(converted):
                current = hashlib.sha256(job["path"].read_bytes()).hexdigest()
                if current == job["result_sha256"]:
                    backup_path = backup_root / job["relative"]
                    tmp = job["path"].with_name(job["path"].name +
                                                ".rollback-" + uuid.uuid4().hex)
                    shutil.copyfile(backup_path, tmp)
                    os.replace(tmp, job["path"])
            raise
    except Exception:
        print("REPAIR FAILED: see backups in " + str(backup_root),file=sys.stderr)
        raise
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RepairError, OSError, ValueError, ET.ParseError) as exc:
        print("MT28 repair preflight failed: " + str(exc), file=sys.stderr)
        raise SystemExit(1)
