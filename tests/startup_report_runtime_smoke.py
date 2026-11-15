"""Acceptance of actual logger startup trace and consolidated report.

Verifies scene/physics/streaming startup, real Win32 input/GPU selection and
clean handling of a startup failure. Uses a temporary log directory.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run_case(player: Path, scene: Path, shared: Path, folder: Path,
             *, character: bool) -> tuple[subprocess.CompletedProcess[str], dict, str, str]:
    folder.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["NORTHSTARCPP_LOG_DIRECTORY"] = str(folder)
    cmd = [str(player), "--warp",
           "--character-smoke" if character else "--runtime-init-smoke",
           "--explore", "--scene", str(scene),
           "--shared-assets", str(shared)]
    result = subprocess.run(
        cmd, cwd=player.parent, env=env, timeout=60,
        text=True, errors="replace", capture_output=True)
    # Project scripts intentionally create their own independent logger
    # session in the same folder. Verify the selected Player host pair.
    reports = list(folder.glob("NorthStarCpp-Player-*-startup.json"))
    logs = list(folder.glob("NorthStarCpp-Player-*.log"))
    ulogs = list(folder.glob("NorthStarCpp-Player-*.ulog"))
    if len(reports) != 1 or len(logs) != 1 or len(ulogs) != 1:
        raise AssertionError(
            f"Expected one JSON startup report, text and structured log; "
            f"got reports={reports}, logs={logs}, ulogs={ulogs}\n"
            f"stderr={result.stderr[-2500:]}")
    record = json.loads(reports[0].read_text(encoding="utf-8"))
    return result, record, logs[0].read_text(encoding="utf-8"), ulogs[0].read_text(encoding="utf-8")


def verify(player: Path, scene: Path, shared: Path) -> None:
    player = player.resolve(strict=True)
    scene = scene.resolve(strict=True)
    shared = shared.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="northstar-startup-report-") as temp:
        base = Path(temp)
        init, report, log, ulog = run_case(
            player, scene, shared, base / "initialize", character=False)
        assert init.returncode == 0, (init.stdout, init.stderr)
        assert "Player runtime asset initialization: PASS" in init.stdout
        assert report["schema"] == "northstar.startup.v1"
        assert report["outcome"] in ("READY", "READY_WITH_WARNINGS")
        stages = {(e["phase"], e["status"]) for e in report["events"]}
        required = ("arguments.parse", "logger.initialize", "scene.load",
                    "script.discovery", "physics.provider", "scene.partition",
                    "streaming.zones", "application.construct")
        for key in required:
            assert (key, "PASS") in stages, (key, stages)
        assert all(e["duration_ms"] >= 0 for e in report["events"])
        assert any(e["phase"] == "scene.summary" and "objects=" in e["detail"]
                   for e in report["events"])
        assert "phase=physics.provider status=PASS" in log, log[-1800:]
        assert '"channel":"engine.startup"' in ulog, ulog[-1800:]
        print("Startup scene/physics/assets telemetry + .log/.ulog/JSON: PASS")

        graphical, report, log, ulog = run_case(
            player, scene, shared, base / "renderer", character=True)
        assert graphical.returncode == 0, (graphical.stdout, graphical.stderr)
        assert "Project-defined textured skeletal draw: PASS" in graphical.stdout
        stages = {(e["phase"], e["status"]) for e in report["events"]}
        for key in ("window.classes", "window.create", "input.provider",
                    "window.surface", "renderer.provider",
                    "renderer.asset_mounts", "frame.timer"):
            assert (key, "PASS") in stages, (key, stages)
        assert "selected=" in log and "phase=renderer.provider" in log
        assert report["outcome"] in ("READY", "READY_WITH_WARNINGS")
        print("Startup Win32/input/renderer/scene logs and character WARP QA: PASS")

        failure, report, log, ulog = run_case(
            player, base / "does-not-exist.nscn", shared,
            base / "failure", character=False)
        assert failure.returncode != 0, (failure.stdout, failure.stderr)
        assert report["outcome"] == "FAILED", report
        assert report["failures"] >= 1, report
        assert any(e["phase"] == "scene.load" and e["status"] == "FAIL"
                   for e in report["events"])
        assert "phase=summary status=FAILED" in log
        print("Failed startup produces explicit failure report before window creation: PASS")


if __name__ == "__main__":
    verify(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]))
