#!/usr/bin/env python3
"""Integration-check a Release-like bin launcher against privately bundled Qt."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.deploy_qt import deploy, isolate_qt_runtime, verified_extras
from tools.build_dist import SYSTEM_DLLS

APP = "northstarcpp_project_manager.exe"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--gpp", required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    real = build / APP
    launcher = build / "qt_launchers" / "northstarcpp_project_manager_launcher.exe"
    if not real.is_file() or not launcher.is_file():
        parser.error("Compile the Qt project manager and its native launcher first")
    with tempfile.TemporaryDirectory(prefix="northstar-qt-release-") as temp:
        stage = Path(temp)
        bin_dir = stage / "bin"
        bin_dir.mkdir()
        shutil.copy2(real, bin_dir / APP)
        source_license = Path(__file__).resolve().parents[1] / (
            "engine/northstarcpp/resources/fontawesome/LICENSE.txt")
        license_target = stage / "licenses/FontAwesome/LICENSE.txt"
        license_target.parent.mkdir(parents=True)
        shutil.copy2(source_license, license_target)
        runtime = deploy(stage, args.gpp, SYSTEM_DLLS)
        source = stage / "tools/qt_launchers/northstarcpp_project_manager_launcher.exe"
        source.parent.mkdir(parents=True)
        shutil.copy2(launcher, source)
        result = isolate_qt_runtime(stage)
        if result.get("layout") != "process-isolated" or verified_extras(stage):
            raise RuntimeError("Qt isolation verification failed")
        if not (bin_dir / APP).is_file() or not (
                bin_dir / "qt_tools" / APP).is_file():
            raise RuntimeError("Public launcher or private Qt program is missing")
        if any(bin_dir.glob("Qt6*.dll")):
            raise RuntimeError("Qt DLL leaked into public bin")
        env = os.environ.copy()
        windows = Path(os.environ.get("SystemRoot", "C:/Windows"))
        env["PATH"] = str(windows / "System32") + os.pathsep + str(windows)
        env["QT_QPA_PLATFORM"] = "windows"
        env.pop("QT_PLUGIN_PATH", None)
        env.pop("QT_QPA_PLATFORM_PLUGIN_PATH", None)
        env.pop("NORTHSTARCPP_MODULES", None)
        marker = stage / "launcher-integration-marker.txt"
        proc = subprocess.run([str(bin_dir / APP), "--smoke-editor-child",
                               str(marker)],
                              cwd=bin_dir, env=env, capture_output=True,
                              text=True, errors="replace", timeout=45)
        print("root_launcher:", bin_dir / APP)
        print("qt_runtime:", bin_dir / "qt_tools")
        print("return_code:", proc.returncode)
        if proc.stdout:
            print("stdout:", proc.stdout[-2500:])
        if proc.stderr:
            print("stderr:", proc.stderr[-2500:])
        if proc.returncode != 0 or not marker.is_file():
            return proc.returncode or 1
        if marker.read_bytes() != b"\n\n":
            raise RuntimeError("Qt child did not run with expected environment")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
