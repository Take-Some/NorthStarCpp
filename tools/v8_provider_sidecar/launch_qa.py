#!/usr/bin/env python3
"""Launch a LOCAL development Qt Editor with genuine V8 15.8.62 sidecar.

This is a verified d8 process-worker, NOT an embeddable native V8 SDK.
Does not modify production dist or existing QA package contents.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
V8 = ROOT / "out/v8-official-windows/15.8.62"
DEFAULT_QA = ROOT / ("out/asset-mount-playfix-qa-20261114/"
    "NorthStarCpp-0.4.0-windows-x64-Release-QA/bin")


def hash_file(file: Path) -> str:
    h = hashlib.sha256()
    with file.open("rb") as stream:
        for part in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(part)
    return h.hexdigest()


def verify_runtime() -> tuple[Path, Path, Path]:
    provenance = json.loads((V8 / "sidecar-startup-fix-provenance.json").read_text("utf8"))
    if provenance.get("security_approved") is not False:
        raise RuntimeError("Only the separately classified development runtime may use this launcher")
    paths = (
        (V8 / "sidecar-startup-fix-build/scripting_v8_sidecar.dll", "provider_sha256"),
        (V8 / "bin/d8.exe", "d8_sha256"),
        (ROOT / "tools/v8_provider_sidecar/v8_worker.js", "worker_sha256"),
    )
    result = []
    for path, key in paths:
        if not path.is_file() or path.is_symlink():
            raise FileNotFoundError(path)
        if hash_file(path) != provenance[key]:
            raise RuntimeError("V8 developer sidecar hash mismatch: " + str(path))
        result.append(path.resolve())
    return tuple(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qa-bin", type=Path, default=DEFAULT_QA)
    parser.add_argument("--smoke", action="store_true",
                        help="Verify real Player script startup without opening a window")
    args = parser.parse_args()
    bin_dir = args.qa_bin.resolve(strict=True)
    player = bin_dir / "northstarcpp_player.exe"
    editor = bin_dir / "northstarcpp_editor_qt.exe"
    if not player.is_file() or not editor.is_file():
        raise RuntimeError("Qt Editor/Player must be from the same installed QA build")
    provider, d8, worker = verify_runtime()
    env = os.environ.copy()
    for name in list(env):
        if name in ("NORTHSTARCPP_MODULES", "NORTHSTARCPP_SCRIPTING_PLUGIN",
                    "NORTHSTARCPP_V8_D8_EXECUTABLE", "NORTHSTARCPP_V8_SIDECAR_WORKER"):
            del env[name]
    env.update({
        "NORTHSTARCPP_SCRIPTING_PLUGIN": str(provider),
        "NORTHSTARCPP_V8_D8_EXECUTABLE": str(d8),
        "NORTHSTARCPP_V8_SIDECAR_WORKER": str(worker),
    })
    if args.smoke:
        scene = V8 / "qa-scripting-project/main.nscene"
        command = [str(player), "--runtime-init-smoke", "--scene", str(scene)]
    else:
        command = [str(editor)]
    print("NorthStarCpp developer-only V8 15.8.62 runtime (not release approved)")
    print("Program:", command[0])
    try:
        result = subprocess.run(command, cwd=bin_dir, env=env,
                                timeout=70 if args.smoke else None,
                                capture_output=args.smoke, text=args.smoke,
                                errors="replace" if args.smoke else None)
        if args.smoke and result.stdout:
            print(result.stdout.strip())
        if args.smoke and result.stderr:
            print("Player stderr:", result.stderr.strip(), file=sys.stderr)
        if result.returncode:
            raise RuntimeError("Player/Editor exited with code " + str(result.returncode))
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("V8 developer runtime smoke timed out") from error
    if args.smoke:
        print("V8 ABI 2 Player startup regression: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        print("V8 development launcher FAILED:", error, file=sys.stderr)
        raise SystemExit(2)
