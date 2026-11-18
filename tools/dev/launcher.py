#!/usr/bin/env python3
# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""NorthStarCpp native manager entry and non-GUI development commands."""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any
from .cli import ROOT, WINDOWS_APP_TARGETS, config_load, toolchain, build_path

from .projects import (LauncherError, Project, ProjectManager, MANIFEST, STARTUP_SCENE, _safe_scene)


@dataclass(frozen=True)
class RuntimeState:
    ready: bool
    reason: str
    executable: Path | None = None
    cwd: Path | None = None
    args: tuple[str, ...] = ()



def relative_path(root: Path, value: str, label: str) -> Path:
    if not isinstance(value, str) or not value:
        raise LauncherError(label + ": empty path.")
    raw = Path(value)
    if raw.is_absolute():
        raise LauncherError(label + ": use a repository-relative path.")
    path = (root / raw).resolve()
    if path != root.resolve() and root.resolve() not in path.parents:
        raise LauncherError(label + ": path outside repository.")
    return path


def native_app_state(root: Path, cfg: dict, target: str, project: Project | None) -> RuntimeState:
    if os.name != "nt":
        return RuntimeState(False, "The native NorthStarCpp editor and player require Windows.")
    try:
        kind = cfg.get("default_build_type", "Debug")
        if kind not in ("Debug", "Release", "RelWithDebInfo"):
            raise LauncherError("Invalid default build type.")
        out = relative_path(root, cfg.get("build_root", "out") + "/build",
                            "Native build directory")
        exe = out / (WINDOWS_APP_TARGETS[target] + ".exe")
        from tools.build_layout import configured_build_type
        actual = configured_build_type(out)
        if actual != kind:
            return RuntimeState(False, "Canonical out/build has a different configuration: {}. Run CMake configure for {}.".format(actual,kind))
        if not exe.is_file():
            return RuntimeState(False, f"Build the NorthStarCpp {target} with CMake/GCC first.")
        if project is None:
            return RuntimeState(True, "Native scene application available.", exe, root)
        if not _safe_scene(project.startup_scene):
            return RuntimeState(False, "Project startup scene path is unsafe.")
        base = project.directory.resolve()
        scene = (base / project.startup_scene).resolve()
        if base not in scene.parents:
            return RuntimeState(False, "Project scene escapes its directory.")
        if not project.can_open and target == "player":
            return RuntimeState(False, "Project is not ready: " + project.status_reason)
        if scene.is_file():
            game_args = ("--fps", "--scene", str(scene)) if (
                target == "player" and project.game_mode == "fps"
            ) else ("--scene", str(scene))
            return RuntimeState(True, "Project scene available.", exe, project.directory,
                                game_args)
        if target == "editor":
            return RuntimeState(True, "Save a starter scene to enable the player.", exe, project.directory,
                                ("--new-scene", str(scene)))
        return RuntimeState(False, "Open the project in the editor and save its scene first.")
    except (LauncherError, ValueError) as exc:
        return RuntimeState(False, str(exc))


def editor_state(root: Path = ROOT, cfg: dict | None = None,
                 project: Project | None = None) -> RuntimeState:
    return native_app_state(root.resolve(), cfg if cfg is not None else config_load(), "editor", project)


def runtime_state(root: Path = ROOT, cfg: dict | None = None,
                  project: Project | None = None) -> RuntimeState:
    root = root.resolve()
    cfg = cfg if cfg is not None else config_load()
    options = cfg.get("runtime", {})
    if not isinstance(options, dict):
        return RuntimeState(False, "Runtime configuration is invalid.")
    if not options.get("executable"):
        if options.get("native_scene_player"):
            return native_app_state(root, cfg, "player", project)
        return RuntimeState(
            False,
            "The playable engine is not compiled yet. GCC currently builds libraries, "
            "utilities and tests; the game/editor runtime is not available."
        )
    try:
        exe = relative_path(root, options["executable"], "Runtime executable")
        work_rel = options.get("working_directory") or str(exe.parent.relative_to(root))
        work = relative_path(root, work_rel, "Runtime working directory")
        required = options.get("required_paths", [])
        if not isinstance(required, list) or any(not isinstance(item, str) for item in required):
            raise LauncherError("runtime.required_paths must be a list of paths.")
        if not exe.is_file():
            return RuntimeState(False, "Game executable is missing: " + str(exe))
        if not work.is_dir():
            return RuntimeState(False, "Runtime working directory is missing: " + str(work))
        for item in required:
            dependency = relative_path(root, item, "Required asset path")
            if not dependency.exists():
                return RuntimeState(False, "Required runtime files missing: " + str(dependency))
        if exe.name.casefold() in {
            "shader_generator.exe", "northstarcpp_math_smoke.exe",
            "northstarcpp_portability_smoke.exe"
        }:
            return RuntimeState(False, "This executable is a development tool, not a game runtime.")
        raw_args = options.get("args", [])
        if not isinstance(raw_args, list) or any(not isinstance(a, str) for a in raw_args):
            raise LauncherError("runtime.args must be a list of strings.")
        if project is None and any("{project}" in arg for arg in raw_args):
            return RuntimeState(False, "Select a project to start the runtime.")
        arguments = tuple(a.replace("{project}", str(project.directory)) if project else a
                          for a in raw_args)
        return RuntimeState(True, "Configured game executable found.", exe, work, arguments)
    except (LauncherError, ValueError) as exc:
        return RuntimeState(False, str(exc))


