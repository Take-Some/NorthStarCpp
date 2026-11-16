#!/usr/bin/env python3
"""NorthStarCpp QA runner (build tooling, not part of engine runtime).

Runs registered native CTest scenarios, engine-specific asset regressions,
and optional installed engine/game packaging. Each run creates:
  summary.json       machine-readable manifest, coverage, failures and metadata
  junit.xml          CI-compatible xUnit/JUnit cases
  report.html        standalone styled report, no JS/remote dependencies
  logs/*.log         complete command output and per-case CTest output
  artifacts/         installed release/game packages when requested

Exit status: 0 verified; 1 failed/error; 2 QA configuration failure.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from datetime import datetime, timezone
from html import escape
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
import traceback
import uuid
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "Projects" / "NorthStarEngineQA"
DEFAULT_MANIFEST = PROJECT / "qa.manifest.json"
DEFAULT_OUTPUT = PROJECT / "reports"
SHORT_TIMEOUT = 100
MAX_LOG_BYTES = 16 * 1024 * 1024

class QAConfigurationError(ValueError):
    pass


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def read_manifest(path: Path) -> dict:
    obj = json.loads(path.read_text(encoding="utf-8"))
    if obj.get("schema") != 1 or not isinstance(obj.get("capabilities"), dict):
        raise QAConfigurationError("Invalid QA manifest schema")
    known = set()
    for label, desc in obj["capabilities"].items():
        tests = desc.get("tests", [])
        if not isinstance(tests, list) or any(not isinstance(x, str) for x in tests):
            raise QAConfigurationError("Invalid CTest names under " + label)
        for test in tests:
            if test in known:
                raise QAConfigurationError("Duplicate CTest mapping: " + test)
            known.add(test)
    return obj


def case(name: str, category: str, status: str, duration: float = 0.0,
         details: str = "", log: str = "", group: str = "") -> dict:
    if status not in ("passed", "failed", "error", "skipped"):
        raise QAConfigurationError("Unexpected QA test status: " + status)
    return {"name": name, "category": category, "status": status,
            "duration_s": round(duration, 5), "details": details[:8000],
            "log": log, "group": group}


def compact(s: str, limit: int = 2500) -> str:
    return s[-limit:].strip() if len(s) > limit else s.strip()


def write_log(folder: Path, name: str, output: str) -> str:
    folder.mkdir(parents=True, exist_ok=True)
    filename = re.sub(r"[^0-9A-Za-z_.-]", "_", name).strip("._") or "output"
    filename = filename[:110] + ".log"
    path = folder / filename
    with path.open("w", encoding="utf-8", errors="replace") as file:
        file.write(output[-MAX_LOG_BYTES:])
    return path.name


def command(cmd: list[str], cwd: Path, timeout: int, logs: Path, label: str) -> tuple[int, float, str, str]:
    began = time.perf_counter()
    try:
        result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                                errors="replace", timeout=timeout, check=False)
        output = "$ " + subprocess.list2cmdline(cmd) + "\n\n" + \
                 result.stdout + ("\n[STDERR]\n" + result.stderr if result.stderr else "")
        code = result.returncode
    except subprocess.TimeoutExpired as exc:
        def decode(buf):
            return buf.decode("utf-8", errors="replace") if isinstance(buf, bytes) else (buf or "")
        output = ("$ " + subprocess.list2cmdline(cmd) + "\n\nTIMEOUT after " +
                  str(timeout) + "s\n" + decode(exc.stdout) + "\n" + decode(exc.stderr))
        code = 124
    except OSError as exc:
        output = "PROCESS LAUNCH ERROR: " + repr(exc)
        code = 127
    duration = time.perf_counter() - began
    name = write_log(logs, label, output)
    return code, duration, name, output


def run_ctest_list(ctest: str, build: Path, logs: Path) -> tuple[list[str], dict | None]:
    code, took, log, output = command([ctest, "--test-dir", str(build), "-N"], ROOT, 45,
                                     logs, "ctest-discovery")
    if code:
        return [], case("ctest-discovery", "qa-infrastructure", "error", took, compact(output), log)
    tests = re.findall(r"^\s*Test\s+#\d+:\s+(\S+)\s*$", output, re.M)
    if not tests:
        return [], case("ctest-discovery", "qa-infrastructure", "error", took,
                        "No CTest tests discovered in build tree", log)
    return tests, None


def parse_ctest_junit(junit: Path, label_map: dict[str, str],
                      all_names: list[str], case_logs: Path) -> list[dict]:
    if not junit.is_file():
        raise QAConfigurationError("CTest did not create the expected JUnit report")
    tree = ET.parse(junit)
    cases = tree.findall(".//testcase")
    results = []
    seen = set()
    for el in cases:
        name = el.get("name", "")
        if not name:
            continue
        seen.add(name)
        try:
            duration = float(el.get("time", "0"))
        except ValueError:
            duration = 0.
        failure = el.find("failure")
        error = el.find("error")
        skip = el.find("skipped")
        status = ("failed" if failure is not None else "error" if error is not None
                  else "skipped" if skip is not None else "passed")
        output = (el.findtext("system-out") or "") + "\n" + \
                 (el.findtext("system-err") or "")
        element = failure if failure is not None else error if error is not None else skip
        details = ((element.get("message", "") if element is not None else "") +
                   "\n" + (element.text or "" if element is not None else "")).strip()
        # A separate log exists for every case, even when CTest emits no stdout.
        diagnostic = output if output.strip() else (
            "CTest case: " + name + "\nOutcome: " + status +
            "\nDuration: " + str(duration) + "s\nNo test stdout/stderr.\n")
        log = "cases/" + write_log(case_logs, name, diagnostic)
        results.append(case(name, label_map.get(name, "unclassified"), status,
                            duration, details or compact(output) if status != "passed" else "", log,
                            "native-ctest"))
    for name in all_names:
        if name not in seen:
            results.append(case(name, label_map.get(name, "unclassified"), "error",
                                details="Selected test has no JUnit result",
                                group="native-ctest"))
    return results


def run_ctest(ctest: str, build: Path, selected: list[str], logs: Path,
              mapping: dict[str, str], timeout: int) -> list[dict]:
    xml = logs.parent / "ctest-results.xml"
    cmd = [ctest, "--test-dir", str(build), "--output-junit", str(xml),
           "--output-on-failure", "--no-compress-output"]
    if selected:
        escaped = "|".join(re.escape(x) for x in selected)
        cmd += ["-R", "^(" + escaped + ")$"]
    exit_code, elapsed, log, output = command(cmd, ROOT, timeout, logs, "ctest-run")
    try:
        parsed = parse_ctest_junit(xml, mapping, selected, logs / "cases")
        if not parsed:
            raise QAConfigurationError("JUnit produced no test cases")
        if exit_code and all(c["status"] == "passed" for c in parsed):
            parsed.append(case("ctest-process", "qa-infrastructure", "error", elapsed,
                               "CTest returned " + str(exit_code) + ": " + compact(output), log))
        return parsed
    except (ET.ParseError, QAConfigurationError) as exc:
        return [case("ctest-run", "qa-infrastructure", "error", elapsed,
                     str(exc) + "\n" + compact(output), log)]


def ensure_qa_project_valid() -> dict:
    path = PROJECT / "northstarcpp.project.json"
    if not path.is_file():
        return case("qa-project", "qa-infrastructure", "error",
                    details="Missing QA project descriptor")
    try:
        obj = json.loads(path.read_text(encoding="utf-8"))
        assert obj.get("engine") == "NorthStarCpp"
        assert obj.get("project_type") == "qa"
        assert obj.get("qa_manifest") == "qa.manifest.json"
    except (OSError, ValueError, AssertionError) as exc:
        return case("qa-project", "qa-infrastructure", "error", details=repr(exc))
    return case("qa-project", "qa-infrastructure", "passed")


def run_regressions(manifest: dict, profile: str, logs: Path, python: str) -> list[dict]:
    results = []
    for spec in manifest.get("regression_targets", []):
        if profile not in spec.get("profiles", []):
            continue
        argv = [python if a == "{python}" else a for a in spec["command"]]
        code, elapsed, log, output = command(argv, ROOT, SHORT_TIMEOUT, logs, spec["id"])
        results.append(case(spec["id"], "assets-and-packaging", "passed" if code == 0 else "failed",
                            elapsed, "" if code == 0 else compact(output), log, "regression"))
    return results


def run_release(build: Path, report: Path, python: str, logs: Path) -> list[dict]:
    results = []
    package_root = report / "artifacts"
    package_root.mkdir(parents=True, exist_ok=True)
    dist_cmd = [python, "-m", "tools.build_dist", "--from-build", str(build),
                "--build-type", "Release", "--output-dir", str(package_root)]
    code, took, log, output = command(dist_cmd, ROOT, 110, logs, "engine-package")
    results.append(case("engine-package", "release-packaging",
                        "passed" if code == 0 else "failed", took,
                        "" if code == 0 else compact(output), log, "release"))
    if code != 0:
        for provider in ("jolt",):
            results.append(case("fps-package-" + provider, "release-packaging", "skipped",
                                details="Prerequisite engine-package failed", group="release"))
        return results
    packages = [p for p in package_root.glob("NorthStarCpp-*-windows-x64-Release")
                if p.is_dir() and (p / "manifest.json").is_file()]
    if len(packages) != 1:
        results.append(case("engine-package-layout", "release-packaging", "error",
                            details="Expected exactly one engine package", group="release"))
        return results
    engine = packages[0]
    for provider in ("jolt",):
        cmd = [python, "-m", "tools.build_game",
               "--project", str(ROOT / "Projects/NorthStarFPSArena"),
               "--engine-package", str(engine),
               "--output-dir", str(package_root / "games"),
               "--physics", provider]
        code, took, log, output = command(cmd, ROOT, 90, logs, "fps-package-" + provider)
        results.append(case("fps-package-" + provider, "release-packaging",
                            "passed" if code == 0 else "failed", took,
                            "" if code == 0 else compact(output), log, "release"))
    return results


def git_revision(logs: Path) -> str | None:
    code, _, _, out = command(["git", "rev-parse", "--short", "HEAD"], ROOT, 12,
                               logs, "git-revision")
    return out.splitlines()[-1].strip() if code == 0 else None


def summarize(results: list[dict], capabilities: dict, selected: list[str],
              profile: str, expected: list[str], run_id: str, build: Path,
              started: str, finish: str, duration: float, build_result: str) -> dict:
    counts = {kind: sum(x["status"] == kind for x in results)
              for kind in ("passed", "failed", "error", "skipped")}
    coverage = {}
    index = {x["name"]: x for x in results}
    for name, desc in capabilities.items():
        targets = desc.get("tests", [])
        present = [index[n] for n in targets if n in index]
        if not present:
            state = "not_run" if profile == "quick" else "not_covered"
        elif any(x["status"] in ("error", "failed") for x in present):
            state = "failed"
        elif len(present) == len(targets) and all(x["status"] == "passed" for x in present):
            state = "passed"
        else:
            state = "partial"
        coverage[name] = {"owner": desc.get("owner", ""),
                          "status": state, "planned": len(targets),
                          "executed": len(present),
                          "tests": targets}
    missing = sorted(set(expected) - set(index))
    status = ("failed" if (counts["failed"] or counts["error"] or missing)
              else "passed")
    return {"schema": 1, "qa_project": "NorthStarEngineQA", "run_id": run_id,
            "profile": profile, "status": status, "started_at": started,
            "finished_at": finish, "duration_s": round(duration, 3),
            "build_dir": str(build), "build_result": build_result,
            "environment": {"os": platform.platform(), "arch": platform.machine(),
                            "python": platform.python_version()},
            "counts": counts, "ctest_discovered": len(selected),
            "unreported_tests": missing, "capabilities": coverage, "tests": results}


def write_junit(report: Path, results: list[dict], elapsed: float) -> None:
    counts = {name: sum(x["status"] == status for x in results)
              for name, status in [("failures", "failed"), ("errors", "error"), ("skipped", "skipped")]}
    suite = ET.Element("testsuite", {"name": "NorthStarCpp QA",
                  "tests": str(len(results)), "time": str(round(elapsed, 3)),
                  **{k: str(v) for k,v in counts.items()}})
    for item in results:
        el = ET.SubElement(suite, "testcase", {
            "classname": item["category"], "name": item["name"],
            "time": str(item["duration_s"])})
        kind = item["status"]
        if kind in ("failed", "error", "skipped"):
            sub = ET.SubElement(el, {"failed":"failure","error":"error",
                                    "skipped":"skipped"}[kind],
                                {"message": item["details"][:500]})
            sub.text = item["details"]
        if item["log"]:
            ET.SubElement(el,"system-out").text = "Logs: " + item["log"]
    ET.indent(suite, space="  ")
    ET.ElementTree(suite).write(report / "junit.xml",
                                encoding="utf-8", xml_declaration=True)


def write_html(report: Path, summary: dict, gaps: list[dict]) -> None:
    results = summary["tests"]
    def badge(status):
        return '<span class="status '+escape(status)+'">'+escape(status)+'</span>'
    stats = " ".join(f'<span class="stat"><b>{summary["counts"][s]}</b> {s}</span>'
                     for s in ("passed","failed","error","skipped"))
    matrix = "\n".join(
        '<tr><td>'+escape(name)+'</td><td>'+escape(desc["owner"])+'</td><td>'+
        badge(desc["status"])+'</td><td>'+str(desc["executed"])+
        '/'+str(desc["planned"])+'</td></tr>'
        for name,desc in summary["capabilities"].items())
    rows = "\n".join(
        '<tr><td>'+escape(c["name"])+'</td><td>'+escape(c["category"])+'</td><td>'+
        badge(c["status"])+'</td><td>'+format(c["duration_s"],'.2f')+'s</td><td>'+
        (('<a href="logs/'+escape(c["log"])+'">log</a>') if c["log"] else "—")+
        '</td><td><code>'+escape(c["details"][:600])+'</code></td></tr>'
        for c in results)
    gap_rows = "\n".join('<tr><td>'+escape(x["id"])+'</td><td>'+
                         badge("not_covered")+'</td><td>'+escape(x["reason"])+'</td></tr>'
                         for x in gaps)
    html = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>NorthStar QA Report</title><style>
:root{font-family:Segoe UI,Arial,sans-serif;color:#26364a;background:#f5f7fa}
body{max-width:1440px;margin:28px auto;padding:0 20px}h1{margin-bottom:4px}
small,.muted{color:#718093}section{background:#fff;margin:20px 0;padding:22px;
border-radius:12px;border:1px solid #e5eaf0;overflow:auto}
table{border-collapse:collapse;width:100%;font-size:13px}
th,td{text-align:left;padding:10px 12px;border-bottom:1px solid #edf1f5;vertical-align:top}
th{background:#f7f9fc;color:#374a61}code{white-space:pre-wrap;overflow-wrap:anywhere}
.status{font-size:12px;font-weight:650;padding:4px 8px;border-radius:6px;background:#e6eaf0}
.passed{background:#d9f3e4;color:#166539}.failed,.error{background:#fee3e1;color:#ad2727}
.skipped,.not_run,.not_covered,.partial{background:#fff0d0;color:#795716}
.stat{display:inline-block;padding:10px;margin:6px 10px 2px 0;background:#eff4f9;border-radius:6px}
a{color:#167dae}p{line-height:1.6}
</style></head><body>
<h1>NorthStarCpp — Engine QA</h1>
<p class="muted">Run RUNID · PROFILE · STARTED · Duration DURATIONs</p>
<section><h2>Summary STATUS</h2>STATS
<p>See <a href="summary.json">structured JSON</a> and
<a href="junit.xml">CI/JUnit XML</a>. Unverified features are explicitly not covered.</p></section>
<section><h2>Capability matrix</h2><table><thead><tr>
<th>Subsystem</th><th>Owner</th><th>Result</th><th>Checks</th>
</tr></thead><tbody>MATRIX</tbody></table></section>
<section><h2>Executed checks</h2><table><thead><tr>
<th>Test</th><th>Area</th><th>Result</th><th>Time</th><th>Output</th><th>Details</th>
</tr></thead><tbody>ROWS</tbody></table></section>
<section><h2>Known coverage gaps (not tested)</h2><table><thead><tr>
<th>Area</th><th>Status</th><th>Remaining requirement</th>
</tr></thead><tbody>GAPS</tbody></table></section>
<footer class="muted">Reports are local artifacts; no telemetry or remote upload.</footer>
</body></html>"""
    for old, new in {
        "RUNID":escape(summary["run_id"]), "PROFILE":escape(summary["profile"]),
        "STARTED":escape(summary["started_at"]),
        "DURATION":format(summary["duration_s"],".2f"),
        "STATUS":badge(summary["status"]), "STATS":stats, "MATRIX":matrix,
        "ROWS":rows, "GAPS":gap_rows,
    }.items():
        html = html.replace(old, new)
    (report / "report.html").write_text(html, encoding="utf-8")


