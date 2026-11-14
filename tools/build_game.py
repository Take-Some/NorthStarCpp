#!/usr/bin/env python3
# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""Build a self-contained Windows game package from a VERIFIED NorthStarCpp engine distribution.

This exports a binary .nscn game (legacy .nscene projects are importable) and assets. It does not compile arbitrary
C++ gameplay, execute scripts, cook textures, or waive redistribution rights.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
import sys
import tempfile
import struct
import zlib
from zipfile import ZipFile, ZIP_DEFLATED

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.build_dist import verify_manifest as verify_engine, parse_registry
from tools.dev.scene_binary import validate_nef8_scene, scene_entity_references

ROOT = Path(__file__).resolve().parents[1]
CAPABILITIES = frozenset({
    "engine.ecs", "engine.physics", "engine.render",
    "engine.audio", "engine.input", "engine.assets", "engine.logger", "engine.perlin"
})
MAX_FILE = 256 * 1024 * 1024
MAX_ASSETS = 1024 * 1024 * 1024


class GameBuildError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as src:
        for block in iter(lambda: src.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def is_link(path: Path) -> bool:
    return path.is_symlink() or (getattr(path, "is_junction", lambda: False)())


def checked_relative(root: Path, value: str) -> Path:
    if not isinstance(value, str) or not value or "\x00" in value or "\\" in value:
        raise GameBuildError("Invalid relative project file path")
    if ":" in value or value.startswith("/") or any(x in ("", ".", "..") for x in value.split("/")):
        raise GameBuildError("Project file path escapes its project root")
    path = root.joinpath(*value.split("/"))
    if any(is_link(parent) for parent in (path, *path.parents) if parent == root or root in parent.parents):
        raise GameBuildError("Project symlink/junction paths cannot be packaged")
    if root.resolve() not in path.resolve().parents:
        raise GameBuildError("Project file resolves outside its root")
    return path


def project_info(project: Path) -> tuple[str, Path, Path]:
    root = project.resolve(strict=True)
    if not root.is_dir():
        raise GameBuildError("Project is not a directory")
    file = root / "northstarcpp.project.json"
    if not file.is_file() or is_link(file):
        raise GameBuildError("Missing or redirected NorthStarCpp project manifest")
    info = json.loads(file.read_text(encoding="utf-8"))
    if not isinstance(info, dict) or info.get("engine") != "NorthStarCpp":
        raise GameBuildError("Unsupported game project manifest")
    name = info.get("name")
    if not isinstance(name, str) or not name.strip() or len(name.strip()) > 64:
        raise GameBuildError("Game name must contain 1-64 characters")
    default_scene = "main.nscn" if (root / "main.nscn").is_file() else "main.nscene"
    scene = checked_relative(root, info.get("startup_scene", default_scene))
    if scene.suffix.lower() not in (".nscn", ".nscene") or not scene.is_file():
        raise GameBuildError("Game startup scene is absent or has an unsupported extension")
    data = scene.read_bytes()
    if data[:4] == b"NEF8":
        try:
            validate_nef8_scene(data)
        except ValueError as exc:
            raise GameBuildError("Invalid NEF8 Scene ListFile") from exc
    elif data[:4] == b"NSCN":
        if len(data) < 16 or len(data) > 16*1024*1024:
            raise GameBuildError("Invalid binary scene size")
        major, minor, size, checksum = struct.unpack_from("<HHII", data, 4)
        if (major, minor) != (1, 0) or size != len(data)-16 or zlib.crc32(data[16:]) != checksum:
            raise GameBuildError("Invalid NSCN version, length or CRC32 checksum")
    else:
        header=data.split(b"\n", 1)[0].rstrip(b"\r")
        limit=16*1024*1024 if header==b"NorthStarCpp.Scene 3" else 512*1024
        if scene.suffix.lower() != ".nscene" or header not in (
                b"NorthStarCpp.Scene 1", b"NorthStarCpp.Scene 2", b"NorthStarCpp.Scene 3"
                ) or not 1 <= len(data) <= limit:
            raise GameBuildError("Unsupported or overlarge legacy scene")
    return name.strip(), root, scene


def game_mode(project: Path) -> str:
    info = json.loads((project / "northstarcpp.project.json").read_text(encoding="utf-8"))
    mode = info.get("game_mode", "scene")
    if mode not in ("scene", "fps", "explore"):
        raise GameBuildError("Unsupported game_mode in project")
    return mode


def scene_obj_refs(scene_file: Path) -> set[str]:
    """Check OBJ dependencies from NEF8 records and legacy text scenes."""
    raw = scene_file.read_bytes()
    if raw.startswith(b"NEF8"):
        refs: set[str] = set()
        try:
            entities = scene_entity_references(raw)
        except (ValueError, UnicodeError) as exc:
            raise GameBuildError("Invalid Scene ListFile entity component") from exc
        for asset, _ in entities:
            if not asset.lower().endswith(".obj"):
                continue
            path = asset if asset.startswith("assets/") else "assets/" + asset
            if (not path.startswith("assets/") or
                    any(v in ("", ".", "..") for v in path.split("/")) or
                    "\\" in path or ":" in path):
                raise GameBuildError("Unsafe OBJ resource in NEF8 scene: " + asset)
            refs.add(path)
        return refs
    if not raw.startswith(b"NorthStarCpp.Scene 3"):
        return set()
    refs: set[str] = set()
    for line in scene_file.read_text(encoding="utf-8-sig").splitlines():
        if not line.startswith("entity "):
            continue
        try:
            fields = shlex.split(line, posix=True)
        except ValueError as exc:
            raise GameBuildError("Malformed Scene V3 entity record") from exc
        if len(fields) < 8:
            raise GameBuildError("Incomplete Scene V3 entity record")
        asset = fields[7]
        if asset.endswith(".obj"):
            if (not asset.startswith("assets/models/") or
                    not asset.endswith(".obj")):
                raise GameBuildError("Unsupported OBJ scene resource: " + asset)
            refs.add(asset)
    return refs


def copy_game_assets(project: Path, target: Path, scene: Path) -> int:
    # Copy the validated startup scene and project assets. Project scripts
    # are separately staged and require a real scripting provider.
    (target / ("main" + scene.suffix.lower())).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(scene, target / ("main" + scene.suffix.lower()))
    # Model resources are referenced by VFS-relative path, never absolute paths.
    # Fail before packaging if a prefab/scene references a missing OBJ.
    for model_path in scene_obj_refs(scene):
        source = checked_relative(project, model_path)
        if not source.is_file() or is_link(source):
            raise GameBuildError("Missing or redirected OBJ scene resource: " + model_path)
    asset_root = project / "assets"
    if not asset_root.exists():
        return 0
    if not asset_root.is_dir() or is_link(asset_root):
        raise GameBuildError("Project assets directory is invalid or redirected")
    total = 0
    count = 0
    for entry in sorted(asset_root.rglob("*")):
        if is_link(entry):
            raise GameBuildError("Symlink/junction inside assets: " + str(entry))
        if entry.is_dir():
            continue
        if not entry.is_file():
            raise GameBuildError("Non-file asset encountered: " + str(entry))
        size = entry.stat().st_size
        total += size
        if size > MAX_FILE or total > MAX_ASSETS:
            raise GameBuildError("Game asset exceeds packaging size limit")
        relative = entry.relative_to(asset_root)
        dst = target / "assets" / relative
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry, dst)
        count += 1
    return count


