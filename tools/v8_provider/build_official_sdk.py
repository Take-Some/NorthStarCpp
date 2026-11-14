#!/usr/bin/env python3
"""Build an embeddable Windows x64 V8 SDK from the verified Chromium source.

Requires depot_tools, Visual Studio C++/Windows SDK, GN, Ninja and 30 GiB free.
Does not equate the official d8-only archive to an embeddable C++ SDK.
Never promotes a package to production dist.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from preflight_stable_build import check_pin, inspect

GN_ARGS = {
    "is_debug": False,
    "is_component_build": False,
    "use_custom_libcxx": False,  # match clang-cl/MSVC STL in the ABI 2 DLL
    "target_cpu": "x64",
    "v8_target_cpu": "x64",
    "v8_monolithic": True,
    "v8_use_external_startup_data": False,
    "v8_use_snapshot": True,
    "v8_enable_pointer_compression": True,
    "v8_enable_sandbox": True,
    "v8_enable_i18n_support": True,
    "symbol_level": 0,
    "treat_warnings_as_errors": False,
}
MANIFEST = "northstar-v8-sdk.json"


def emit_json(path: Path, result: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def guarded_destination(path: Path, repository: Path) -> Path:
    path = path.resolve()
    repository = repository.resolve()
    if path == repository or path in repository.parents:
        raise ValueError("Destination must not overwrite the repository")
    if "dist" in {part.casefold() for part in path.parts}:
        raise ValueError("Unattested SDK cannot be written to production dist")
    if path.is_symlink():
        raise ValueError("Destination must not be a symbolic link")
    return path


def gn_args() -> str:
    def literal(value: object) -> str:
        return str(value).lower() if isinstance(value, bool) else json.dumps(value)
    return " ".join(name + "=" + literal(val) for name, val in GN_ARGS.items())


def build_steps(pin: dict, workspace: Path) -> list[list[str]]:
    sha = pin["v8_stable_tip_commit"]
    source = str(workspace / "v8")
    return [
        ["fetch", "v8"],
        ["git", "-C", source, "fetch", "origin", pin["v8_stable_branch"]],
        ["git", "-C", source, "checkout", "--detach", sha],
        ["gclient", "sync", "--revision", "v8@" + sha],
        ["gn", "gen", "out/sdk_release", "--args=" + gn_args()],
        ["ninja", "-C", "out/sdk_release", "v8_monolith", "d8"],
    ]


def execute(argv: list[str], cwd: Path, environ: dict, timeout: int = 21600) -> str:
    print("RUN", argv, "CWD", cwd, flush=True)
    result = subprocess.run(argv, cwd=cwd, env=environ, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            errors="replace", timeout=timeout)
    if result.returncode:
        raise RuntimeError("Command failed: " + repr(argv) + "\n" +
                           result.stdout[-5000:])
    return result.stdout


def sdk_version(header: Path) -> tuple[int, ...]:
    contents = header.read_text("utf-8")
    result = []
    for name in ("V8_MAJOR_VERSION", "V8_MINOR_VERSION",
                 "V8_BUILD_NUMBER", "V8_PATCH_LEVEL"):
        m = re.search(r"^\s*#define\s+" + name + r"\s+(\d+)\s*$",
                      contents, re.M)
        if not m:
            raise ValueError("Missing V8 version macro: " + name)
        result.append(int(m.group(1)))
    return tuple(result)


def stage(source: Path, destination: Path, pin: dict, output: str) -> dict:
    built = source / "out/sdk_release"
    candidates = [p for p in built.rglob("*") if p.is_file() and
                  p.name.casefold() == "v8_monolith.lib"]
    if len(candidates) != 1 or candidates[0].stat().st_size < 1024*1024:
        raise RuntimeError("A real, single static V8 monolith library is required")
    if not (source / "include/v8.h").is_file():
        raise RuntimeError("Public V8 header v8.h was not generated")
    version = sdk_version(source / "include/v8-version.h")
    if version[:2] != (15, 8) or not re.search(r"\b15\.8\.\d+", output):
        raise RuntimeError("Built SDK/d8 do not identify as V8 15.8")
    if destination.exists():
        raise FileExistsError("Refusing to overwrite SDK: " + str(destination))
    (destination / "lib").mkdir(parents=True)
    (destination / "bin").mkdir()
    shutil.copytree(source / "include", destination / "include")
    shutil.copy2(candidates[0], destination / "lib/v8_monolith.lib")
    if not (built / "d8.exe").is_file():
        raise RuntimeError("Compiled d8.exe is missing")
    shutil.copy2(built / "d8.exe", destination / "bin/d8.exe")
    for optional in ("icudtl.dat", "snapshot_blob.bin"):
        if (built / optional).is_file():
            shutil.copy2(built / optional, destination / "bin" / optional)
    shutil.copy2(source / "LICENSE", destination / "LICENSE")
    if (source / "third_party/icu/LICENSE").is_file():
        (destination / "licenses/icu").mkdir(parents=True)
        shutil.copy2(source / "third_party/icu/LICENSE",
                     destination / "licenses/icu/LICENSE")
    records = {
        p.relative_to(destination).as_posix():
            {"size": p.stat().st_size, "sha256": sha256(p)}
        for p in destination.rglob("*") if p.is_file()
    }
    manifest = {
        "schema": 1,
        "kind": "official-source-embeddable-windows-x64-v8-sdk",
        "v8_version": ".".join(map(str, version)),
        "source_commit": pin["v8_stable_tip_commit"],
        "source_branch": pin["v8_stable_branch"],
        "source_repository": pin["source_git"],
        "gn_args": GN_ARGS,
        "files": dict(sorted(records.items())),
        "toolchain": "Windows clang-cl + MSVC COFF C++ ABI",
        "requirement": "Compile a separate ABI 2 scripting DLL; do not link V8 C++ into MinGW engine",
        "security_attested": False,
        "release_approved": False,
        "created_utc": datetime.now(timezone.utc).isoformat(),
    }
    emit_json(destination / MANIFEST, manifest)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pin", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--sdk-dir", type=Path, required=True)
    parser.add_argument("--depot-tools", type=Path)
    parser.add_argument("--mode", choices=("plan", "build"), default="plan")
    parser.add_argument("--plan-output", type=Path)
    args = parser.parse_args()
    pin = check_pin(args.pin)
    repository = Path(__file__).resolve().parents[2]
    workspace = guarded_destination(args.work_dir, repository)
    destination = guarded_destination(args.sdk_dir, repository)
    if workspace == destination or workspace in destination.parents or destination in workspace.parents:
        raise ValueError("Checkout and SDK destinations must be separate")
    steps = build_steps(pin, workspace)
    if args.mode == "plan":
        plan = {
            "schema": 1,
            "purpose": "sdk_source_build_plan_only_not_a_binary",
            "source_commit": pin["v8_stable_tip_commit"],
            "source_branch": pin["v8_stable_branch"],
            "commands": [{"argv": cmd, "cwd": str(workspace if i in (0, 3)
                                                      else workspace / "v8")}
                         for i, cmd in enumerate(steps)],
            "gn_args": GN_ARGS,
            "sdk_output": str(destination),
            "release_approved": False,
        }
        if args.plan_output:
            emit_json(args.plan_output, plan)
        print(json.dumps(plan, indent=2))
        return 0
    if os.name != "nt":
        raise RuntimeError("Full SDK build requires Windows x64")
    if destination.exists():
        raise FileExistsError("SDK destination already exists")
    environ = os.environ.copy()
    if args.depot_tools:
        if not args.depot_tools.is_dir():
            raise ValueError("Specified depot_tools directory does not exist")
        environ["PATH"] = str(args.depot_tools.resolve()) + os.pathsep + environ.get("PATH", "")
    original_path = os.environ.get("PATH", "")
    try:
        os.environ["PATH"] = environ["PATH"]
        preflight = inspect(args.pin, workspace, check_remote=True)
    finally:
        os.environ["PATH"] = original_path
    emit_json(repository / "out/v8-official-windows/sdk-build-preflight.json", preflight)
    if not preflight["ready_for_full_build"]:
        missing = [k for k, ok in preflight["checks"].items() if not ok]
        raise RuntimeError("Missing official V8 build prerequisites: " + ", ".join(missing))
    environ["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "0"
    workspace.mkdir(parents=True, exist_ok=True)
    source = workspace / "v8"
    if not source.exists():
        execute(steps[0], workspace, environ)
    elif not (source / ".git").exists():
        raise RuntimeError("Existing V8 directory is not a git repository")
    if execute(["git", "status", "--porcelain"], source, environ).strip():
        raise RuntimeError("Existing V8 checkout has local modifications")
    execute(steps[1], source, environ)
    execute(steps[2], source, environ)
    if execute(["git", "rev-parse", "HEAD"], source, environ).strip() != pin["v8_stable_tip_commit"]:
        raise RuntimeError("V8 HEAD differs from pinned commit")
    execute(steps[3], workspace, environ)
    if execute(["git", "rev-parse", "HEAD"], source, environ).strip() != pin["v8_stable_tip_commit"]:
        raise RuntimeError("gclient changed the pinned official V8 source commit")
    execute(steps[4], source, environ, timeout=3600)
    execute(steps[5], source, environ)
    version = execute([str(source / "out/sdk_release/d8.exe"),
                       "-e", "print(version())"], source, environ, timeout=35)
    result = stage(source, destination, pin, version)
    print(json.dumps({"sdk": str(destination), "version": result["v8_version"],
                      "release_approved": False}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print("V8 SDK build FAILED:", error, file=sys.stderr)
        raise SystemExit(2)
