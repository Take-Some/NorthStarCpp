#!/usr/bin/env python3
"""Read The Last of Us Part II Remastered PC Naughty Dog PAK without ResHax.

Implements the original, local PAK page tables, typed resource tables and
pointer fixups. Supports introspection and skeleton reconstruction into
NorthStar's authentic NEF8 YMT v2.6 container.

Limits: the secondary helper-rig bind data is only partially represented in
the source JOINT_HIERARCHY transform table; generated helper bones without
authored bind transforms are marked as derived in the sidecar report. This
tool does NOT claim to export fully skinned geometry, materials or YCD clips.
No third-party executables are invoked.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import math
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
from blake3 import blake3

DEFAULT_SOURCE = (Path(__file__).resolve().parents[1] /
    "engine/northstarcpp/resources/sharedAssets/characters/ellie/source/actor97")
DEFAULT_YMT = (Path(__file__).resolve().parents[1] /
    "engine/northstarcpp/resources/sharedAssets/characters/ellie/models/ellie.ymt")


class PakError(ValueError):
    pass


class NaughtyDogPak:
    def __init__(self, path: Path):
        self.path = path.resolve(strict=True)
        self.data = self.path.read_bytes()
        if len(self.data) < 128 or self.u32(0) not in (2681, 2685):
            raise PakError("Unrecognized Naughty Dog PC PAK header")
        count = self.u32(16)
        entry_table = self.u32(20)
        if not 1 <= count <= 4096 or entry_table + count * 12 > len(self.data):
            raise PakError("Invalid PAK page table")
        self.pages = []
        for i in range(count):
            start, length, kind = self.unpack("<III", entry_table + i * 12)
            if length < 20 or start + length > len(self.data):
                raise PakError(f"Page {i} lies outside the PAK")
            if self.data[start:start + 8] != bytes.fromhex("efbeaddeefbeadde"):
                raise PakError(f"Page {i} has unexpected marker")
            self.pages.append((start, length, kind))
        fix_table = self.u32(28)
        if fix_table + 12 > len(self.data):
            raise PakError("Invalid pointer fixup table")
        fix_start, fix_count = self.unpack("<II", fix_table + 4)
        if fix_count > 2000000 or fix_start + fix_count * 8 > len(self.data):
            raise PakError("Invalid pointer fixup count/extent")
        self.fixups = {}
        for i in range(fix_count):
            from_pg, to_pg, rel = self.unpack("<HHI", fix_start + i * 8)
            if from_pg >= count or to_pg >= count:
                raise PakError("Invalid pointer fixup page id")
            address = self.pages[from_pg][0] + rel
            if address + 8 > len(self.data):
                raise PakError("Pointer fixup address outside PAK")
            self.fixups[address] = to_pg
        self.entries = []
        for pg, (start, length, kind) in enumerate(self.pages):
            num = self.unpack("<H", start + 18)[0]
            if num > 16384 or start + 20 + num * 16 > start + length:
                raise PakError(f"Invalid typed entries in page {pg}")
            for j in range(num):
                row = start + 20 + j * 16
                offset = self.u32(row + 8)
                item = start + offset
                if item + 16 > start + length:
                    raise PakError("Resource item exceeds page")
                name = self.ascii(start + self.u64(item))
                kind_name = self.ascii(start + self.u64(item + 8))
                self.entries.append({"page": pg, "offset": item,
                                     "type": kind_name, "name": name})

    def require(self, offset: int, size: int) -> None:
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise PakError(f"PAK byte range out of bounds: {offset:#x}+{size}")

    def unpack(self, fmt: str, offset: int):
        self.require(offset, struct.calcsize(fmt))
        return struct.unpack_from(fmt, self.data, offset)

    def u32(self, offset: int) -> int:
        return self.unpack("<I", offset)[0]

    def u64(self, offset: int) -> int:
        return self.unpack("<Q", offset)[0]

    def ascii(self, offset: int):
        self.require(offset, 1)
        end = self.data.find(b"\0", offset, min(len(self.data), offset + 4096))
        if end == -1:
            raise PakError("Missing string terminator in PAK")
        return self.data[offset:end].decode("utf-8", errors="replace")

    def pointer(self, addr: int) -> int:
        if addr not in self.fixups:
            raise PakError(f"Unresolved PAK pointer fixup at {addr:#x}")
        rel = self.u64(addr)
        dest = self.pages[self.fixups[addr]][0] + rel
        self.require(dest, 1)
        return dest

    def typed(self, kind: str):
        return [entry for entry in self.entries if entry["type"] == kind]

    def summary(self):
        return {
            "file": str(self.path),
            "sha256": hashlib.sha256(self.data).hexdigest(),
            "bytes": len(self.data),
            "pak_version_magic": self.u32(0),
            "page_count": len(self.pages),
            "fixup_count": len(self.fixups),
            "typed_resource_count": len(self.entries),
            "typed_resource_distribution": dict(
                sorted(collections.Counter(e["type"] for e in self.entries).items())),
            "geometry_records": self.typed("GEOMETRY_1"),
            "joint_hierarchies": self.typed("JOINT_HIERARCHY"),
        }

    def skeleton(self):
        hierarchies = self.typed("JOINT_HIERARCHY")
        if len(hierarchies) != 1:
            raise PakError("Expected precisely one JOINT_HIERARCHY")
        joint = hierarchies[0]["offset"]
        # Naughty Dog TLOU2 resource: 48-byte Item header + 20 bytes metadata
        joint_count = self.u32(joint + 68)
        if joint_count < 1 or joint_count > 4096:
            raise PakError(f"Unreasonable joint count: {joint_count}")
        xforms = self.pointer(joint + 80)
        names = self.pointer(joint + 104)
        count_main, xform_count = self.unpack("<HH", xforms + 16)
        header_size = self.u32(xforms + 32)
        hierarchy_offset = self.u32(xforms + 60)
        if count_main != joint_count or xform_count < 1 or xform_count > joint_count:
            raise PakError("Inconsistent node or transform count")
        if header_size < 64 or header_size > 256*1024:
            raise PakError("Invalid hierarchy transform header")
        transform_start = xforms + header_size
        hierarchy_head = xforms + hierarchy_offset + 20
        hashes_size = self.u32(hierarchy_head)
        parent_start = hierarchy_head + hashes_size - 20
        self.require(parent_start, joint_count * 16)
        self.require(transform_start, xform_count * 48)
        self.require(names, joint_count * 16)
        bone_names = []
        parents = []
        chains = []
        for i in range(joint_count):
            name_ref = self.u64(names + i * 16 + 8)
            name = self.ascii(self.pages[hierarchies[0]["page"]][0] + name_ref)
            if not name or len(name) > 256 or re.search(r"[@/\\:]", name):
                raise PakError(f"Invalid bone name {name!r}")
            _, parent, _, chain = self.unpack("<4i", parent_start + i*16)
            if parent < -1 or parent >= i:
                raise PakError(f"Unexpected parent index at joint {i}: {parent}")
            bone_names.append(name)
            parents.append(parent)
            chains.append(chain)
        if len(set(bone_names)) != joint_count:
            raise PakError("Duplicate bone names")

        def root_for(idx):
            while parents[idx] != -1:
                idx = parents[idx]
            return idx
        main = [i for i in range(joint_count) if root_for(i) == 0]
        if len(main) != xform_count:
            raise PakError(f"Primary bone count {len(main)} != authored transforms {xform_count}")
        bind = {}
        for slot, joint_index in enumerate(main):
            data = self.unpack("<12f", transform_start + 48*slot)
            scale = data[:3]
            quat = data[4:8]
            trans = data[8:11]
            if any(not math.isfinite(v) for v in scale + quat + trans):
                raise PakError(f"Invalid authored transform on bone {joint_index}")
            if any(abs(v) < 1e-8 for v in scale):
                raise PakError("Zero scale in primary bind transform")
            bind[joint_index] = (trans, quat, scale)
        identity = ((0., 0., 0.), (0., 0., 0., 1.), (1., 1., 1.))
        by_name = {name: i for i, name in enumerate(bone_names)}
        inferred = collections.Counter()
        for i in range(joint_count):
            if i in bind:
                continue
            name = bone_names[i]
            alias = name[:-7] if name.endswith("_helper") else None
            if alias is None and name == "root_helper":
                alias = "root"
            if alias is not None and alias in by_name and by_name[alias] in bind:
                bind[i] = bind[by_name[alias]]
                inferred["matched_primary_alias"] += 1
            elif chains[i] in bind:
                bind[i] = bind[chains[i]]
                inferred["parent_chain_transform"] += 1
            else:
                bind[i] = identity
                inferred["unresolved_identity"] += 1
        return {
            "source": str(self.path), "source_sha256": hashlib.sha256(self.data).hexdigest(),
            "joint_count": joint_count, "authored_transform_count": xform_count,
            "primary_joint_count": len(main),
            "reconstructed_helper_transforms": dict(inferred),
            "names": bone_names, "parents": parents, "bind": bind,
        }


def ymt_xml(rig) -> bytes:
    doc = ET.Element("Metadata")
    sk = ET.SubElement(doc, "Skeleton", {
        "source_format": "northstar.pc.joint_hierarchy.v1",
        "name": "skel.ellie-skel",
    })
    for i, (name, parent) in enumerate(zip(rig["names"], rig["parents"])):
        t, q, s = rig["bind"][i]
        attrs = {
            "index": str(i), "tag": str(i), "name": name,
            "parent_index": str(parent),
        }
        if parent >= 0:
            attrs["parent"] = rig["names"][parent]
        attrs.update({k: f"{v:.9f}" for k, v in zip(
            ("tx","ty","tz","qx","qy","qz","qw","sx","sy","sz"),
            (*t,*q,*s))})
        ET.SubElement(sk, "Joint", attrs)
    return ET.tostring(doc, encoding="utf-8", xml_declaration=True)


def pack_ymt(data: bytes, logical_path: str) -> bytes:
    if not data or len(data) >= 64*1024*1024:
        raise PakError("Invalid native skeleton XML size")
    info = {
        "schema": "newengine.asset.list_file.header_metadata",
        "logical_path": logical_path,
        "content_kind": "opaque:10",
        "authored_by": "NorthStarCpp native PAK reader",
        "source": "common.psarc/actor97/ellie-skel.pak",
        "build_profile": "native-reconstructed-skeleton",
        "entries": [],
        "dependencies": [],
        "warnings": ["Secondary helper bind transforms are reconstructed and require QA against PC Remastered"],
    }
    metadata = json.dumps(info, separators=(",",":"),ensure_ascii=False).encode("utf-8")
    encoder = zlib.compressobj(level=9, wbits=-15)
    zipped = encoder.compress(data) + encoder.flush()
    hdr = bytearray(64)
    hdr[:4] = b"NEF8"
    struct.pack_into("<BBHHHIQQ",hdr,4,2,6,10,7,2,1,len(zipped),len(data))
    hdr[32:64] = blake3(data).digest()
    return bytes(hdr) + metadata + zipped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_SOURCE/"ellie-skel.pak")
    parser.add_argument("--output", type=Path, default=DEFAULT_YMT)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--extract-skeleton", action="store_true")
    parser.add_argument("--inspect", action="store_true")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    try:
        pak = NaughtyDogPak(args.input)
        summary = pak.summary()
        if args.extract_skeleton:
            rig = pak.skeleton()
            payload = ymt_xml(rig)
            # Independent roundtrip checks before writing anything.
            xml = ET.fromstring(payload)
            xml_bones = xml.find("Skeleton").findall("Joint")
            if len(xml_bones) != rig["joint_count"]:
                raise PakError("Skeleton XML roundtrip failed")
            ymt = pack_ymt(payload, "characters/ellie/models/ellie.ymt")
            decoded = zlib.decompress(ymt[len(ymt) - struct.unpack_from("<Q",ymt,16)[0]:], -15)
            if decoded != payload or blake3(decoded).digest() != ymt[32:64]:
                raise PakError("NEF8 compressed payload failed validation")
            if args.output.exists() and not args.force:
                raise FileExistsError(f"Refusing to overwrite {args.output}, use --force")
            args.output.parent.mkdir(parents=True, exist_ok=True)
            temp = args.output.with_suffix(".ymt.tmp")
            temp.write_bytes(ymt)
            temp.replace(args.output)
            summary.update({
                "native_ymt": str(args.output),
                "native_ymt_sha256": hashlib.sha256(ymt).hexdigest(),
                "joint_count": rig["joint_count"],
                "authored_transform_count": rig["authored_transform_count"],
                "primary_joint_count": rig["primary_joint_count"],
                "reconstructed_helpers": rig["reconstructed_helper_transforms"],
                "status": "partial_skeleton_requires_secondary_bind_QA",
            })
        elif args.inspect:
            summary["status"] = "metadata_inspection_only"
        else:
            parser.error("Specify --inspect or --extract-skeleton")
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(summary,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
        print(json.dumps(summary,indent=2,ensure_ascii=False))
        return 0
    except (OSError, ValueError, struct.error, ET.ParseError) as exc:
        print(f"NATIVE PAK FAILURE: {exc}",file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
