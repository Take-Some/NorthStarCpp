#!/usr/bin/env python3
"""Audit a compiled V8 SDK before permitting an ABI 2 native provider build.

A version string, a downloaded d8.exe or a manifest alone is never sufficient:
public C++ headers, the actual monolith library, files and SHA256 must match.
This does not constitute security attestation or clean-machine acceptance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


def digest(file: Path) -> str:
    h = hashlib.sha256()
    with file.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def verify_sdk(folder: Path) -> dict:
    folder = folder.resolve(strict=True)
    manifest_file = folder / "northstar-v8-sdk.json"
    record = json.loads(manifest_file.read_text(encoding="utf-8"))
    if (record.get("schema") != 1 or
        record.get("kind") != "official-source-embeddable-windows-x64-v8-sdk" or
        not re.fullmatch(r"15\.8\.\d+\.\d+", str(record.get("v8_version", ""))) or
        not re.fullmatch(r"[a-f0-9]{40}", str(record.get("source_commit", ""))) or
        record.get("source_branch") != "refs/branch-heads/15.8" or
        record.get("source_repository") != "https://chromium.googlesource.com/v8/v8.git" or
        record.get("release_approved") is not False):
        raise ValueError("Invalid/unverified V8 15.8 SDK provenance")
    args = record.get("gn_args")
    if (not isinstance(args, dict) or args.get("v8_monolithic") is not True or
        args.get("v8_use_external_startup_data") is not False or
        args.get("is_component_build") is not False or
        args.get("use_custom_libcxx") is not False or
        args.get("v8_use_snapshot") is not True or
        args.get("v8_enable_sandbox") is not True or
        args.get("v8_enable_pointer_compression") is not True or
        args.get("target_cpu") != "x64"):
        raise ValueError("V8 SDK GN ABI configuration differs from provider")
    required = {"include/v8.h", "include/v8-version.h",
                "include/libplatform/libplatform.h",
                "lib/v8_monolith.lib", "bin/d8.exe", "LICENSE"}
    files = record.get("files")
    if not isinstance(files, dict) or not required.issubset(files):
        raise ValueError("Not a complete embeddable V8 SDK")
    actual = {file.relative_to(folder).as_posix()
              for file in folder.rglob("*") if file.is_file()} - {"northstar-v8-sdk.json"}
    if actual != set(files):
        raise ValueError("Unlisted or missing SDK files, manifest is incomplete")
    for rel, expected in files.items():
        if not isinstance(rel, str) or len(rel)>320 or "\\" in rel:
            raise ValueError("Unsafe SDK manifest path")
        rel_path = Path(rel)
        if rel_path.is_absolute() or ".." in rel_path.parts:
            raise ValueError("SDK manifest contains traversal")
        file = folder / rel_path
        if (not file.is_file() or file.is_symlink() or
            not isinstance(expected, dict) or
            expected.get("size") != file.stat().st_size or
            expected.get("sha256") != digest(file)):
            raise ValueError("SDK file is missing/changed: " + rel)
    if (folder / "lib/v8_monolith.lib").stat().st_size < 1024*1024:
        raise ValueError("V8 monolith library is not a real linkable archive")
    return record


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    ns = parser.parse_args()
    try:
        data = verify_sdk(ns.sdk)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print("V8 SDK verification FAILED:", error, file=sys.stderr)
        raise SystemExit(2)
    print("Verified local SDK file hashes; V8",data["v8_version"],
          "source:",data["source_commit"])
    print("This is NOT a signed security attestation or release approval")