def safe_report_dir(base: Path, run_id: str) -> Path:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{2,90}", run_id):
        raise QAConfigurationError("Unsafe report run id")
    result = base.resolve() / run_id
    # Never overwrite a previous QA run; evidence is immutable by default.
    result.mkdir(parents=True, exist_ok=False)
    (result / "logs").mkdir()
    return result


def execute(args: argparse.Namespace) -> tuple[dict, Path]:
    manifest = read_manifest(args.manifest)
    started = utc_now()
    timer = time.perf_counter()
    report = safe_report_dir(args.output, args.run_id)
    logs = report / "logs"
    state_file = report / "run_state.json"
    state_file.write_text(json.dumps({
        "schema": 1, "status": "running", "profile": args.profile,
        "started_at": started, "run_id": args.run_id,
        "note": "If this is left running after termination, the run is incomplete."
    }, indent=2), encoding="utf-8")
    build = args.build_dir.resolve()
    results = [ensure_qa_project_valid()]
    build_state = "reused"
    mapping = {name: category for category, data in manifest["capabilities"].items()
               for name in data["tests"]}
    # Regenerate CMake before a requested build. Some NorthStar Windows
    # configurations suppress automatic Ninja regeneration, so skipping this
    # would falsely test old binaries after newly added engine source files.
    if args.build:
        code, seconds, log, output = command(
            [args.cmake, "-S", str(ROOT), "-B", str(build)],
            ROOT, args.build_timeout, logs, "configure")
        results.append(case("configure", "qa-infrastructure",
                            "passed" if code == 0 else "failed", seconds,
                            "" if code == 0 else compact(output), log))
        if code:
            build_state = "configure-failed"
        else:
            code, seconds, log, output = command(
                [args.cmake, "--build", str(build), "--target", "all",
                 "--parallel", str(args.jobs)],
                ROOT, args.build_timeout, logs, "build")
            build_state = "passed" if code == 0 else "failed"
            results.append(case("build", "qa-infrastructure",
                                "passed" if code == 0 else "failed", seconds,
                                "" if code == 0 else compact(output), log))
    if build_state == "configure-failed":
        discovered, issue = [], case("ctest-discovery", "qa-infrastructure", "skipped",
                                      details="CMake configuration failed")
    else:
        discovered, issue = run_ctest_list(args.ctest, build, logs)
    selected = []
    if issue:
        results.append(issue)
        build_state = "missing-ctest"
    else:
        # Quick runs a curated minimum of independent native integration
        # checks; full/release always run every CTest, including newly added ones.
        quick = {"northstarcpp_math", "northstarcpp_project_manager_window",
                 "northstarcpp_shared_assets", "northstarcpp_assets_vfs",
                 "northstarcpp_input_win32", "northstarcpp_flecs_ecs",
                 "northstarcpp_jolt_physics",
                 "northstarcpp_fps_arena", "northstarcpp_fps_ui",
                 "northstarcpp_editor_player", "northstarcpp_capability_swap",
                 "northstarcpp_camera_director", "northstarcpp_camera_d3d11"}
        selected = discovered if args.profile != "quick" else [n for n in discovered if n in quick]
        if args.profile == "quick" and not selected:
            results.append(case("qa-discovery", "qa-infrastructure", "error",
                                details="No quick-profile test cases registered"))
        if build_state not in ("failed", "configure-failed") and selected:
            results += run_ctest(args.ctest, build, selected, logs, mapping,
                                 args.test_timeout)
        elif build_state in ("failed", "configure-failed"):
            results.extend(case(n, mapping.get(n, "unclassified"), "skipped",
                                details="Build prerequisite failed", group="native-ctest")
                           for n in selected)
    if args.profile in ("full", "release"):
        if not issue and build_state not in ("failed", "configure-failed"):
            results += run_regressions(manifest, args.profile, logs, args.python)
        else:
            for spec in manifest.get("regression_targets", []):
                if args.profile in spec.get("profiles", []):
                    results.append(case(spec["id"], "assets-and-packaging", "skipped",
                                        details="Build/discovery prerequisite failed",
                                        group="regression"))
    if args.profile == "release":
        if not issue and build_state != "failed" and not any(
            c["status"] in ("failed", "error") for c in results):
            results += run_release(build, report, args.python, logs)
        else:
            results.append(case("release-packaging", "release-packaging", "skipped",
                                details="Earlier QA stage failed", group="release"))
    # Avoid assuming a passing score represents modules with no test coverage.
    summary = summarize(results, manifest["capabilities"], selected, args.profile,
                        selected, args.run_id, build, started, utc_now(),
                        time.perf_counter()-timer, build_state)
    summary["known_gaps"] = manifest.get("known_gaps", [])
    summary["ctest_registered_total"] = len(discovered)
    summary["git_revision"] = git_revision(logs)
    summary["commands"] = {"build_requested": args.build,
                           "build_timeout_s": args.build_timeout,
                           "test_timeout_s": args.test_timeout,
                           "jobs": args.jobs}
    (report / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    state_file.write_text(json.dumps({
        "schema": 1, "status": "completed", "result": summary["status"],
        "profile": args.profile, "finished_at": summary["finished_at"],
        "run_id": args.run_id
    }, indent=2), encoding="utf-8")
    write_junit(report, results, summary["duration_s"])
    write_html(report, summary, manifest.get("known_gaps", []))
    (report / "README.txt").write_text(
        "NorthStarCpp QA Evidence\n"
        "Run: " + args.run_id + "\n"
        "Open report.html for visual inspection.\n"
        "Load summary.json for automation or junit.xml for CI.\n"
        "Logs remain in logs/. No file in this report is uploaded.\n", encoding="utf-8")
    return summary, report


def build_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("quick","full","release"), default="full")
    parser.add_argument("--build-dir", type=Path, default=ROOT/"out/gcc-shared-assets-release")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--run-id", default="qa-" + datetime.now(timezone.utc).strftime(
        "%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:6])
    parser.add_argument("--build", action="store_true",
                        help="Build all native tests and modules before executing QA")
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--build-timeout", type=int, default=900)
    parser.add_argument("--test-timeout", type=int, default=700)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--python", default=sys.executable)
    args = parser.parse_args(argv)
    if not 1 <= args.jobs <= 64:
        parser.error("Job count must be between 1 and 64")
    if args.build_timeout < 10 or args.test_timeout < 10:
        parser.error("Timeout must be >= 10 seconds")
    return args


def main(argv: list[str] | None = None) -> int:
    args = build_arguments(argv)
    try:
        summary, report = execute(args)
        print(json.dumps({"status": summary["status"], "run": summary["run_id"],
                          "report": str(report / "report.html"),
                          "json": str(report / "summary.json"),
                          "junit": str(report / "junit.xml"),
                          "counts": summary["counts"],
                          "known_gaps": len(summary["known_gaps"])}, indent=2))
        return 0 if summary["status"] == "passed" else 1
    except (QAConfigurationError, OSError, json.JSONDecodeError) as exc:
        print("QA CONFIGURATION ERROR: " + str(exc), file=sys.stderr)
        return 2
    except Exception as exc:
        # Unexpected runner defects must also fail CI.
        print("QA INTERNAL FAILURE: " + str(exc), file=sys.stderr)
        traceback.print_exc()
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
