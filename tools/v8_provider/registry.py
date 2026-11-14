"""Verified staging of an optional native V8 provider into NorthStarCpp dist.

Only intended for a V8 SDK built with compatible CRT/runtime binaries.
A separate in-process UCRT64 provider can collide with MSVCRT Qt runtime
dependencies; hash conflicts fail closed instead of overwriting existing DLLs.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from typing import Callable

MINIMUM_REVIEWED_V8 = (15, 3, 48)  # CVE-2026-85046 fixed from V8 15.3.48 onward.
RUNTIME_NAME = re.compile(
    r"(?:libv8(?:_[A-Za-z0-9]+)?|libicu[A-Za-z0-9]+|libstdc\+\+-6|"
    r"libgcc_s_seh-1|libwinpthread-1|zlib1)\.dll", re.I)
REQUIRED = {"scripting_v8.dll", "libv8.dll", "libv8_libbase.dll",
            "libv8_libplatform.dll"}


def _hash(file: Path) -> str:
    h = hashlib.sha256()
    with file.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def v8_version(sdk: Path) -> tuple[int, int, int]:
    metadata = sdk / "lib/pkgconfig/v8.pc"
    if not metadata.is_file():
        raise ValueError("V8 SDK pkg-config metadata missing")
    match = re.search(r"^Version:\s*(\d+)\.(\d+)\.(\d+)",
                      metadata.read_text(encoding="utf-8"), re.M)
    if not match:
        raise ValueError("Cannot identify V8 SDK version")
    return tuple(map(int, match.groups()))


def probe_v8_version(sdk: Path) -> tuple[int, int, int]:
    """Verify the SDK's real V8 runtime with its d8 executable."""
    shell = sdk / "bin/d8.exe"
    if not shell.is_file() or shell.is_symlink():
        raise ValueError("V8 version probe requires a real SDK bin/d8.exe")
    try:
        result = subprocess.run(
            [str(shell), "-e", "print(version())"],
            cwd=shell.parent, capture_output=True, text=True, timeout=15,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ValueError("Cannot execute SDK V8 version probe") from exc
    if result.returncode:
        raise ValueError("SDK V8 version probe failed: " + result.stderr[-250:])
    versions = re.findall(r"(\d+)\.(\d+)\.(\d+)(?:\.\d+)?", result.stdout)
    if len(versions) != 1:
        raise ValueError("Ambiguous/invalid SDK d8 version response")
    return tuple(int(x) for x in versions[0])


def registered_v8_runtime(package: Path) -> set[str]:
    """Verify opt-in V8 binary manifest and licensing; return non-plugin DLL names."""
    record_path = package / "v8.runtime.json"
    if not record_path.is_file():
        return set()
    record = json.loads(record_path.read_text(encoding="utf-8"))
    dlls = record.get("dlls")
    if (record.get("schema") != 1 or record.get("runtime") != "V8-UCRT64" or
            not isinstance(dlls, dict) or not dlls):
        raise ValueError("Invalid V8 runtime manifest")
    names = set()
    for rel, expected in dlls.items():
        if (not isinstance(rel, str) or not rel.startswith("bin/") or
                not RUNTIME_NAME.fullmatch(rel[4:]) or
                not isinstance(expected, dict)):
            raise ValueError("Unsafe V8 runtime DLL entry")
        path = package / rel
        if (not path.is_file() or path.is_symlink() or
                path.stat().st_size != expected.get("size") or
                _hash(path) != expected.get("sha256")):
            raise ValueError("Missing/tampered V8 DLL: " + rel)
        names.add(rel)
    for license_name in ("v8/LICENSE", "gcc-libs/COPYING.RUNTIME",
                         "libwinpthread/COPYING", "zlib/LICENSE", "ICU-LICENSE"):
        if not (package / "licenses" / license_name).is_file():
            raise ValueError("Missing V8 dependency license: " + license_name)
    if not (package / "bin/scripting_v8.dll").is_file():
        raise ValueError("Registered V8 provider DLL missing")
    version = record.get("v8_version", "")
    if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Invalid V8 version manifest")
    if tuple(map(int, version.split("."))) < MINIMUM_REVIEWED_V8:
        raise ValueError("V8 version predates the current security release gate")
    return names


def stage_v8_provider(package: Path, provider_bin: Path, sdk: Path, *,
                      version_probe: Callable[[Path], tuple[int, int, int]] = probe_v8_version) -> dict:
    """Opt-in release registration. Rejects unsupported/unpatched V8 versions."""
    package = package.resolve()
    provider_bin = provider_bin.resolve(strict=True)
    sdk = sdk.resolve(strict=True)
    version = v8_version(sdk)
    # The pc file is editable metadata, not authoritative proof of the binary.
    # d8 runs against the SDK's own libv8 and reports its embedded version.
    binary_version = version_probe(sdk)
    if version != binary_version:
        raise ValueError(
            f"V8 SDK metadata {version} disagrees with d8 binary {binary_version}")
    if version < MINIMUM_REVIEWED_V8:
        raise ValueError(
            f"V8 {'.'.join(map(str, version))} is below reviewed minimum "
            f"{'.'.join(map(str, MINIMUM_REVIEWED_V8))}; refusing release")
    license_dir = sdk / "share/licenses"
    for name, required in (("v8", "LICENSE"), ("gcc-libs", "COPYING.RUNTIME"),
                           ("libwinpthread", "COPYING"), ("zlib", "LICENSE")):
        if not (license_dir / name / required).is_file():
            raise ValueError("Required runtime license missing: " + name)
    icu_licenses = sorted((sdk / "share/icu").glob("*/LICENSE"))
    if not icu_licenses:
        raise ValueError("ICU runtime license missing")
    provider = provider_bin / "scripting_v8.dll"
    if not provider.is_file() or provider.is_symlink():
        raise ValueError("Native scripting_v8.dll is missing")
    libs = {f.name: f for f in provider_bin.iterdir()
            if f.is_file() and RUNTIME_NAME.fullmatch(f.name)}
    if not REQUIRED.issubset(set(libs) | {"scripting_v8.dll"}):
        raise ValueError("Incomplete V8 runtime dependency set")
    # Prevent a developer from pairing a patched SDK version file with
    # old provider binaries or with different UCRT runtime dependencies.
    for name, candidate in libs.items():
        sdk_candidate = sdk / "bin" / name
        if not sdk_candidate.is_file() or sdk_candidate.is_symlink():
            raise ValueError("SDK runtime DLL is missing: " + name)
        if _hash(candidate) != _hash(sdk_candidate):
            raise ValueError("Staged V8 SDK/runtime binary mismatch: " + name)
    registry_path = package / "northstarcpp.plugins.json"
    registry = json.loads(registry_path.read_text(encoding="utf-8"))
    if any(item.get("capability") == "engine.scripting"
           for item in registry.get("plugins", [])):
        raise ValueError("engine.scripting is already registered in the engine dist")
    bindir = package / "bin"
    if not bindir.is_dir():
        raise ValueError("Engine distribution has no bin directory")
    checked = {"scripting_v8.dll": provider, **libs}
    # Preflight every destination before copying even one DLL. This avoids
    # partially staged runtimes if Qt has incompatible same-name UCRT/MSVCRT
    # dependencies (libstdc++, ICU, zlib, winpthreads, etc.).
    conflicts = sorted(
        name for name, source in checked.items()
        if (bindir / name).exists() and _hash(bindir / name) != _hash(source))
    if conflicts:
        raise ValueError("UCRT/MSVCRT native DLL collision: " + ", ".join(conflicts))
    for name, source in checked.items():
        if not (bindir / name).exists():
            shutil.copy2(source, bindir / name)
    for name in ("v8", "gcc-libs", "libwinpthread", "winpthreads", "zlib"):
        if (license_dir / name).is_dir():
            shutil.copytree(license_dir / name, package / "licenses" / name,
                            dirs_exist_ok=True)
    shutil.copy2(icu_licenses[0], package / "licenses/ICU-LICENSE")
    dlls = {"bin/" + name: {"sha256": _hash(bindir / name),
                            "size": (bindir / name).stat().st_size}
            for name in sorted(libs)}
    manifest = {"schema": 1, "runtime": "V8-UCRT64",
                "v8_version": ".".join(map(str, version)), "dlls": dlls}
    (package / "v8.runtime.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    registry["plugins"].append({
        "id": "scripting.v8", "capability": "engine.scripting",
        "provider": "V8", "host_abi": registry["host_abi"],
        "default": True, "path": "bin/scripting_v8.dll",
    })
    registry_path.write_text(
        json.dumps(registry, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    registered_v8_runtime(package)
    return manifest
