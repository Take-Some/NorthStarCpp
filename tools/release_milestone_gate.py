#!/usr/bin/env python3
"""Evidence-based NorthStarCpp 1.0 milestone audit.

Read-only against sources and installed binaries. This is a focused acceptance
matrix, not a replacement for tools/qa_runner.py or release_readiness_gate.py.
No stage is declared production-ready solely because its CTest cases pass.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "out" / "build"

GATES = {
    "1": {
        "title": "Gameplay foundation",
        "tests": [
            "northstarcpp_animation_graph",
            "northstarcpp_player_action_events",
            "northstarcpp_player_blueprint_physics",
            "northstarcpp_viewport_skeletal_pipeline",
            "northstarcpp_camera_physics_jolt",
            "northstarcpp_save_game",
        ],
        "manual": [
            "Live walk/run/jump/fall transitions without bind-pose frames",
            "Possess/unpossess and camera transition in a real game session",
            "NPC behavior, event delivery and save/load round-trip in gameplay",
        ],
    },
    "2": {
        "title": "Complete editor workflow",
        "tests": [
            "northstarcpp_editor_prefab",
            "northstarcpp_asset_catalog",
            "northstarcpp_prefab_composition",
            "northstarcpp_qt_material_graph_authoring",
            "northstarcpp_project_script_assets",
        ],
        "manual": [
            "Clean create/import/edit/play/build path in native Qt editor",
            "Outliner multi-select, Details, Undo/Redo, drag/drop and persistence",
            "No C++ source edits required for baseline game authoring",
        ],
    },
    "3": {
        "title": "Playable vertical slice",
        "tests": [
            "northstarcpp_fps_arena",
            "northstarcpp_vehicle_dynamics",
            "northstarcpp_scene_runtime_jolt",
            "northstarcpp_project_gameplay_scripts",
        ],
        "manual": [
            "15-30 minute playable KaylaNightCity district with objective",
            "Animated pathfinding NPCs with avoidance and working transport",
            "Quest completion, checkpoint save/load and packaged game launch",
        ],
    },
    "4": {
        "title": "Production stability",
        "tests": [
            "northstarcpp_frame_profiler",
            "northstarcpp_runtime_frame_scheduler",
            "northstarcpp_asset_streaming_internal",
            "northstarcpp_provider_select_render_vulkan",
        ],
        "manual": [
            "CPU/GPU p50/p95/p99 on representative game scene and target hardware",
            "Leak, soak, crash and asset hot-reload regressions",
            "Identical-scene Vulkan/D3D12 image and performance comparison",
        ],
    },
    "5": {
        "title": "Release 1.0",
        "tests": [
            "northstarcpp_startup_report_runtime",
            "northstarcpp_capability_swap",
            "northstarcpp_player_scriptless_root",
            "northstarcpp_project_script_assets",
        ],
        "manual": [
            "Clean-tree reproducible engine/editor builds and release package",
            "Independent clean-machine editor/new-project/game launch report",
            "Third-party licensing, documentation and independent SDK sample",
            "Signed security attestation where required by release gate",
        ],
    },
}

def process(args: list[str], cwd: Path, timeout: int) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True,
                          errors="replace", timeout=timeout, check=False)

def discover(ctest: str, build: Path) -> tuple[set[str], str]:
    result = process([ctest, "--test-dir", str(build), "-N"], ROOT, 90)
    names = set(re.findall(r"^\s*Test\s+#\d+:\s+(\S+)\s*$",
                           result.stdout, flags=re.MULTILINE))
    if result.returncode or not names:
        raise RuntimeError("CTest discovery failed: " + (result.stderr or result.stdout)[-1200:])
    return names, result.stderr.strip()

def git_state() -> dict:
    try:
        result = process(["git", "status", "--porcelain", "--untracked-files=no"], ROOT, 20)
        if result.returncode:
            return {"known": False, "clean": False}
        entries = [x for x in result.stdout.splitlines() if x.strip()]
        return {"known": True, "clean": len(entries) == 0,
                "tracked_changed_count": len(entries)}
    except (OSError, subprocess.TimeoutExpired):
        return {"known": False, "clean": False}

def test_stage(ctest: str, build: Path, names: list[str], timeout: int, junit: Path) -> dict:
    pattern = "^(" + "|".join(re.escape(x) for x in names) + ")$"
    cmd = [ctest, "--test-dir", str(build), "--output-on-failure",
           "--timeout", str(timeout), "--output-junit", str(junit),
           "-R", pattern]
    try:
        result = process(cmd, ROOT, max(30, timeout * len(names) + 30))
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"status": "failed", "error": str(exc), "tests": {}}
    observed: dict[str, str] = {}
    if junit.is_file():
        try:
            for el in ET.parse(junit).findall(".//testcase"):
                name = el.get("name", "")
                if name in names:
                    status = ("failed" if el.find("failure") is not None
                              else "error" if el.find("error") is not None
                              else "skipped" if el.find("skipped") is not None
                              else "passed")
                    observed[name] = status
        except ET.ParseError:
            pass
    for name in names:
        observed.setdefault(name, "missing_result")
    ok = result.returncode == 0 and all(x == "passed" for x in observed.values())
    return {"status": "tests_passed_manual_pending" if ok else "failed",
            "exit_code": result.returncode,
            "tests": observed,
            "ctest_output_tail": result.stdout[-3500:],
            "ctest_stderr_tail": result.stderr[-1200:]}

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", choices=["all", *GATES], default="all")
    parser.add_argument("--build-dir", type=Path, default=BUILD)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--timeout", type=int, default=35)
    parser.add_argument("--run", action="store_true",
                        help="Run named CTest smoke tests; default only discovers coverage")
    parser.add_argument("--report", type=Path, default=None,
                        help="New JSON evidence file; existing reports are never overwritten")
    args = parser.parse_args()
    if args.report is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        args.report = ROOT / "out" / "qa" / ("milestone_" + args.stage + "_" + stamp + ".json")
    if args.timeout < 1:
        parser.error("--timeout must be >= 1")
    stages = list(GATES) if args.stage == "all" else [args.stage]
    report = {"schema": 1, "generated_utc": datetime.now(timezone.utc).isoformat(),
              "source": str(ROOT), "build": str(args.build_dir.resolve()),
              "git": git_state(), "executed_ctest": args.run,
              "warning": "Smoke results are not release certification. Manual evidence and clean build are required.",
              "stages": {}}
    try:
        discovered, warning = discover(args.ctest, args.build_dir.resolve())
        report["ctest_registered"] = len(discovered)
        if warning:
            report["ctest_discovery_warning"] = warning
    except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
        report["discovery_error"] = str(exc)
        discovered = set()
    for key in stages:
        gate = GATES[key]
        missing = [name for name in gate["tests"] if name not in discovered]
        info = {"title": gate["title"], "required_ctest": gate["tests"],
                "unregistered_ctest": missing, "manual_acceptance_pending": gate["manual"],
                "status": "blocked_missing_tests" if missing else "not_run"}
        if args.run and not missing:
            junit = args.report.parent / ("milestone_" + key + "_ctest.xml")
            junit.parent.mkdir(parents=True, exist_ok=True)
            info.update(test_stage(args.ctest, args.build_dir.resolve(),
                                   gate["tests"], args.timeout, junit))
            info["junit"] = str(junit)
        report["stages"][key] = info
    # Fail closed: no stage is declared complete without independent manual evidence.
    report["release_ready"] = False
    report["summary"] = "Blocked" if any(x["status"].startswith(("failed", "blocked"))
                                      for x in report["stages"].values()) else "Not certified"
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open("x", encoding="utf-8") as evidence:
        evidence.write(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps({"report": str(args.report), "git": report["git"],
                      "stages": {k: {"status": v["status"],
                                     "unregistered_ctest": v["unregistered_ctest"],
                                     "failed_tests": [n for n, status in
                                                      v.get("tests", {}).items()
                                                      if status != "passed"]}
                                 for k, v in report["stages"].items()},
                      "release_ready": False}, ensure_ascii=False, indent=2))
    return 1 if report["summary"] == "Blocked" else 0

if __name__ == "__main__":
    sys.exit(main())
