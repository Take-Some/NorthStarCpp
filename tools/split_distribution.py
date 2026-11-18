#!/usr/bin/env python3
"""Separate a verified NorthStarCpp release into independently shippable roles.

The input is the existing, fully validated QA/release staging distribution.
No build-directory DLL discovery, publication approval bypass, or asset cooking.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

ROLES = ("Engine", "GameClient")
CLIENT_FILES = frozenset({
    "bin/northstarcpp_player.exe", "bin/bugtrap.exe",
    "bin/bootstrap.json", "northstarcpp.plugins.json", "native.runtime.json",
})


class SplitError(RuntimeError):
    pass


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def safe_file(root: Path, relative: str) -> Path:
    part = Path(relative)
    if (not relative or part.is_absolute() or "\\" in relative or
            any(chunk in ("", ".", "..") for chunk in relative.split("/")) or
            part.as_posix() != relative):
        raise SplitError("Unsafe distribution path: " + repr(relative))
    path = root
    for component in part.parts:
        path = path / component
        if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            raise SplitError("Symlink/junction not allowed: " + relative)
    return path


def client_file(relative: str) -> bool:
    if relative in CLIENT_FILES or relative == "v8.runtime.json":
        return True
    if relative.startswith("licenses/") or relative.startswith("attestations/"):
        return True
    if relative.startswith("bin/archive/") and relative.endswith(".nsp"):
        return True
    if relative.startswith("bin/scripting/"):
        return True
    if (relative.startswith("bin/") and relative.count("/") == 1
            and relative.casefold().endswith(".dll")):
        return True
    return False


def expected_files(source_manifest: dict, role: str) -> set[str]:
    if role not in ROLES:
        raise SplitError("Unsupported distribution role: " + role)
    names = set(source_manifest.get("files", {}))
    if role == "Engine":
        return names - {"bin/northstarcpp_player.exe"}
    return {name for name in names if client_file(name)}


def _validate_registry(package: Path, record: dict) -> None:
    registry_path = package / "northstarcpp.plugins.json"
    if not registry_path.is_file():
        raise SplitError("Plugin registry missing")
    registry = json.loads(registry_path.read_text(encoding="utf-8"))
    if (registry.get("engine") != "NorthStarCpp" or
            registry.get("engine_version") != record["version"] or
            registry.get("host_abi") != record["host_abi"] or
            not isinstance(registry.get("plugins"), list)):
        raise SplitError("Invalid or incompatible plugin registry")
    if registry["plugins"] != record.get("plugins"):
        raise SplitError("Distribution/registry plugin list mismatch")
    actual = set()
    for plugin in registry["plugins"]:
        if not isinstance(plugin, dict) or plugin.get("host_abi") != record["host_abi"]:
            raise SplitError("Invalid plugin ABI")
        if plugin.get("kind", "external") == "external":
            path = plugin.get("path")
            if (not isinstance(path, str) or not path.startswith("bin/") or
                    path.count("/") != 1 or not path.endswith(".dll")):
                raise SplitError("Invalid external plugin path")
            if not safe_file(package, path).is_file():
                raise SplitError("Missing plugin: " + path)
            actual.add(path.lower())
    # Check for stray root-bin DLLs. They must be declared providers or
    # hash-recorded native import dependencies; V8/Qt are never guessed.
    from tools.build_dist import verified_native_runtime
    from tools.v8_provider.registry import registered_v8_runtime
    native = verified_native_runtime(package)
    v8 = {name.casefold() for name in registered_v8_runtime(package)}
    for file in (package / "bin").glob("*.dll"):
        relative = "bin/" + file.name.lower()
        if (relative not in actual and relative not in v8
                and file.name.lower() not in native):
            raise SplitError("Unregistered root runtime DLL: " + file.name)


def verify_component(package: Path, role: str | None = None) -> dict:
    if not package.is_dir() or package.is_symlink():
        raise SplitError("Component directory is missing or redirected")
    manifest = package / "manifest.json"
    if not manifest.is_file() or manifest.is_symlink():
        raise SplitError("Component manifest missing")
    record = json.loads(manifest.read_text(encoding="utf-8"))
    actual_role = record.get("distribution_role")
    if (record.get("engine") != "NorthStarCpp" or
            record.get("schema_version") != 2 or actual_role not in ROLES or
            (role is not None and role != actual_role) or
            not isinstance(record.get("files"), dict)):
        raise SplitError("Invalid role-specific distribution manifest")
    paths = {}
    for path in package.rglob("*"):
        if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            raise SplitError("Redirected component content")
        if path.is_file() and path != manifest:
            paths[path.relative_to(package).as_posix()] = path
    expected = record["files"]
    if set(paths) != set(expected):
        raise SplitError("Component file set mismatch")
    for name, metadata in expected.items():
        file = safe_file(package, name)
        if (not isinstance(metadata, dict) or not file.is_file() or
                file.stat().st_size != metadata.get("size") or
                digest(file) != metadata.get("sha256")):
            raise SplitError("Component checksum mismatch: " + name)
    if actual_role == "Engine":
        if "bin/northstarcpp_player.exe" in paths:
            raise SplitError("Player leaked into the Engine package")
        for required in ("bin/northstarcpp_editor_qt.exe", "sdk/include/northstarcpp/math.hpp"):
            if required not in paths:
                raise SplitError("Engine lacks a development prerequisite: " + required)
    else:
        if not CLIENT_FILES.issubset(paths):
            raise SplitError("Game Client lacks a required runtime file")
        if any(not client_file(p) for p in paths):
            raise SplitError("Engine editor/SDK content leaked into Game Client")
        if not any(p.startswith("bin/archive/") and p.endswith(".nsp") for p in paths):
            raise SplitError("Game Client lacks engine resource packs")
        for entry, dependencies in record.get("imports", {}).items():
            if entry not in paths:
                raise SplitError("Import metadata references unshipped binary: " + entry)
            for dep in dependencies:
                if dep.get("status") == "bundled":
                    if ("bin/" + dep["name"]) not in paths:
                        raise SplitError("Unresolved client runtime import: " + dep["name"])
    _validate_registry(package, record)
    return record


def assemble(source: Path, destination: Path, *, approved: bool,
             zip_packages: bool = True, development: bool = False) -> dict:
    from tools.build_dist import verify_manifest
    source = source.resolve(strict=True)
    original = verify_manifest(source)
    if approved and development:
        raise SplitError("Development packages cannot be release-approved")
    if not original.get("installed_runtime_smoke"):
        raise SplitError("Source distribution has not passed installed-runtime smoke")
    if not original.get("tests_passed") and not development:
        raise SplitError("QA distribution must pass CTest before packaging")
    if approved and not original.get("installed_sdk_consumer_smoke"):
        raise SplitError("Source SDK smoke not complete")
    destination = destination.resolve()
    if destination.exists():
        raise SplitError("Refusing to overwrite component output: " + str(destination))
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".northstar-split-", dir=destination.parent) as tmp:
        staging = Path(tmp) / destination.name
        staging.mkdir()
        for role in ROLES:
            output = staging / role
            output.mkdir()
            selected = expected_files(original, role)
            for name in sorted(selected):
                src = safe_file(source, name)
                if not src.is_file():
                    raise SplitError("Missing verified source: " + name)
                target = safe_file(output, name)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, target)
                metadata = original["files"][name]
                if target.stat().st_size != metadata["size"] or digest(target) != metadata["sha256"]:
                    raise SplitError("Source/copy digest mismatch: " + name)
            record = {
                "schema_version": 2, "engine": "NorthStarCpp",
                "version": original["version"], "build_type": original["build_type"],
                "target": original["target"], "host_abi": original["host_abi"],
                "distribution_role": role, "release_approved": approved,
                "tests_passed": original["tests_passed"],
                "source_manifest_sha256": digest(source / "manifest.json"),
                "plugins": original["plugins"],
                "imports": {k: v for k, v in original.get("imports", {}).items()
                            if k in selected},
                "files": {k: original["files"][k] for k in sorted(selected)},
            }
            (output / "manifest.json").write_text(
                json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            verify_component(output, role)
            if zip_packages:
                archive_name = f"{role}-{original['version']}-windows-x64-{original['build_type']}.zip"
                archive = staging / archive_name
                with ZipFile(archive, "w", ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
                    for file in sorted(output.rglob("*")):
                        if file.is_file():
                            z.write(file, arcname=role + "/" + file.relative_to(output).as_posix())
                with ZipFile(archive) as z:
                    if z.testzip() is not None:
                        raise SplitError("Corrupt role archive: " + archive_name)
        staging.rename(destination)
    for role in ROLES:
        verify_component(destination / role, role)
    return {
        "component_root": str(destination),
        "engine": str(destination / "Engine"),
        "game_client": str(destination / "GameClient"),
        "release_approved": approved,
        "archives": sorted(str(p) for p in destination.glob("*.zip")),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", type=Path, help="Verify an Engine or GameClient package")
    parser.add_argument("--source", type=Path, help="Verified QA/release monolithic stage")
    parser.add_argument("--output", type=Path, help="New versioned output directory")
    parser.add_argument("--qa-only", action="store_true",
                        help="Create non-shipping QA component candidates")
    parser.add_argument("--no-zip", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.verify:
            record = verify_component(args.verify)
            result = {"verified": True, "role": record["distribution_role"],
                      "files": len(record["files"])}
        elif args.source and args.output:
            if not args.qa_only:
                raise SplitError("Public release splitting requires the signed promote-qa workflow")
            result = assemble(args.source, args.output, approved=False,
                              zip_packages=not args.no_zip)
        else:
            parser.error("Specify --verify or both --source and --output")
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except (SplitError, OSError, ValueError, KeyError) as error:
        print("SPLIT ERROR: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
