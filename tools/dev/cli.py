#!/usr/bin/env python3
# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""NorthStarCpp GNU GCC developer shell, Python 3.9+, standard library only."""
from __future__ import annotations
import argparse
from collections import Counter
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "northstarcpp.json"
LOGS = ROOT / ".northstarcpp" / "logs"
WINDOWS_APP_TARGETS = {"editor": "northstarcpp_editor", "player": "northstarcpp_player"}
PORTED_TARGETS = ("northstarcpp_portability_smoke",)
if os.name == "nt":
    PORTED_TARGETS += ("shader_generator", "northstarcpp_math", "northstarcpp_runtime", *WINDOWS_APP_TARGETS.values())


class ShellError(RuntimeError):
    pass


def config_load() -> dict:
    try:
        return json.loads(CONFIG.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ShellError("Configuration error: " + str(exc)) from exc


def inside(relative: str) -> Path:
    path = (ROOT / relative).resolve()
    if path != ROOT and ROOT not in path.parents:
        raise ShellError("Path escapes project root: " + relative)
    return path


def find_exe(name: str, cfg: dict) -> str | None:
    specific = cfg.get("toolchain", {}).get(name, "")
    if specific:
        path = Path(specific)
        return str(path) if path.is_file() else shutil.which(specific)
    found = shutil.which(name)
    if found:
        return found
    fallback = Path(cfg.get("toolchain", {}).get("bin_dir", r"C:\msys64\mingw64\bin"))
    return str(fallback / (name + ".exe")) if (fallback / (name + ".exe")).is_file() else None


def toolchain(cfg: dict, required: bool = False) -> dict[str, str | None]:
    result = {name: find_exe(name, cfg) for name in ("gcc", "g++", "cmake", "ninja")}
    missing = [name for name, path in result.items() if not path]
    if required and missing:
        raise ShellError("Missing tools: {}. Use 'doctor'.".format(", ".join(missing)))
    return result


def version(path: str | None) -> str:
    if not path:
        return "missing"
    try:
        r = subprocess.run([path, "--version"], capture_output=True, text=True,
                           errors="replace", timeout=10)
        return (r.stdout or r.stderr).splitlines()[0]
    except (OSError, IndexError, subprocess.TimeoutExpired):
        return "unknown"


def build_type(args: argparse.Namespace, cfg: dict) -> str:
    return getattr(args, "build_type", None) or cfg.get("default_build_type", "Debug")


def build_path(cfg: dict, kind: str) -> Path:
    if kind not in ("Debug", "Release", "RelWithDebInfo"):
        raise ShellError("Unsupported build type: " + kind)
    return inside(cfg.get("build_root", "out") + "/gcc-" + kind.lower())


def invoke(command: list[str], cwd: Path = ROOT, dry: bool = False, logged: bool = True) -> int:
    line = subprocess.list2cmdline(command)
    print("$ " + line, flush=True)
    if dry:
        return 0
    try:
        if not logged:
            return subprocess.run(command, cwd=cwd, check=False).returncode
        LOGS.mkdir(parents=True, exist_ok=True)
        logfile = LOGS / ("{}-{}.log".format(
            Path(command[0]).stem, datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")))
        with logfile.open("w", encoding="utf-8") as handle:
            handle.write("$ " + line + "\n\n")
            process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True, errors="replace")
            assert process.stdout
            try:
                for output in process.stdout:
                    print(output, end="", flush=True)
                    handle.write(output)
                code = process.wait()
            except KeyboardInterrupt:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                code = 130
        print("\nExit: {} | Log: {}".format(code, logfile))
        return code
    except OSError as exc:
        raise ShellError("Command failed: " + str(exc)) from exc


def doctor(cfg: dict, as_json: bool = False) -> int:
    checks = [
        ("CMake project", (ROOT / "CMakeLists.txt").is_file(), "NorthStarCpp GNU GCC project"),
        ("Native runtime sources", (ROOT / "engine" / "northstarcpp" / "CMakeLists.txt").is_file(),
         "Editor, player and runtime CMake modules"),
        ("Math source tree", (ROOT / "engine" / "northstarcpp" / "modules" / "foundation" / "northstarcpp" / "compat" / "core" / "sources").is_dir(),
         "Internal engine math implementation"),
    ]
    for name, path in toolchain(cfg).items():
        checks.append((name, bool(path), "{} | {}".format(path, version(path))))
    if as_json:
        print(json.dumps([{"check": name, "ok": ok, "detail": detail}
                          for name, ok, detail in checks], indent=2, ensure_ascii=False))
    else:
        for name, ok, detail in checks:
            print("[{}] {:<24} {}".format("OK" if ok else "FAIL", name, detail))
    return 0 if all(ok for _, ok, _ in checks) else 1


def status(cfg: dict, as_json: bool = False) -> int:
    output = build_path(cfg, cfg["default_build_type"])
    folder = ROOT / "Projects"
    project_count = sum(
        1 for entry in folder.iterdir()
        if entry.is_dir() and (entry / "northstarcpp.project.json").is_file()
    ) if folder.is_dir() else 0
    data = {
        "engine": "NorthStarCpp", "root": str(ROOT),
        "compiler": "GNU GCC / MinGW-w64",
        "tool_versions": {name: version(path) for name, path in toolchain(cfg).items()},
        "projects": project_count,
        "build_dir": str(output),
        "configured": (output / "CMakeCache.txt").is_file(),
        "ported_targets": list(PORTED_TARGETS),
        "full_engine_ported": False
    }
    if as_json:
        print(json.dumps(data, ensure_ascii=False, indent=2))
    else:
        for key, value in data.items():
            print("{:<24} {}".format(key + ":", value))
    return 0


def require_cache(cfg: dict, kind: str) -> Path:
    path = build_path(cfg, kind)
    if not (path / "CMakeCache.txt").exists():
        raise ShellError("Build tree not configured: {}. Use 'configure' first.".format(path))
    return path


def configure(cfg: dict, args: argparse.Namespace) -> int:
    found = toolchain(cfg, required=True)
    kind = build_type(args, cfg)
    command = [found["cmake"], "-S", str(ROOT), "-B", str(build_path(cfg, kind)),
               "-G", "Ninja", "-DCMAKE_BUILD_TYPE=" + kind,
               "-DCMAKE_C_COMPILER=" + found["gcc"],
               "-DCMAKE_CXX_COMPILER=" + found["g++"],
               "-DCMAKE_MAKE_PROGRAM=" + found["ninja"],
               "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    return invoke(command, dry=args.dry_run)


def build(cfg: dict, args: argparse.Namespace, rebuild: bool = False) -> int:
    if args.target == "engine" or (args.target in ("editor", "game", "player") and os.name != "nt"):
        raise ShellError("Target unavailable on this platform. Available: {}".format(
            ", ".join(PORTED_TARGETS)))
    found = toolchain(cfg, required=True)
    kind = build_type(args, cfg)
    path = build_path(cfg, kind) if args.dry_run else require_cache(cfg, kind)
    target = WINDOWS_APP_TARGETS.get(
        "player" if args.target == "game" else args.target, args.target)
    command = [found["cmake"], "--build", str(path), "--target", target,
               "--parallel", str(args.jobs or os.cpu_count() or 2)]
    if rebuild:
        command.append("--clean-first")
    return invoke(command, dry=args.dry_run)


def test(cfg: dict, args: argparse.Namespace) -> int:
    selected = argparse.Namespace(target="all", jobs=None,
                                  build_type=build_type(args, cfg), dry_run=args.dry_run)
    code = build(cfg, selected)
    if code:
        return code
    found = toolchain(cfg, required=True)
    cmake = Path(found["cmake"])
    ctest = shutil.which("ctest") or str(cmake.with_name("ctest.exe" if os.name == "nt" else "ctest"))
    return invoke([ctest, "--test-dir", str(build_path(cfg, selected.build_type)),
                   "--output-on-failure"], dry=args.dry_run)


def run(cfg: dict, args: argparse.Namespace) -> int:
    kind = build_type(args, cfg)
    if args.executable:
        executable = inside(args.executable)
    else:
        target = "player" if args.target == "game" else args.target
        name = ("northstarcpp_portability_smoke" if target == "smoke"
                else WINDOWS_APP_TARGETS.get(target, target))
        if os.name == "nt":
            name += ".exe"
        executable = build_path(cfg, kind) / name
    if not executable.is_file():
        raise ShellError("Executable not found: {}. Run 'build' first.".format(executable))
    relative_inputs = args.target == "shader_generator" or args.target in (
        "editor", "player", "game")
    cwd = Path.cwd() if relative_inputs and not args.executable else executable.parent
    extra = args.args[1:] if args.args and args.args[0] == "--" else args.args
    command = [str(executable), *extra]
    if args.detach and not args.dry_run:
        try:
            process = subprocess.Popen(command, cwd=cwd)
        except OSError as exc:
            raise ShellError("Launch failed: " + str(exc)) from exc
        print("Started PID {}".format(process.pid))
        return 0
    return invoke(command, cwd=cwd, dry=args.dry_run, logged=False)


def clean(cfg: dict, args: argparse.Namespace) -> int:
    path = build_path(cfg, build_type(args, cfg))
    if args.purge:
        if args.dry_run:
            print("[dry-run] Would delete " + str(path))
        elif path.is_dir():
            shutil.rmtree(path)
            print("Removed generated directory: " + str(path))
        else:
            print("No generated directory to remove.")
        return 0
    if not (path / "CMakeCache.txt").exists():
        print("Nothing to clean.")
        return 0
    found = toolchain(cfg, required=True)
    return invoke([found["cmake"], "--build", str(path), "--target", "clean"], dry=args.dry_run)


def audit(as_json: bool = False) -> int:
    patterns = {
        "pragma comment": r"#\s*pragma\s+comment\s*\(",
        "MSVC declspec": r"\b__declspec\s*\(",
        "MSVC compiler guard": r"\b_MSC_VER\b",
        "Win32 calling convention": r"\b__(?:stdcall|cdecl|fastcall)\b",
        "SEH or inline assembly": r"\b__(?:asm|try|except|finally)\b"
    }
    counts = Counter()
    total = 0
    for path in (ROOT / "engine" / "northstarcpp").rglob("*"):
        if path.suffix.lower() not in (".cpp", ".h", ".hpp", ".inl"):
            continue
        total += 1
        content = path.read_text(encoding="utf-8", errors="replace")
        for name, pattern in patterns.items():
            counts[name] += len(re.findall(pattern, content))
    report = {"files_scanned": total, "signals": dict(counts),
              "note": "Heuristic counts, not definitive compiler errors"}
    if as_json:
        print(json.dumps(report, indent=2))
    else:
        print("{} source/header files scanned".format(total))
        for name, count in counts.items():
            print("{:<32} {}".format(name + ":", count))
        print(report["note"])
    return 0


def show_logs(args: argparse.Namespace) -> int:
    files = sorted(LOGS.glob("*.log"), reverse=True) if LOGS.exists() else []
    if args.list:
        for path in files[:30]:
            print(path.name)
    elif files:
        print("== {} ==".format(files[0]))
        for line in files[0].read_text(encoding="utf-8", errors="replace").splitlines()[-args.tail:]:
            print(line)
    else:
        print("No logs yet.")
    return 0


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="northstarcpp", description="NorthStarCpp GNU GCC developer shell")
    sub = p.add_subparsers(dest="command", required=True)
    for name in ("doctor", "status", "audit"):
        sp = sub.add_parser(name)
        sp.add_argument("--json", action="store_true")
    sp = sub.add_parser("projects")
    sp.add_argument("--filter", default="")
    sub.add_parser("project-manager", help="Open the native C++ Project Manager")
    if os.name == "nt":
        sp = sub.add_parser("editor", help="Open the NorthStarCpp editor")
        sp.add_argument("--build-type", choices=["Debug", "Release", "RelWithDebInfo"])
        sp.add_argument("scene", nargs="?", help="Scene file to open")
    for name in ("configure", "build", "rebuild", "test", "clean", "run"):
        sp = sub.add_parser(name)
        sp.add_argument("--build-type", choices=["Debug", "Release", "RelWithDebInfo"])
        sp.add_argument("--dry-run", action="store_true")
        if name in ("build", "rebuild"):
            sp.add_argument("--target", default="all")
            sp.add_argument("--jobs", type=int)
        if name == "clean":
            sp.add_argument("--purge", action="store_true")
        if name == "run":
            targets = ["smoke"] + (["shader_generator", *WINDOWS_APP_TARGETS, "game"] if os.name == "nt" else [])
            sp.add_argument("--target", choices=targets, default="smoke")
            sp.add_argument("--executable", help="Repository-relative .exe path")
            sp.add_argument("--detach", action="store_true")
            sp.add_argument("args", nargs=argparse.REMAINDER)
    sp = sub.add_parser("logs")
    sp.add_argument("--list", action="store_true")
    sp.add_argument("--tail", type=int, default=50)
    sp = sub.add_parser("dist", help="Build, test and package NorthStarCpp with all registered plugins")
    sp.add_argument("--build-type", choices=["Debug", "Release", "RelWithDebInfo"], default="Release")
    sp.add_argument("--jobs", type=int, default=4)
    sp.add_argument("--no-zip", action="store_true")
    sp.add_argument("--from-build", metavar="BUILD_DIR")
    sp.add_argument("--verify", metavar="PACKAGE")
    game = sub.add_parser("game-dist", help="Package one project with the verified game runtime")
    game.add_argument("--project", metavar="FOLDER", help="Project folder or Projects/<name>")
    game.add_argument("--engine-package", metavar="FOLDER", help="Verified engine distribution")
    game.add_argument("--output-dir", metavar="FOLDER", default="dist/games")
    game.add_argument("--physics", choices=("bullet", "jolt"), default="bullet")
    game.add_argument("--allow-debug", action="store_true")
    game.add_argument("--no-zip", action="store_true")
    game.add_argument("--verify", metavar="FOLDER")
    sub.add_parser("selftest")
    sub.add_parser("menu")
    return p


def dispatch(cfg: dict, args: argparse.Namespace) -> int:
    name = args.command
    if name == "doctor":
        return doctor(cfg, args.json)
    if name == "status":
        return status(cfg, args.json)
    if name == "audit":
        return audit(args.json)
    if name == "projects":
        from .launcher import ProjectManager
        active_projects = [
            item for item in ProjectManager().list() if args.filter.casefold() in item.name.casefold()
        ]
        for item in active_projects:
            print("{:<34} {}".format(item.name, item.directory))
        print("{} NorthStarCpp project(s).".format(len(active_projects)))
        return 0
    if name == "project-manager":
        from .launcher import open_native_project_manager
        return open_native_project_manager()
    if name == "configure":
        return configure(cfg, args)
    if name == "build":
        return build(cfg, args)
    if name == "rebuild":
        return build(cfg, args, rebuild=True)
    if name == "test":
        return test(cfg, args)
    if name == "run":
        return run(cfg, args)
    if name == "editor":
        settings = argparse.Namespace(target="editor", executable=None, detach=False, dry_run=False,
                                      build_type=args.build_type, args=["--scene", args.scene] if args.scene else [])
        return run(cfg, settings)
    if name == "clean":
        return clean(cfg, args)
    if name == "logs":
        return show_logs(args)
    if name == "dist":
        command = [sys.executable, "-B", "-m", "tools.build_dist",
                   "--build-type", args.build_type, "--jobs", str(args.jobs)]
        if args.no_zip:
            command.append("--no-zip")
        if args.from_build:
            command.extend(["--from-build", args.from_build])
        if args.verify:
            command.extend(["--verify", args.verify])
        return invoke(command)
    if name == "game-dist":
        command = [sys.executable, "-B", "-m", "tools.build_game",
                   "--output-dir", args.output_dir, "--physics", args.physics]
        if args.project:
            command.extend(["--project", args.project])
        if args.engine_package:
            command.extend(["--engine-package", args.engine_package])
        if args.allow_debug:
            command.append("--allow-debug")
        if args.no_zip:
            command.append("--no-zip")
        if args.verify:
            command.extend(["--verify", args.verify])
        return invoke(command)
    if name == "selftest":
        return invoke([sys.executable, "-B", "-m", "unittest", "discover", "-s",
                       "tests", "-p", "test_*.py", "-v"])
    if name == "menu":
        entries = {
            "1": ("Status", ["status"]),
            "2": ("Doctor", ["doctor"]),
            "3": ("Configure GCC", ["configure"]),
            "4": ("Build GCC target", ["build"]),
            "5": ("CTest", ["test"]),
            "6": ("Run GCC smoke", ["run", "--target", "smoke"]),
            "7": ("NorthStarCpp projects", ["projects"]),
            "8": ("Portability audit", ["audit"]),
            "9": ("Logs", ["logs"]),
            "11": ("Release distributable", ["dist"]),
        }
        if os.name == "nt":
            entries["10"] = ("Open scene editor", ["editor"])
        while True:
            print("\n=== NorthStarCpp / GNU GCC ===")
            for k, (label, _) in entries.items():
                print("{}. {}".format(k, label))
            print("0. Exit")
            try:
                selected = input("Select: ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                return 0
            if selected == "0":
                return 0
            if selected in entries:
                try:
                    print("Result:", dispatch(cfg, parser().parse_args(entries[selected][1])))
                except ShellError as exc:
                    print("ERROR: " + str(exc))
            else:
                print("Unknown option")
    raise ShellError("Unknown command " + str(name))


def main(argv: list[str] | None = None) -> int:
    try:
        return dispatch(config_load(), parser().parse_args(argv))
    except (ShellError, OSError, ValueError) as exc:
        print("ERROR: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
