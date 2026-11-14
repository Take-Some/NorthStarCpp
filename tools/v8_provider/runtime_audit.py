"""Compare V8 UCRT64 and Qt MINGW64 runtime DLLs before packaging.

A single Windows process has module name resolution constraints. Identically
named but different CRT/ICU DLLs must never be silently interchanged.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def dll_files(path: Path) -> dict[str, Path]:
    if not path.is_dir():
        raise ValueError("Runtime directory not found: " + str(path))
    files: dict[str, Path] = {}
    for file in path.iterdir():
        if file.is_file() and file.suffix.lower() == ".dll":
            if file.is_symlink():
                raise ValueError("Runtime file cannot be a symlink: " + str(file))
            key = file.name.casefold()
            if key in files:
                raise ValueError("Ambiguous case-insensitive runtime DLL name: " + key)
            files[key] = file
    return files


def _imports(binary: Path, objdump: Path) -> set[str]:
    process = subprocess.run(
        [str(objdump), "-p", str(binary)], capture_output=True,
        text=True, timeout=25, check=False,
    )
    if process.returncode:
        raise ValueError("Cannot parse PE imports of " + str(binary))
    names = set()
    for line in process.stdout.splitlines():
        if "DLL Name:" in line:
            names.add(line.split("DLL Name:", 1)[1].strip().casefold())
    return names


def audit_runtime(qt_dir: Path, v8_dir: Path, objdump: Path | None = None) -> dict:
    qt = dll_files(qt_dir)
    v8 = dll_files(v8_dir)
    if not (qt.get("qt6core.dll") or qt.get("qt5core.dll")):
        raise ValueError("Qt runtime is not present in Qt build directory")
    if "libv8.dll" not in v8 or "scripting_v8.dll" not in v8:
        raise ValueError("Native V8 runtime is incomplete")
    overlaps = sorted(set(qt) & set(v8))
    collisions = [
        {
            "dll": name,
            "qt_path": str(qt[name]),
            "v8_path": str(v8[name]),
            "qt_sha256": sha256(qt[name]),
            "v8_sha256": sha256(v8[name]),
        }
        for name in overlaps if sha256(qt[name]) != sha256(v8[name])
    ]
    report = {
        "schema": 1,
        "qt_root": str(qt_dir.resolve()),
        "v8_root": str(v8_dir.resolve()),
        "shared_dll_names": overlaps,
        "conflicts": collisions,
        "safe_to_merge": not bool(collisions),
    }
    if objdump is not None:
        qt_core = qt.get("qt6core.dll") or qt.get("qt5core.dll")
        report["qt_imports"] = sorted(_imports(qt_core, objdump))
        report["v8_imports"] = sorted(_imports(v8["libv8.dll"], objdump))
        report["shared_direct_imports"] = sorted(
            set(report["qt_imports"]) & set(report["v8_imports"]))
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-dir", required=True, type=Path)
    parser.add_argument("--v8-dir", required=True, type=Path)
    parser.add_argument("--objdump", type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    report = audit_runtime(args.qt_dir, args.v8_dir, args.objdump)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("DLL_COLLISIONS", len(report["conflicts"]))
    print("COLLIDING_NAMES", ", ".join(x["dll"] for x in report["conflicts"]))
    print("SAFE_TO_MERGE", report["safe_to_merge"])
    print("REPORT", args.report)
    return 0 if report["safe_to_merge"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
