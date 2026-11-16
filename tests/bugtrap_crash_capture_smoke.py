"""Test SEH and C++ terminate crash capture in disposable subprocesses (Windows)."""
import os
import struct
from pathlib import Path
import subprocess
import sys
import tempfile

def check(binary: Path, reporter: Path, mode: str, code: str, temp: Path) -> None:
    env = dict(os.environ)
    env["LOCALAPPDATA"] = str(temp / mode)
    env["NORTHSTARCPP_BUGTRAP_SILENT"] = "1"
    result = subprocess.run([str(binary), mode], env=env,
                            timeout=25, capture_output=True, text=True)
    if result.returncode == 0:
        raise AssertionError(f"{mode}: intentional crash incorrectly returned 0")
    crash_dir = temp / mode / "KaylasSystems" / "NorthStarCpp" / "Crashes"
    reports = list(crash_dir.glob("crash-*.txt"))
    if len(reports) != 1:
        raise AssertionError(f"{mode}: expected 1 report, got {reports}")
    report = reports[0]
    contents = report.read_text(encoding="utf-8")
    for required in ("NorthStarCpp Crash Report", "Application: ",
                     "Process ID: ", "Thread ID: ", "Exception address: ",
                     f"Exception code: {code}", "Privacy: Local only."):
        if required not in contents:
            raise AssertionError(f"{mode}: missing {required!r}: {contents}")
    dump = report.with_suffix(".dmp")
    if "Minidump: saved" not in contents or not dump.exists() or dump.stat().st_size == 0:
        raise AssertionError(f"{mode}: minidump not created: {contents}")
    quiet = subprocess.run([str(reporter), "--report", str(report), "--silent"],
                           timeout=12)
    if quiet.returncode != 0:
        raise AssertionError(f"{mode}: BugTrap failed to open generated report")
    print(f"PASS {mode}: code={code}, report={report.name}, dump={dump.stat().st_size} B")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: bugtrap_crash_capture_smoke.py <fixture.exe> <bugtrap.exe>")
    with tempfile.TemporaryDirectory(prefix="northstar-bugtrap-") as temp:
        check(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(),
              "seh", "0xE000B007", Path(temp))
        check(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(),
              "terminate", "0xE0000001", Path(temp))
        check(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(),
              "abort", "0xE0000002", Path(temp))