def status(root: Path = ROOT, cfg: dict | None = None) -> dict[str, Any]:
    cfg = cfg if cfg is not None else config_load()
    if root.resolve() == ROOT:
        out = build_path(cfg, cfg.get("default_build_type", "Debug"))
    else:
        out = root / cfg.get("build_root", "out") / (
            "build")
    state = runtime_state(root, cfg)
    return {
        "engine": "NorthStarCpp",
        "toolchain": {key: bool(value) for key, value in toolchain(cfg).items()},
        "cmake_configured": (out / "CMakeCache.txt").exists(),
        "gcc_smoke_built": (out / ("northstarcpp_portability_smoke.exe" if os.name == "nt"
                                  else "northstarcpp_portability_smoke")).exists(),
        "projects": len(ProjectManager(root).list()),
        "editor_ready": editor_state(root, cfg).ready,
        "runtime_ready": state.ready,
        "runtime_reason": state.reason,
    }


def open_in_desktop(path: Path) -> None:
    if not path.exists():
        raise LauncherError("Path not found: " + str(path))
    if os.name == "nt":
        os.startfile(str(path))
    else:
        exe = shutil.which("open" if sys.platform == "darwin" else "xdg-open")
        if not exe:
            raise LauncherError("Desktop file browser is unavailable.")
        subprocess.Popen([exe, str(path)])


def launch(state: RuntimeState, physics_provider: str | None = None) -> int:
    if not state.ready or not state.executable or not state.cwd:
        raise LauncherError(state.reason)
    kwargs = {"cwd": str(state.cwd), "shell": False}
    if physics_provider is not None:
        provider = physics_provider.strip().lower()
        if provider not in {"bullet", "jolt"}:
            raise LauncherError("Unknown physics provider: " + physics_provider)
        environment = os.environ.copy()
        environment["NORTHSTARCPP_PHYSICS_PROVIDER"] = provider
        kwargs["env"] = environment
    process = subprocess.Popen([str(state.executable), *state.args], **kwargs)
    return process.pid


def open_native_project_manager() -> int:
    """Start the Win32 C++ manager. Never fall back to a Python GUI."""
    if os.name != "nt":
        raise LauncherError("The native project manager requires Windows.")
    entry = ROOT / "tools" / "dev" / "start.cmd"
    if not entry.is_file():
        raise LauncherError("Native project manager entry point is missing.")
    return subprocess.run(["cmd", "/d", "/c", str(entry)], cwd=ROOT, check=False).returncode


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="NorthStarCpp native C++ Project Manager launcher")
    commands = parser.add_mutually_exclusive_group()
    commands.add_argument("--status-json", action="store_true")
    commands.add_argument("--list-projects", action="store_true")
    commands.add_argument("--create-project", metavar="NAME")
    commands.add_argument("--start-runtime", action="store_true")
    commands.add_argument("--start-editor", action="store_true")
    commands.add_argument("--hub", action="store_true", help="Open native C++ Project Manager (compatibility alias)")
    commands.add_argument("--import-project", metavar="FOLDER")
    commands.add_argument("--scan-projects", metavar="FOLDER")
    parser.add_argument("--project", metavar="FOLDER",
                        help="Project to open for --start-editor / --start-runtime")
    parser.add_argument("--template", default="sandbox",
                        choices=("blank", "sandbox", "physics", "city", "architecture"))
    parser.add_argument("--location", metavar="FOLDER",
                        help="Parent directory for --create-project")
    parser.add_argument("--physics", choices=["bullet", "jolt"],
                        help="Physics provider for the editor or player")
    args = parser.parse_args(argv)
    try:
        if args.status_json:
            print(json.dumps(status(), ensure_ascii=False, indent=2))
        elif args.list_projects:
            for p in ProjectManager().list():
                print(p.name, "\t", p.directory)
        elif args.create_project is not None:
            print(ProjectManager().create(
                args.create_project, template=args.template,
                location=Path(args.location) if args.location else None).directory)
        elif args.start_editor:
            manager = ProjectManager()
            project = manager.get(Path(args.project)) if args.project else None
            if project and not project.can_open:
                raise LauncherError("Project cannot be opened: " + project.status_reason)
            pid = launch(editor_state(project=project), physics_provider=args.physics)
            if project:
                manager.mark_opened(project)
            print("Started editor PID:", pid)
        elif args.import_project:
            item = ProjectManager().register(Path(args.import_project))
            print("Registered:", item.name, item.directory)
        elif args.scan_projects:
            for item in ProjectManager().scan(Path(args.scan_projects)):
                print("Registered:", item.name, item.directory)
        elif args.hub:
            return open_native_project_manager()
        elif args.start_runtime:
            manager = ProjectManager()
            project = manager.get(Path(args.project)) if args.project else None
            if project and not project.can_open:
                raise LauncherError("Project cannot be opened: " + project.status_reason)
            pid = launch(runtime_state(project=project), physics_provider=args.physics)
            if project:
                manager.mark_opened(project)
            print("Started runtime PID:", pid)
        else:
            return open_native_project_manager()
        return 0
    except (LauncherError, ImportError, OSError, RuntimeError, ValueError) as exc:
        print("NorthStarCpp: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