def scene_script_refs(scene_file: Path) -> set[str]:
    refs: set[str] = set()
    raw = scene_file.read_bytes()
    if raw.startswith(b"NEF8"):
        try:
            entities = scene_entity_references(raw)
        except (ValueError, UnicodeError) as exc:
            raise GameBuildError("Invalid NEF8 Scene ListFile script component") from exc
        for _, script in entities:
            if not script:
                continue
            if (not script.startswith("scripts/") or
                    not script.endswith((".js", ".mjs")) or
                    "\\" in script or ":" in script or
                    any(part in ("", ".", "..") for part in script.split("/"))):
                raise GameBuildError("Invalid NEF8 script resource: " + script)
            refs.add(script)
        return refs
    if raw.startswith((b"NSCN", b"NEF8")):
        # NSCN v1 references require native importer validation; we still
        # package only the explicitly listed project scripts.
        return refs
    text = raw.decode("utf-8-sig")
    for line in text.splitlines():
        if not line.startswith("script "):
            continue
        try:
            words = shlex.split(line, posix=True)
        except ValueError as exc:
            raise GameBuildError("Malformed scene script component") from exc
        if len(words) != 3 or not words[1].isdigit():
            raise GameBuildError("Malformed scene script component")
        script = words[2]
        if (not script.startswith("scripts/") or
                not script.endswith((".js", ".mjs"))):
            raise GameBuildError("Invalid scene script resource: " + script)
        refs.add(script)
    return refs


