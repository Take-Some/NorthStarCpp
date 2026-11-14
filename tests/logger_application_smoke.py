"""End-to-end engine.logger lifecycle and fail-open integration smoke.

Called by CTest with a built NorthStarCpp runtime; no user profile writes.
Copyright (c) 2026 Kayla's Systems - Kayla Verner.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


class SmokeFailure(RuntimeError):
    pass


def require(condition: bool, description: str) -> None:
    if not condition:
        raise SmokeFailure(description)


def execute(bin_dir: Path, exe: str, args: list[str],
            log_destination: Path, expected: int = 0) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env["NORTHSTARCPP_LOG_DIRECTORY"] = str(log_destination)
    env.pop("NORTHSTARCPP_LOGGER_PLUGIN", None)
    command = [str(bin_dir / exe), *args]
    process = subprocess.run(
        command, cwd=bin_dir, env=env, capture_output=True,
        text=True, encoding="utf-8", errors="replace", timeout=35
    )
    require(process.returncode == expected,
            f"{exe} exit {process.returncode} != {expected}: {process.stderr[-600:]}")
    return process


def verify_content(folder: Path, expected: tuple[str, ...]) -> None:
    logs = sorted(folder.glob("*.log"))
    structured = sorted(folder.glob("*.ulog"))
    require(len(logs) == len(structured) == 1,
            f"Expected exactly one text and structured file in {folder}")
    text = logs[0].read_text(encoding="utf-8")
    rows = [json.loads(line) for line in structured[0].read_text(
        encoding="utf-8").splitlines() if line.strip()]
    require(len(rows) >= len(expected),
            f"Too few structured log records: {len(rows)}")
    require(len(text.splitlines()) >= len(expected),
            "Too few formatted log lines")
    for entry in expected:
        require(entry in text, f"Missing expected text record: {entry}")
    for row in rows:
        require(set(("timestamp_utc", "level", "channel", "message", "pid", "tid"))
                .issubset(row), "Incomplete structured log JSON")
        require(row["timestamp_utc"].endswith("Z"), "Timestamp is not UTC")
        require(row["level"] in
                ("TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"),
                "Invalid structured severity")


def run(bin_dir: Path, scene: Path) -> None:
    folder = bin_dir / "logger_application_ctest"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)

    apps = {
        "player": (
            "northstarcpp_player.exe",
            ["--check-scene", "--scene", str(scene)],
            ("game player starting", "Application exited with code 0")
        ),
        # Qt Editor has an independent QProcess/Qt log panel; its startup
        # and Player handoff are verified in northstarcpp_qt_editor_authoring
        # and northstarcpp_qt_editor_player_launch. Only engine.logger
        # consumers belong in this .log/.ulog lifecycle contract.
        "manager": (
            "northstarcpp_project_manager.exe",
            ["--smoke"],
            ("Qt Project Manager starting", "Qt Project Manager finished successfully")
        ),
        "camera": (
            "northstarcpp_camera_timeline_editor.exe",
            ["--smoke"],
            ("Camera Timeline Editor starting", "Camera Timeline Editor closed")
        )
    }
    for key, (exe, args, records) in apps.items():
        output = folder / key
        output.mkdir()
        execute(bin_dir, exe, args, output)
        verify_content(output, records)
        print("PASS", key, "records / text + structured")

    fatal = folder / "fatal"
    fatal.mkdir()
    execute(bin_dir, "northstarcpp_player.exe",
            ["--invalid-unrecognized-option"], fatal, expected=1)
    verify_content(fatal, ("[FATAL] [engine]",))
    print("PASS fatal during argument parsing")

    # Opening the log files must fail, but the applications must still run.
    blocked = folder / "directory-is-a-file"
    blocked.write_text("not a directory", encoding="utf-8")
    for key, (exe, args, _) in apps.items():
        result = execute(bin_dir, exe, args, blocked)
        require("logger" in result.stderr.lower(),
                f"{key} did not emit fallback logger error")
        print("PASS", key, "invalid log directory fallback")

    # An absent DLL override must also be fail-open in a host application.
    missing = folder / "missing_plugin"
    missing.mkdir()
    env = os.environ.copy()
    env["NORTHSTARCPP_LOG_DIRECTORY"] = str(missing)
    env["NORTHSTARCPP_LOGGER_PLUGIN"] = str(
        folder / "provider-that-does-not-exist.dll")
    probe = subprocess.run(
        [str(bin_dir / "northstarcpp_player.exe"), "--check-scene",
         "--scene", str(scene)],
        cwd=bin_dir, env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=35
    )
    require(probe.returncode == 0 and "logger" in probe.stderr.lower(),
            "Missing provider interrupted normal game startup")
    require(not list(missing.glob("*")), "Missing provider unexpectedly created files")
    print("PASS missing logger provider fallback")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--scene", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        run(arguments.build.resolve(strict=True), arguments.scene.resolve(strict=True))
    except (SmokeFailure, OSError, subprocess.TimeoutExpired, json.JSONDecodeError) as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    print("All engine.logger application lifecycle checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
