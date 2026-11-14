#!/usr/bin/env python3
"""Verify the declared NorthStarCpp source architecture without modifying files.

Usage:
    python tools/verify_architecture.py
    python tools/verify_architecture.py --json

A failed check is a release/build blocker. The catalog and Git metadata are
reviewable source-control records, not automatically generated from disk.
"""
from __future__ import annotations

import argparse
import configparser
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "architecture/module_catalog.json"
NECESSARY = (
    "tools/cmake/NorthStarPaths.cmake",
    "engine/northstarcpp/modules/api/include/northstarcpp/module.h",
    "engine/northstarcpp/modules/api/include/northstarcpp/assets_plugin.h",
    "sdk/vendor/include",
    "sdk/vendor/libraries",
    "sdk/vendor/sources/bullet-3.25",
    "sdk/vendor/sources/blake3-1.8.7",
    "sdk/vendor/sources/libvorbis/CMakeLists.txt",
    "sdk/vendor/sources/libvorbis/COPYING",
    "sdk/vendor/sources/JoltPhysics",
    "sdk/vendor/flecs",
    "sdk/vendor/bcdec",
    "engine/northstarcpp/modules/api/include/northstarcpp/module.h",
    "engine/northstarcpp/modules/api/include/northstarcpp/scene.hpp",
    "engine/northstarcpp/modules/core/northstarcpp",
    "engine/northstarcpp/modules/host",
    "engine/northstarcpp/modules/editor",
    "engine/northstarcpp/resources/icons/logoStar.ico",
    "engine/northstarcpp/resources/icons/folder.png",
    "tools/cmake/NorthStarModules.cmake",
)
FORBIDDEN = (
    "external", "include", "icons",
    "engine/northstarcpp/core",
    "engine/northstarcpp/apps",
    "engine/northstarcpp/plugins",
    "engine/northstarcpp/renderer",
)
OLD_REFERENCES = (
    "external/sdk/",
    "external/flecs",
    "external/third_party/",
    "PROJECT_SOURCE_DIR}/include",
    "PROJECT_SOURCE_DIR}/icons",
    "NORTHSTARCPP_ENGINE_ROOT}/include",
    "NORTHSTARCPP_SOURCE_ROOT}/include",
)
ACTIVE_CONFIGURATION = (
    ROOT / "CMakeLists.txt",
    ROOT / "engine/northstarcpp/CMakeLists.txt",
    ROOT / "tools/cmake/NorthStarModules.cmake",
    ROOT / "tools/cmake/NorthStarPaths.cmake",
    ROOT / "tools/v8_provider/CMakeLists.txt",
    ROOT / "tools/v8_provider_sidecar/CMakeLists.txt",
    ROOT / "tools/build_dist.py",
    ROOT / "tools/update_project_headers.py",
)
VALID_KINDS = {"engine", "application", "tool", "provider", "optional_provider"}


def check() -> dict:
    errors: list[str] = []
    data = json.loads(CATALOG.read_text(encoding="utf-8"))
    if data.get("schema") != 1:
        errors.append("Module catalog schema must be 1")
    for name in NECESSARY:
        if not (ROOT / name).exists():
            errors.append("Missing owned path: " + name)
    for name in FORBIDDEN:
        if (ROOT / name).exists():
            errors.append("Legacy/ambiguous root is forbidden: " + name)
    # Owned public API and engine sources have one source root.
    # sdk/include is reserved for installed package artifacts.
    for obsolete in ("sdk/include", "engine/northstarcpp/modules/foundation"):
        if (ROOT / obsolete).exists():
            errors.append("First-party source tree outside native modules: " + obsolete)
    ids: set[str] = set()
    targets: set[str] = set()
    cmake_targets = (
        (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        + (ROOT / "engine/northstarcpp/CMakeLists.txt").read_text(encoding="utf-8")
        + (ROOT / "tools/v8_provider_sidecar/CMakeLists.txt").read_text(encoding="utf-8")
    )
    for entry in data["modules"]:
        label = entry["id"]
        if label in ids:
            errors.append("Duplicate module id: " + label)
        ids.add(label)
        if entry.get("kind") not in VALID_KINDS:
            errors.append("Unknown module kind: " + label)
        source = ROOT / data["source_root"] / entry["directory"]
        if not source.is_dir():
            errors.append("Module is not in its registered directory: " + str(source))
        if entry["kind"] in {"provider", "optional_provider"} and not entry.get("capability"):
            errors.append("Provider has no capability: " + label)
        if "contract" in entry and not (ROOT / data["public_sdk"] / entry["contract"]).is_file():
            errors.append("Public contract missing for " + label)
        for target in entry["targets"]:
            if target in targets:
                errors.append("Duplicate CMake target owner: " + target)
            targets.add(target)
            if target not in cmake_targets:
                errors.append("Module target not registered in CMake: " + target)
    for key, rel in data["ownership_rules"].items():
        if not rel or Path(rel).is_absolute() or ".." in Path(rel).parts:
            errors.append("Invalid ownership path: " + key)
    for file in ACTIVE_CONFIGURATION:
        if not file.is_file():
            errors.append("Missing build configuration: " + str(file.relative_to(ROOT)))
            continue
        text = file.read_text(encoding="utf-8", errors="replace")
        for bad in OLD_REFERENCES:
            if bad in text:
                errors.append("Stale source layout reference: " +
                              file.relative_to(ROOT).as_posix() + ": " + bad)
    modules = configparser.ConfigParser()
    modules.read(ROOT / ".gitmodules", encoding="utf-8")
    declared = {}
    for section in modules.sections():
        if section.startswith('submodule "'):
            declared[modules.get(section, "path", fallback="")] = modules.get(
                section, "url", fallback="")
    for submodule in data["submodules"]:
        name = submodule["path"]
        if declared.get(name) != submodule["url"]:
            errors.append("Submodule path/URL differs from catalog: " + name)
        if not (ROOT / name).is_dir():
            errors.append("Missing submodule source: " + name)
    if len(declared) != len(data["submodules"]):
        errors.append("Uncataloged Git submodule")
    return {
        "result": "PASS" if not errors else "FAIL",
        "modules": len(ids),
        "targets": len(targets),
        "submodules": len(data["submodules"]),
        "owned_roots": len(data["ownership_rules"]),
        "errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    report = check()
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        print("NorthStarCpp architecture:", report["result"])
        print("Modules:", report["modules"], "Targets:", report["targets"],
              "Git submodules:", report["submodules"])
        for issue in report["errors"]:
            print("ERROR:", issue, file=sys.stderr)
    return 0 if report["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