def copy_game_scripts(project: Path, target: Path, scene: Path) -> int:
    root = project / "scripts"
    refs = scene_script_refs(scene)
    if not root.exists():
        if refs:
            raise GameBuildError("Scene scripts missing from project scripts directory")
        return 0
    if not root.is_dir() or is_link(root):
        raise GameBuildError("Invalid or redirected scripts directory")
    count = 0
    total = 0
    copied: set[str] = set()
    for entry in sorted(root.rglob("*")):
        if is_link(entry):
            raise GameBuildError("Redirected file in project scripts")
        if entry.is_dir():
            continue
        if not entry.is_file() or entry.suffix.lower() not in (".js", ".mjs"):
            raise GameBuildError("Project scripts directory may contain only .js/.mjs files")
        size = entry.stat().st_size
        total += size
        if size > 1024*1024 or total > 16*1024*1024 or count >= 1024:
            raise GameBuildError("Project scripting source exceeds package limits")
        relative = entry.relative_to(project).as_posix()
        if not checked_relative(project, relative).is_file():
            raise GameBuildError("Project script cannot be safely packaged")
        dst = target / relative
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry, dst)
        copied.add(relative)
        count += 1
    missing=refs-copied
    if missing:
        raise GameBuildError("Missing scene script files: " + ", ".join(sorted(missing)))
    return count


def validate_animation_clip_data(data: object, virtual_path: str) -> None:
    if not isinstance(data, dict) or type(data.get("schema")) is not int or data.get("schema") != 1 or (
        data.get("type") != "scene-transform"
    ) or data.get("interpolation", "linear") not in ("linear", "step"):
        raise GameBuildError("Unsupported animation schema: " + virtual_path)
    frames = data.get("keyframes")
    if not isinstance(frames, list) or not 2 <= len(frames) <= 8192:
        raise GameBuildError("Animation requires 2..8192 keys: " + virtual_path)
    previous = -1.0
    for item in frames:
        if not isinstance(item, dict):
            raise GameBuildError("Animation keyframe must be an object")
        time = item.get("time")
        if type(time) not in (int, float) or (
            time < 0 or time > 3600 or time <= previous or not math.isfinite(time)
        ):
            raise GameBuildError("Invalid animation keyframe timing: " + virtual_path)
        if previous < 0 and time != 0:
            raise GameBuildError("Animation must begin at time zero: " + virtual_path)
        previous = time
        for field, default, magnitude in (
            ("translation", [0, 0, 0], 100000),
            ("rotation", [0, 0, 0], 36000),
            ("scale", [1, 1, 1], 1000),
        ):
            vector = item.get(field, default)
            if not isinstance(vector, list) or len(vector) != 3 or any(
                type(x) not in (int, float) or abs(x) > magnitude or
                not math.isfinite(x) or (field == "scale" and x <= 0)
                for x in vector
            ):
                raise GameBuildError("Invalid animation " + field + ": " + virtual_path)


