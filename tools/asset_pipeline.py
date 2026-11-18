#!/usr/bin/env python3
"""NorthStar asset identity, dependency graph, and reproducible cook manifest.

This is an authoring/CI pipeline. It does not replace AssetStore or decode
runtime payloads; it preflights source assets before packaging. State, DDC and
outputs MUST live outside the scanned assets directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import uuid
import xml.etree.ElementTree as ET
from collections import defaultdict

from pack_nsp import EXCLUDE_NAMES, MAX_FILE, safe_path

SCHEMA = "northstar.asset_registry.v1"
COOK_VERSION = 1
TEXT_EXT = {".xml", ".json", ".js", ".mjs", ".ts", ".ymat",
            ".hlsl", ".glsl", ".vert", ".frag", ".comp", ".wgsl", ".inc"}
REFERENCE_RE = re.compile(r"""["']([^"'\r\n]{1,512})["']""")
ANONYMOUS = re.compile(r"mat_[0-9]+$")
MATERIAL_REF = re.compile(r"^[^\s]+\.ymat@[^@\s]+$", re.IGNORECASE)


class PipelineError(ValueError):
    pass


def canonical(data: object) -> bytes:
    return (json.dumps(data, sort_keys=True, ensure_ascii=False,
                       separators=(",", ":")) + "\n").encode("utf-8")


def atomic_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(canonical(data))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def digest_file(path: Path) -> tuple[str, int]:
    sha = hashlib.sha256()
    size = 0
    with path.open("rb") as src:
        for chunk in iter(lambda: src.read(1024 * 1024), b""):
            sha.update(chunk)
            size += len(chunk)
            if size > MAX_FILE:
                raise PipelineError(f"Asset exceeds 256 MiB: {path}")
    return sha.hexdigest(), size


def scan_paths(root: Path) -> dict[str, Path]:
    if not root.is_dir():
        raise PipelineError(f"Asset directory missing: {root}")
    paths: dict[str, Path] = {}
    lower: dict[str, str] = {}
    for path in root.rglob("*"):
        rel = path.relative_to(root)
        if any(part in EXCLUDE_NAMES for part in rel.parts):
            continue
        # Mirror pack_nsp inventory: raw character/sky source research is
        # excluded from both the published registry and runtime archives.
        if ((len(rel.parts) >= 3 and rel.parts[0] == "characters" and rel.parts[2] == "source")
                or (len(rel.parts) >= 3 and rel.parts[:2] == ("textures", "sky")
                    and rel.parts[2] == "source")):
            continue
        if path.is_symlink():
            raise PipelineError(f"Symlink is not a safe runtime source: {rel}")
        if not path.is_file() or ".bak" in path.name.lower() or path.name.endswith(("~", ".tmp")):
            continue
        name = rel.as_posix()
        safe_path(name)
        folded = name.casefold()
        if folded in lower:
            raise PipelineError(f"Case-insensitive collision: {lower[folded]} / {name}")
        lower[folded] = name
        paths[name] = path
    return dict(sorted(paths.items(), key=lambda x: x[0].encode("utf-8")))


def ymat_body(data: bytes, path: Path) -> tuple[bytes, int]:
    if not data.startswith(b"NEF8") or len(data) < 16:
        raise PipelineError(f"YMAT must use NEF8 kind-4 XML: {path}")
    kind = struct.unpack_from("<H", data, 6 if data[4] == 2 else 8)[0]
    if kind != 4:
        raise PipelineError(f"JSON/non-XML YMAT kind {kind} forbidden: {path}")
    from repair_mt28_material_slots import unpack_nef8
    try:
        return unpack_nef8(data, 4)
    except (ValueError, RuntimeError) as exc:
        raise PipelineError(f"Invalid XML YMAT: {path}: {exc}") from exc


def material_names(path: Path) -> list[str]:
    raw, count = ymat_body(path.read_bytes(), path)
    try:
        tree = ET.fromstring(raw)
    except ET.ParseError as exc:
        raise PipelineError(f"Invalid XML YMAT: {path}: {exc}") from exc
    if (tree.tag != "YmatMaterialLibrary"
            or tree.get("version") != "2"
            or tree.get("schema") != "northstarcpp.ymat.material_library.v2"):
        raise PipelineError(f"Expected canonical XML YMAT schema v2: {path}")
    values = [m.get("name") for m in tree.findall("Material")]
    if count != len(values):
        raise PipelineError(f"NEF8/YMAT member count mismatch: {path}")
    if not values or any(not v or ANONYMOUS.fullmatch(v) for v in values):
        raise PipelineError(f"Missing or anonymous YMAT material identity: {path}")
    if len(set(values)) != len(values):
        raise PipelineError(f"Duplicate YMAT material identity: {path}")
    if any("@" in v or "/" in v or chr(92) in v for v in values):
        raise PipelineError(f"Unsafe YMAT member: {path}")
    return values


def extract_references(path: Path, name: str, known: set[str],
                       names: dict[str, list[str]]) -> list[str]:
    if path.suffix.lower() not in TEXT_EXT:
        return []
    if path.stat().st_size > 8 * 1024 * 1024:
        return []
    raw = path.read_bytes()
    if raw.startswith(b"NEF8"):
        if path.suffix.lower() != ".ymat":
            return []  # Other NEF8 formats need their own authoritative decoder.
        raw, _ = ymat_body(raw, path)
    if b"\x00" in raw:
        return []
    value = raw.decode("utf-8", "strict")
    refs = set()
    for match in REFERENCE_RE.finditer(value):
        token = match.group(1).replace("\\/", "/")
        if token in known:
            refs.add(token)
        elif "/" not in token and (name.rpartition("/")[0] + "/" + token).lstrip("/") in known:
            # Shader #include "common.glsl" and other local references.
            refs.add((name.rpartition("/")[0] + "/" + token).lstrip("/"))
        elif MATERIAL_REF.fullmatch(token):
            base = token.split("@", 1)[0]
            if base in names:
                raise PipelineError(f"Missing named YMAT member: {path}: {token}")
    return sorted(refs, key=lambda s: s.encode("utf-8"))


def prepare(root: Path, namespace: str, previous: dict | None = None,
            moves: list[str] | None = None, prune: bool = False,
            target: str = "generic", options: str = "") -> dict:
    if not namespace.strip():
        raise PipelineError("Namespace cannot be empty")
    if previous and (previous.get("schema") != SCHEMA or previous.get("namespace") != namespace):
        raise PipelineError("Registry schema/namespace mismatch")
    paths = scan_paths(root)
    before = dict(previous.get("assets", {})) if previous else {}
    for change in moves or []:
        if "=" not in change:
            raise PipelineError("Move must be OLD=NEW")
        src, dst = change.split("=", 1)
        if src not in before or dst in before or src in paths or dst not in paths:
            raise PipelineError(f"Unsafe or ambiguous identity move: {change}")
        before[dst] = before.pop(src)
    missing = sorted(before.keys() - paths.keys())
    if missing and not prune:
        raise PipelineError("Deleted/renamed assets require --move OLD=NEW or --prune: " +
                            ", ".join(missing[:6]))
    assets: dict[str, dict] = {}
    members: dict[str, list[str]] = {}
    for name, path in paths.items():
        sha, size = digest_file(path)
        prior = before.get(name)
        asset_id = (prior["id"] if prior else str(uuid.uuid5(
            uuid.NAMESPACE_URL, f"northstarcpp/{namespace}/asset/{name}")))
        if name.lower().endswith(".ymat"):
            members[name] = material_names(path)
        # Existing member IDs are preserved by member name, not by position.
        existing = prior.get("members", {}) if prior else {}
        if existing and set(existing) != set(members.get(name, [])):
            raise PipelineError(f"YMAT member identity changed: {name}; migrate explicitly")
        if prior and prior.get("member_order") is not None and prior["member_order"] != members.get(name, []):
            raise PipelineError(f"YMAT member slot order changed: {name}")
        asset_members = {
            value: existing.get(value) or str(uuid.uuid5(uuid.NAMESPACE_URL,
                f"northstarcpp/{namespace}/material/{asset_id}/{value}"))
            for value in members.get(name, [])
        }
        assets[name] = {"id": asset_id, "sha256": sha, "size": size,
                        "members": dict(sorted(asset_members.items())),
                        "member_order": members.get(name, [])}
    known = set(paths)
    known.update(f"{asset}@{member}" for asset, items in members.items() for member in items)
    graph: dict[str, list[str]] = {}
    for name, path in paths.items():
        deps = extract_references(path, name, known, members)
        if name in deps:
            raise PipelineError(f"Self-dependency: {name}")
        graph[name] = deps
    # Hash dependencies in postorder, including member identities and options.
    states: dict[str, str] = {}
    visiting: set[str] = set()

    def cook_key(name: str) -> str:
        base = name.split("@", 1)[0]
        if base in states and name == base:
            return states[base]
        if base in visiting:
            raise PipelineError(f"Asset dependency cycle: {name}")
        if base not in assets:
            raise PipelineError(f"Unknown dependency: {name}")
        visiting.add(base)
        ref_hashes = []
        for ref in graph[base]:
            ref_base = ref.split("@", 1)[0]
            key = cook_key(ref_base)
            ref_hashes.append((ref, key,
                assets[ref_base]["members"].get(ref.split("@", 1)[1], "")
                if "@" in ref else assets[ref_base]["id"]))
        visiting.remove(base)
        record = {"v": COOK_VERSION, "target": target, "options": options,
                  "id": assets[base]["id"], "sha256": assets[base]["sha256"],
                  "members": assets[base]["members"], "dependencies": ref_hashes}
        result = hashlib.sha256(canonical(record)).hexdigest()
        states[base] = result
        return result

    for name in assets:
        assets[name]["dependencies"] = graph[name]
        assets[name]["cook_key"] = cook_key(name)
    return {"schema": SCHEMA, "namespace": namespace,
            "cook_version": COOK_VERSION, "target": target, "options": options,
            "assets": assets}


def validate_inventory(registry: dict, files: list[tuple[str, Path, int, int]]) -> None:
    """Fail closed before NSP publish when source identity or content differs."""
    items = registry.get("assets", {})
    found = {name for name, _, _, _ in files}
    if set(items) != found:
        raise PipelineError("Registry/NSP inventory mismatch: " +
                            str(sorted(set(items) ^ found)[:8]))
    for name, path, size, _ in files:
        item = items[name]
        if size != item["size"] or digest_file(path)[0] != item["sha256"]:
            raise PipelineError(f"Stale asset registry content: {name}")


def verify_registry(root: Path, registry: dict) -> None:
    if registry.get("schema") != SCHEMA:
        raise PipelineError("Unsupported asset registry schema")
    rebuilt = prepare(root, registry["namespace"], registry,
                      target=registry["target"], options=registry["options"])
    if canonical(rebuilt) != canonical(registry):
        raise PipelineError("Asset registry graph, IDs, or cook keys are stale")


def publish_cook(registry: dict, cache: Path, manifest: Path) -> tuple[int, int]:
    if cache.resolve() == manifest.resolve():
        raise PipelineError("Cache root and manifest path cannot match")
    hits = misses = 0
    for name, item in registry["assets"].items():
        key = item["cook_key"]
        file = cache / key[:2] / (key + ".json")
        payload = {"schema": "northstar.ddc_record.v1", "asset": name, **item}
        if file.is_file() and file.read_bytes() == canonical(payload):
            hits += 1
        else:
            atomic_json(file, payload)
            misses += 1
    atomic_json(manifest, registry)
    return hits, misses


def in_root(root: Path, path: Path) -> bool:
    return path.resolve().is_relative_to(root.resolve())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path, help="Assets directory")
    parser.add_argument("--state", required=True, type=Path, help="Persistent identity registry JSON")
    parser.add_argument("--namespace", help="Stable project identity; default is assets parent name")
    parser.add_argument("--move", action="append", default=[], metavar="OLD=NEW")
    parser.add_argument("--prune", action="store_true", help="Confirm intentional asset deletions")
    parser.add_argument("--target", default="generic", help="Cook target/platform")
    parser.add_argument("--options", default="", help="Canonical compiler options/variants")
    parser.add_argument("--apply", action="store_true", help="Atomically publish registry")
    parser.add_argument("--verify", action="store_true", help="Require exact registry match; never write")
    parser.add_argument("--cache", type=Path, help="DDC directory; requires --apply")
    parser.add_argument("--manifest", type=Path, help="Cook manifest; requires --cache")
    args = parser.parse_args()
    if args.apply and args.verify:
        parser.error("--apply and --verify are mutually exclusive")
    if (args.cache or args.manifest) and not (args.apply and args.cache and args.manifest):
        parser.error("--cache and --manifest require each other and --apply")
    for dest in (args.state, args.cache, args.manifest):
        if dest and in_root(args.root, dest):
            raise PipelineError("Registry/cache/manifest must live OUTSIDE assets/")
    prior = json.loads(args.state.read_text("utf-8")) if args.state.exists() else None
    if args.verify and prior is None:
        raise PipelineError("Cannot verify absent registry")
    new = prepare(args.root, args.namespace or args.root.resolve().parent.name, prior,
                  args.move, args.prune, args.target, args.options)
    if args.verify and canonical(prior) != canonical(new):
        raise PipelineError("Registry differs from source assets or cooking options")
    if args.apply:
        atomic_json(args.state, new)
        if args.cache:
            hits, misses = publish_cook(new, args.cache, args.manifest)
            print(f"DDC metadata: {hits} hits, {misses} misses")
    print(f"Asset pipeline {'verified' if args.verify else 'published' if args.apply else 'preflight'}: "
          f"{len(new['assets'])} assets, "
          f"{sum(len(a['dependencies']) for a in new['assets'].values())} direct edges")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (PipelineError, ValueError, OSError, UnicodeError) as exc:
        print(f"Asset pipeline ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
