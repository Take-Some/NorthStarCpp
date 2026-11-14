"""Run an integration test against an optional sample project.

An entirely absent sample is reported as CTest SKIP (exit 77). A present
but broken/incomplete sample is never skipped and remains a failing test.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


def main() -> int:
    if "--" not in sys.argv:
        print("Required -- command delimiter not found", file=sys.stderr)
        return 2
    marker = sys.argv.index("--")
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample", type=Path, required=True)
    options = parser.parse_args(sys.argv[1:marker])
    command = sys.argv[marker + 1:]
    if not command:
        print("No test command supplied", file=sys.stderr)
        return 2
    if not options.sample.exists():
        print("SKIP: optional NorthStarFPSArena sample project is not installed: "
              + str(options.sample))
        return 77
    if not options.sample.is_dir():
        print("FAIL: optional FPS sample path exists but is not a directory: "
              + str(options.sample), file=sys.stderr)
        return 1
    # If the project exists, pass through the real test without suppressing
    # invalid scenes, missing asset files, or process failures.
    try:
        result = subprocess.run(command, timeout=65)
        return result.returncode
    except (OSError, subprocess.TimeoutExpired) as error:
        print("Sample test failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