def copy_game_animations(project: Path, target: Path) -> int:
    """Stage bounded VFS scene-transform animation clips for game releases."""
    root = project / "animations"
    if not root.exists():
        return 0
    if not root.is_dir() or is_link(root):
        raise GameBuildError("Invalid or redirected animation directory")
    total = 0
    count = 0
    for file in sorted(root.rglob("*")):
        if is_link(file):
            raise GameBuildError("Redirected animation asset")
        if file.is_dir():
            continue
        if not file.is_file() or not file.name.endswith(".anim.json"):
            raise GameBuildError("Only .anim.json clips are supported under animations/")
        size = file.stat().st_size
        total += size
        if size < 1 or size > 1024*1024 or total > 16*1024*1024 or count >= 1024:
            raise GameBuildError("Animation assets exceed package limits")
        relative = file.relative_to(project).as_posix()
        if not checked_relative(project, relative).is_file():
            raise GameBuildError("Animation source is redirected or invalid")
        try:
            descriptor = json.loads(file.read_text(encoding="utf-8"))
        except (UnicodeError, json.JSONDecodeError) as exc:
            raise GameBuildError("Invalid animation JSON: " + relative) from exc
        validate_animation_clip_data(descriptor, relative)
        dst = target / relative
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, dst)
        count += 1
    return count


def selected_plugins(engine: Path, physics: str) -> tuple[dict, list[dict]]:
    info = verify_engine(engine)
    registry, _ = parse_registry(engine)
    plugins = [item for item in registry["plugins"]
               if (item["capability"] == "engine.physics" and item["provider"].casefold() == physics)
               or (item["capability"] != "engine.physics" and item["default"])]
    present = {item["capability"] for item in plugins}
    if not CAPABILITIES.issubset(present):
        raise GameBuildError("Engine distribution does not provide all mandatory capabilities: "
                             + ", ".join(sorted(CAPABILITIES - present)))
    if len(present) != len(plugins):
        raise GameBuildError("A game must have exactly one provider for each capability")
    if info.get("host_abi") != registry["host_abi"]:
        raise GameBuildError("Engine registry ABI mismatch")
    return info, plugins


