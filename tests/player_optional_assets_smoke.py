"""Optional asset resilience in a real NorthStarCpp WARP Player run.

Creates a temporary copy of a playable project. Corrupts only optional
camera, skydome, and streaming-zone data in the copy. The source project,
sharedAssets and installed binaries are never modified.

Usage: python tests/player_optional_assets_smoke.py ^
    --player out/fps-release-qa-20261114/northstarcpp_player.exe
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "Projects" / "NorthStarFirstPersonPlatform"


def verify(player: Path, timeout: int = 60) -> None:
    player = player.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="northstar-corrupt-optional-") as tmp:
        project = Path(tmp) / "project"
        shutil.copytree(PROJECT, project)
        for name in (
            "assets/models/skydome.ydd",
            "assets/gameplay/camera.json",
            "assets/streaming/zones.json",
        ):
            path = project / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"invalid optional source")
        env = os.environ.copy()
        env["PATH"] = str(player.parent) + os.pathsep + env.get("PATH", "")
        result = subprocess.run(
            [
                str(player), "--warp", "--character-smoke", "--explore",
                "--scene", str(project / "main.nscn"),
                "--shared-assets", str(ROOT / "sharedAssets"),
            ],
            cwd=ROOT,
            env=env,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=timeout,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"Player returned {result.returncode}\n"
                f"stdout: {result.stdout[-1400:]}\n"
                f"stderr: {result.stderr[-2400:]}"
            )
        if "Project-defined textured skeletal draw: PASS" not in result.stdout:
            raise RuntimeError(
                "Valid character did not continue rendering after invalid assets"
            )
        for warning in (
            "Skipping invalid gameplay/camera.json",
            "Skipping invalid streaming zone manifest",
            "Skipping invalid skydome asset",
        ):
            hits = [line for line in result.stderr.splitlines()
                    if "[WARN] [engine.assets]" in line and warning in line]
            if len(hits) != 1:
                raise RuntimeError(
                    f"Expected exactly one warning for {warning}, got {len(hits)}"
                )
        print("Optional corrupt camera, sky and streaming zones: "
              "3 warnings, valid character render PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--player", type=Path, required=True)
    args = parser.parse_args()
    verify(args.player)
