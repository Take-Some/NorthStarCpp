#!/usr/bin/env python3
"""Verify two independent V8 vendor artifacts in NorthStarCpp's canonical SDK.

A downloadable third-party binary V8 15.7 SDK is NOT the source-built 15.8 SDK.
Checks content hashes, version tags, GN ABI flags, and vendor-root placement.
This verifier does not approve a binary for production.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

REPOSITORY = Path(__file__).resolve().parents[2]
VENDOR = REPOSITORY / "sdk/vendor"
DEFAULT_BINARY = VENDOR / "v8/15.7.112.23-7-windows-x64-msvc"
DEFAULT_SOURCE = VENDOR / "sources/v8-15.8.62.38-19c06df2"


def sha256(file: Path) -> str:
    with file.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def guarded_path(file: Path) -> Path:
    file = file.resolve(strict=True)
    if not file.is_relative_to(VENDOR.resolve(strict=True)):
        raise ValueError("V8 dependencies must live under sdk/vendor")
    return file


def check_binary(root: Path) -> tuple[str, int]:
    root = guarded_path(root)
    meta = json.loads((root / "vendor-sdk.json").read_text("utf-8"))
    if (meta.get("schema") != 1 or
            meta.get("kind") != "third-party-prebuilt-v8-sdk" or
            meta.get("version") != "15.7.112.23" or
            meta.get("platform") != "windows-x64-msvc-clang-cl-coff" or
            meta.get("release_approved") is not False):
        raise ValueError("Unexpected version or release status of vendor V8 SDK")
    flags = meta.get("gn_flags", {})
    required_flags = {
        "v8_monolithic": "true",
        "is_component_build": "false",
        "v8_enable_pointer_compression": "false",
        "v8_enable_sandbox": "false",
        "v8_enable_i18n_support": "false",
        "target_cpu": '"x64"',
        "use_custom_libcxx": "false",
    }
    for flag, value in required_flags.items():
        if flags.get(flag) != value:
            raise ValueError("Unexpected V8 build flag: " + flag)
    required = {"include/v8.h", "include/v8-version.h",
                "include/libplatform/libplatform.h",
                "lib/v8_monolith.lib", "build-config/args.gn", "LICENSE"}
    expected = meta.get("files", {})
    if not isinstance(expected, dict) or not required.issubset(expected):
        raise ValueError("Incomplete V8 15.7 prebuilt SDK")
    for rel, record in expected.items():
        rel_path = Path(rel)
        if rel_path.is_absolute() or ".." in rel_path.parts:
            raise ValueError("Unsafe vendor SDK manifest entry")
        file = root / rel_path
        if (not file.is_file() or file.is_symlink() or
                file.stat().st_size != record.get("size") or
                sha256(file) != record.get("sha256")):
            raise ValueError("Changed SDK file: " + rel)
    lib = root / "lib/v8_monolith.lib"
    if lib.stat().st_size < 1024 * 1024:
        raise ValueError("Missing V8 C++ monolithic static library")
    with lib.open("rb") as stream:
        if stream.read(8) != b"!<arch>\n":
            raise ValueError("V8 binary does not have an archive signature")
    text = (root / "include/v8-version.h").read_text("utf-8")
    for macro, value in zip(
        ("V8_MAJOR_VERSION", "V8_MINOR_VERSION",
         "V8_BUILD_NUMBER", "V8_PATCH_LEVEL"),
        ("15", "7", "112", "23"),
    ):
        if not re.search(r"(?m)^#define\s+" + macro + r"\s+" + value + r"\s*$", text):
            raise ValueError("V8 headers do not match prebuilt library release")
    return meta["version"], len(expected)


def check_source(root: Path) -> tuple[str, str]:
    root = guarded_path(root)
    meta = json.loads((root / "SOURCE-PROVENANCE.json").read_text("utf-8"))
    revision = "19c06df29e0050413f5f041888f4c990de970314"
    if (meta.get("kind") != "v8-upstream-source-snapshot" or
        meta.get("source_commit") != revision or
        meta.get("detected_version") != "15.8.62.38" or
        meta.get("compiled_sdk") is not False or
        meta.get("release_approved") is not False or
        not (root / "include/v8.h").is_file() or
        not (root / "DEPS").is_file() or
        not (root / "LICENSE").is_file()):
        raise ValueError("Incorrect V8 15.8 source snapshot/provenance")
    return meta["detected_version"], revision


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    args = parser.parse_args()
    version, count = check_binary(args.binary)
    source_version, commit = check_source(args.source)
    print(f"V8 vendor binary: {version}, {count} hash-verified files: PASS")
    print(f"V8 upstream source: {source_version}, {commit}: PASS")
    print("Native V8 15.8 SDK compiled: NO")
    print("Release approved: NO")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, json.JSONDecodeError) as exc:
        print("V8 vendor verification FAILED:", exc, file=sys.stderr)
        raise SystemExit(2)