def build_manifest(folder: Path, name: str, engine: dict, physics: str, plugins: list[dict],
                   assets: int, mode: str = "scene", scripts: int = 0,
                   animations: int = 0) -> None:
    files = {}
    for file in sorted(folder.rglob("*")):
        if file.is_file() and file.name != "manifest.json":
            relative = file.relative_to(folder).as_posix()
            files[relative] = {"size": file.stat().st_size, "sha256": sha256(file)}
    record = {
        "schema": 1, "format": "NorthStarCpp.Game",
        "game_name": name, "engine_version": engine["version"],
        "engine_build_type": engine["build_type"],
        "target": "windows-x64", "physics_provider": physics,
        "host_abi": engine["host_abi"],
        "selected_plugins": plugins, "asset_files": assets,
        "script_files": scripts,
        "animation_files": animations,
        "gameplay_scripting_supported": scripts > 0,
        "game_mode": mode,
        "scene_file": "game/main.nscn" if "game/main.nscn" in files else "game/main.nscene",
        "rights_review_required": True, "files": files,
    }
    (folder / "manifest.json").write_text(
        json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def verify_game(folder: Path) -> dict:
    folder = folder.resolve(strict=True)
    if not folder.is_dir():
        raise GameBuildError("Game package must be a directory")
    manifest = folder / "manifest.json"
    if not manifest.is_file() or is_link(manifest):
        raise GameBuildError("Missing game release manifest")
    data = json.loads(manifest.read_text(encoding="utf-8"))
    if data.get("game_mode", "scene") not in ("scene", "fps", "explore"):
        raise GameBuildError("Unsupported game mode in game distribution")
    if data.get("format") != "NorthStarCpp.Game" or data.get("schema") != 1:
        raise GameBuildError("Unsupported game release manifest")
    expected = data.get("files")
    if not isinstance(expected, dict):
        raise GameBuildError("Invalid game file map")
    existing = {}
    for file in folder.rglob("*"):
        if is_link(file):
            raise GameBuildError("Package symlink/junction detected")
        if file.is_file() and file != manifest:
            existing[file.relative_to(folder).as_posix()] = file
    if existing.keys() != expected.keys():
        raise GameBuildError("Game release file set mismatch")
    for relative, file in existing.items():
        meta = expected[relative]
        if not isinstance(meta, dict) or meta.get("size") != file.stat().st_size or meta.get("sha256") != sha256(file):
            raise GameBuildError("Game release checksum mismatch: " + relative)
    if not (folder / "bin/northstarcpp_player.exe").is_file():
        raise GameBuildError("Game executable is missing")
    if not (folder / "bin/bugtrap.exe").is_file():
        raise GameBuildError("Native crash reporter is missing")
    scene_path=data.get("scene_file", "game/main.nscene")
    if scene_path not in ("game/main.nscn", "game/main.nscene") or not (folder / scene_path).is_file():
        raise GameBuildError("Startup scene is missing or not declared correctly")
    if scene_path not in expected:
        raise GameBuildError("Startup scene missing from package manifest")
    plugins = data.get("selected_plugins")
    if not isinstance(plugins, list) or any(not isinstance(p, dict) for p in plugins):
        raise GameBuildError("Invalid game provider registry")
    available = {p.get("capability") for p in plugins}
    if not CAPABILITIES.issubset(available) or len(available) != len(plugins):
        raise GameBuildError("Required game capabilities are missing or duplicated")
    script_files=[path for path in expected
                  if path.startswith("game/scripts/") and
                  path.endswith((".js", ".mjs"))]
    declared_scripts=data.get("script_files",0)
    if (type(declared_scripts) is not int or declared_scripts != len(script_files) or
            bool(data.get("gameplay_scripting_supported",False)) != bool(script_files)):
        raise GameBuildError("Packaged scripting status or script file count mismatch")
    if script_files and "engine.scripting" not in available:
        raise GameBuildError("Packaged scripts require an engine.scripting provider")
    actual_animation_files = [path for path in expected
        if path.startswith("game/animations/") and path.endswith(".anim.json")]
    if (type(data.get("animation_files", 0)) is not int or
        data.get("animation_files", 0) != len(actual_animation_files)):
        raise GameBuildError("Packaged animation asset count mismatch")
    for plugin in plugins:
        kind = plugin.get("kind", "external")
        if kind == "builtin" and "path" not in plugin:
            continue
        if kind != "external":
            raise GameBuildError("Invalid game provider kind")
        path = plugin.get("path", "")
        if not isinstance(path, str) or not re.fullmatch(r"bin/[A-Za-z0-9_.-]+\.dll", path):
            raise GameBuildError("Unsafe plugin entry")
        if path not in expected:
            raise GameBuildError("Missing selected plugin in game package")
    if not (folder / "launch.cmd").is_file():
        raise GameBuildError("Game entry point missing")
    return data


def launch_smoke(folder: Path, mode: str = "scene") -> None:
    runtime = folder / "bin/northstarcpp_player.exe"
    scene = folder / ("game/main.nscn" if (folder / "game/main.nscn").exists()
                      else "game/main.nscene")
    env = os.environ.copy()
    for key in ("NORTHSTARCPP_ECS_PLUGIN", "NORTHSTARCPP_PHYSICS_PROVIDER",
                "NORTHSTARCPP_RENDER_PLUGIN", "NORTHSTARCPP_AUDIO_PLUGIN",
                "NORTHSTARCPP_INPUT_PLUGIN", "NORTHSTARCPP_ASSETS_PLUGIN", "NORTHSTARCPP_LOGGER_PLUGIN"):
        env.pop(key, None)
    command = [str(runtime), "--warp", "--check-scene", "--scene", str(scene)]
    if mode == "fps":
        command.append("--fps")
    elif mode == "explore":
        command.append("--explore")
    result = subprocess.run(command,
                            cwd=folder / "bin", env=env, capture_output=True,
                            text=True, errors="replace", timeout=55)
    if result.returncode:
        raise GameBuildError("Standalone player smoke failed: " +
                             (result.stderr + result.stdout)[-2500:])


def copy_bundled_runtime(engine: Path, bindir: Path, manifest: dict,
                         plugins: list[dict]) -> int:
    """Copy verified transitive DLL imports needed by the standalone player."""
    imports = manifest.get("imports", {})
    if not isinstance(imports, dict):
        raise GameBuildError("Invalid engine binary import registry")
    active = ["bin/northstarcpp_player.exe", "bin/bugtrap.exe"]
    active.extend(str(plugin["path"]) for plugin in plugins
                  if plugin.get("kind", "external") == "external")
    visited: set[str] = set()
    copied = 0
    while active:
        source_key = active.pop()
        if source_key.casefold() in visited:
            continue
        visited.add(source_key.casefold())
        rows = imports.get(source_key)
        if rows is None:
            if source_key.casefold() == "bin/scripting_v8.dll":
                raise GameBuildError("V8 plugin is missing verified native import metadata")
            continue
        if not isinstance(rows, list):
            raise GameBuildError("Malformed engine native import metadata")
        for dependency in rows:
            if not isinstance(dependency, dict):
                raise GameBuildError("Invalid native import record")
            if dependency.get("status") != "bundled":
                continue
            filename = dependency.get("name")
            if (not isinstance(filename, str) or
                    not re.fullmatch(r"[A-Za-z0-9_.+\-]+\.dll", filename, re.I)):
                raise GameBuildError("Unsafe native runtime dependency name")
            relative = "bin/" + filename
            source = engine / relative
            if not source.is_file() or relative not in manifest.get("files", {}):
                raise GameBuildError("Unverified native runtime dependency: " + filename)
            destination = bindir / filename
            if destination.exists():
                if sha256(destination) != sha256(source):
                    raise GameBuildError("Conflicting native runtime DLL: " + filename)
            else:
                shutil.copy2(source, destination)
                copied += 1
            active.append(relative)
    return copied


def release(project: Path, engine: Path, output: Path, physics: str, zip_package: bool,
            allow_debug: bool) -> dict:
    name, project, scene = project_info(project)
    mode = game_mode(project)
    engine = engine.resolve(strict=True)
    engine_manifest, plugins = selected_plugins(engine, physics)
    source_scripts=(project / "scripts").is_dir() and any(
        f.is_file() and f.suffix.lower() in (".js", ".mjs")
        for f in (project / "scripts").rglob("*"))
    if (source_scripts or scene_script_refs(scene)) and not any(
        p["capability"] == "engine.scripting" and
        p["provider"].casefold() == "v8" for p in plugins):
        raise GameBuildError("JavaScript game export requires an installed, selected V8 scripting provider")
    if engine_manifest.get("build_type") != "Release" and not allow_debug:
        raise GameBuildError("Final game export requires a Release engine; use --allow-debug only for tests")
    version = engine_manifest["version"]
    safe_name = re.sub(r"[^a-zA-Z0-9._-]+", "-", name).strip("-.")
    if not safe_name:
        safe_name = "NorthStarGame"
    base_name = f"{safe_name}-{version}-windows-x64-{engine_manifest['build_type']}-{physics}"
    output.mkdir(parents=True, exist_ok=True)
    final = output / base_name
    archive = output / (base_name + ".zip")
    if final.exists() or (zip_package and archive.exists()):
        raise GameBuildError("Game release already exists; refusing to overwrite: " + str(final))
    with tempfile.TemporaryDirectory(prefix=".game-stage-", dir=output) as staging:
        stage = Path(staging) / base_name
        stage.mkdir()
        bindir = stage / "bin"
        bindir.mkdir()
        shutil.copy2(engine / "bin/northstarcpp_player.exe",
                     bindir / "northstarcpp_player.exe")
        shutil.copy2(engine / "bin/bugtrap.exe", bindir / "bugtrap.exe")
        for item in plugins:
            if item.get("kind", "external") == "external":
                src = engine / item["path"]
                shutil.copy2(src, bindir / src.name)
        copy_bundled_runtime(engine, bindir, engine_manifest, plugins)
        # A scene is playable through the provider-neutral engine.assets API.
        count = copy_game_assets(project, stage / "game", scene)
        script_count = copy_game_scripts(project, stage / "game", scene)
        animation_count = copy_game_animations(project, stage / "game")
        flags = b"--fps " if mode == "fps" else (b"--explore " if mode == "explore" else b"")
        (stage / "launch.cmd").write_bytes(
            b'@echo off\r\nsetlocal\r\ncd /d "%~dp0bin"\r\n'
            + b'northstarcpp_player.exe ' + flags +
            b'--packaged-game --scene "%~dp0game\\main' +
            scene.suffix.lower().encode("ascii") + b'" %*\r\n'
            + b'exit /b %ERRORLEVEL%\r\n')
        (stage / "licenses").mkdir()
        for f in (engine / "licenses").rglob("*"):
            if is_link(f):
                raise GameBuildError("Redirected third-party license path")
            if f.is_file():
                relative=f.relative_to(engine / "licenses")
                target=stage / "licenses" / relative
                target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(f,target)
        (stage / "licenses" / "GAME_RIGHTS_REVIEW.txt").write_text(
            "Review the rights to historical NorthStarCpp-derived engine code and "
            "all game resources before public redistribution.\n"
            "JavaScript scripts require a compatible V8 provider; review game scripts before redistribution.\n",
            encoding="utf-8")
        build_manifest(stage, name, engine_manifest, physics, plugins,
                       count, mode, script_count, animation_count)
        verify_game(stage)
        launch_smoke(stage, mode)
        shutil.copytree(stage, final)
        verify_game(final)
        if zip_package:
            with ZipFile(archive, "w", ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
                for file in sorted(final.rglob("*")):
                    if file.is_file():
                        z.write(file, base_name + "/" + file.relative_to(final).as_posix())
            with ZipFile(archive) as z:
                if z.testzip() is not None:
                    raise GameBuildError("Game archive integrity check failed")
    return {"game": name, "mode": mode, "package": str(final), "archive": str(archive) if zip_package else None,
            "engine_build_type": engine_manifest["build_type"], "plugins": len(plugins),
            "assets": count, "scripts": script_count,
            "animations": animation_count, "standalone_smoke": True}


def main(argv: list[str] | None = None) -> int:
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--project", type=Path, help="Folder containing northstarcpp.project.json")
    cli.add_argument("--engine-package", type=Path, help="Verified NorthStarCpp engine distribution")
    cli.add_argument("--output-dir", type=Path, default=ROOT / "dist" / "games")
    cli.add_argument("--physics", choices=("bullet", "jolt"), default="bullet")
    cli.add_argument("--allow-debug", action="store_true")
    cli.add_argument("--no-zip", action="store_true")
    cli.add_argument("--verify", type=Path, metavar="GAME_DIR")
    opts = cli.parse_args(argv)
    try:
        if opts.verify:
            result = verify_game(opts.verify)
            print(json.dumps({"game": result["game_name"], "verified": True,
                              "files": len(result["files"])}, ensure_ascii=False, indent=2))
            return 0
        if not opts.project or not opts.engine_package:
            raise GameBuildError("--project and --engine-package are required")
        result = release(opts.project, opts.engine_package, opts.output_dir.resolve(),
                         opts.physics, not opts.no_zip, opts.allow_debug)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except (GameBuildError, ValueError, OSError, KeyError, subprocess.TimeoutExpired) as exc:
        print("GAME DIST ERROR: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
