#!/usr/bin/env python3
"""NorthStarCpp installable release builder.

CMake registered targets are the only source of application, SDK and plugin files.
No discovery/copying of unregistered build-directory DLLs is allowed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from zipfile import BadZipFile, ZipFile, ZIP_DEFLATED

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.dev.cli import ROOT, config_load, toolchain, build_path
from tools.deploy_qt import (deploy as deploy_qt, isolate_qt_runtime,
                             verified_extras, QtDeploymentError)
from tools.v8_provider.registry import registered_v8_runtime, stage_v8_provider
from tools.v8_provider.runtime_audit import audit_runtime
from tools.release_readiness_gate import (
    parse_v8_version, verified_v8_attestation, verified_clean_machine_report)

SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "comdlg32.dll", "comctl32.dll",
    "d3d11.dll", "d3d12.dll", "d3dcompiler_47.dll", "dbghelp.dll",
    "dwmapi.dll", "dxgi.dll", "gdi32.dll", "gdiplus.dll", "imm32.dll", "iphlpapi.dll",
    "kernel32.dll", "msvcrt.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll",
    "psapi.dll", "rpcrt4.dll", "secur32.dll", "setupapi.dll",
    "shell32.dll", "shlwapi.dll", "user32.dll", "userenv.dll",
    "version.dll", "winmm.dll", "ws2_32.dll", "wsock32.dll",
    "crypt32.dll", "wtsapi32.dll", "normaliz.dll", "winhttp.dll",
    "ucrtbase.dll", "msvcp140.dll", "vcruntime140.dll",
}
EXTERNAL_PREREQUISITES = {
    # Microsoft DirectX SDK utility runtime; not redistributable by this builder.
    "d3dx9_43.dll": "Microsoft DirectX End-User Runtimes (June 2010), for shader_generator.exe"
}
REQUIRED_BINARIES = ("northstarcpp_editor.exe", "northstarcpp_player.exe",
                     "northstarcpp_project_manager.exe",
                     "northstarcpp_camera_timeline_editor.exe",
                     "northstarcpp_asset_browser.exe", "bugtrap.exe")
COMPONENTS = ("Runtime", "SDK", "Tools", "Documentation", "Licenses")


class DistError(RuntimeError):
    pass

QT_APPLICATIONS = (
    "northstarcpp_project_manager.exe",
    "northstarcpp_camera_timeline_editor.exe",
    "northstarcpp_asset_browser.exe",
)
QT_OPTIONAL_APPLICATIONS = ("northstarcpp_editor_qt.exe",)


def qt_tool_path(package: Path, name: str) -> Path:
    """Locate standalone Qt process binaries without accepting search-path fallback."""
    if name not in QT_APPLICATIONS + QT_OPTIONAL_APPLICATIONS:
        raise DistError("Unsupported Qt tool name")
    manifest = json.loads((package / "qt.runtime.json").read_text("utf-8"))
    layout = manifest.get("layout", "flat")
    if layout not in ("flat", "process-isolated"):
        raise DistError("Unknown Qt runtime process layout")
    root = "bin/qt_tools" if layout == "process-isolated" else "bin"
    return confined(package, root + "/" + name)



def run(command: list[str], *, cwd: Path = ROOT, timeout: int | None = 240,
        silent: bool = False, env: dict[str, str] | None = None) -> None:
    if not silent:
        print("$", subprocess.list2cmdline(command), flush=True)
    try:
        if timeout is None:
            subprocess.run(command, cwd=cwd, check=True, env=env)
            return
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                                errors="replace", timeout=timeout, env=env)
    except subprocess.CalledProcessError as exc:
        raise DistError("Command failed (exit {}): {}".format(
            exc.returncode, subprocess.list2cmdline(command))) from exc
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise DistError("Cannot execute {}: {}".format(command[0], exc)) from exc
    if not silent or result.returncode:
        if result.stdout:
            print(result.stdout.rstrip()[-5000:], flush=True)
        if result.stderr:
            print(result.stderr.rstrip()[-2500:], file=sys.stderr, flush=True)
    if result.returncode:
        raise DistError("Command failed (exit {}): {}".format(
            result.returncode, subprocess.list2cmdline(command)))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def confined(package: Path, relative: str) -> Path:
    if not isinstance(relative, str) or not relative:
        raise DistError("Empty installed relative path")
    normalized = Path(relative.replace("/", os.sep))
    if normalized.is_absolute() or ".." in normalized.parts:
        raise DistError("Illegal path in package registry: " + relative)
    result = (package / normalized).resolve()
    if package.resolve() not in result.parents:
        raise DistError("Registry path escapes installation root: " + relative)
    return result


def parse_registry(package: Path) -> tuple[dict, dict[str, str]]:
    """Verify that all registered modules share the executable directory."""
    source = package / "northstarcpp.plugins.json"
    if not source.is_file():
        raise DistError("CMake module registry missing: " + str(source))
    info = json.loads(source.read_text(encoding="utf-8"))
    if info.get("engine") != "NorthStarCpp" or not isinstance(info.get("plugins"), list):
        raise DistError("Invalid module registry schema")
    if info.get("host_abi") != 1 or not info.get("engine_version"):
        raise DistError("Unsupported module ABI or missing engine version")

    executable_dir = package / "bin"
    seen_ids: set[str] = set()
    seen_paths: set[str] = set()
    defaults: dict[str, str] = {}
    for plugin in info["plugins"]:
        if not isinstance(plugin, dict) or any(
            field not in plugin for field in ("id", "capability", "provider", "host_abi", "default")
        ):
            raise DistError("Incomplete plugin registry entry")
        if plugin["id"] in seen_ids:
            raise DistError("Duplicated registered plugin")
        seen_ids.add(plugin["id"])
        if plugin["host_abi"] != info["host_abi"]:
            raise DistError("Incompatible ABI for plugin " + plugin["id"])
        capability = str(plugin["capability"])
        if not re.fullmatch(r"engine\.[a-z][a-z0-9_]*", capability):
            raise DistError("Unsupported capability: " + capability)
        kind = plugin.get("kind", "external")
        if kind == "builtin" and "path" not in plugin:
            provider = "builtin"
        elif kind == "external" and isinstance(plugin.get("path"), str):
            relative = plugin["path"]
            if relative.casefold() in seen_paths:
                raise DistError("Duplicated registered plugin path")
            seen_paths.add(relative.casefold())
            path = confined(package, relative)
            if not path.is_file() or path.suffix.lower() != ".dll":
                raise DistError("Registered plugin DLL not installed: " + str(path))
            if path.name.lower().startswith("northstarcpp_"):
                raise DistError("Registered plugin still uses the obsolete output prefix: " + path.name)
            if path.parent.resolve() != executable_dir.resolve():
                raise DistError("Plugin must be beside the executable: " + str(path))
            provider = path.name
        else:
            raise DistError("Invalid registered module kind or path")
        if plugin["default"]:
            if capability in defaults:
                raise DistError("Multiple preferred implementations for " + capability)
            defaults[capability] = provider
    if not info["plugins"]:
        raise DistError("No modules registered")
    v8_libs = registered_v8_runtime(package)
    qt_libs = verified_extras(package, v8_libs)
    nonplugins = qt_libs | {Path(x).name.casefold() for x in v8_libs}
    on_disk = {p.relative_to(package).as_posix().casefold()
               for p in executable_dir.glob("*.dll")
               if p.name.casefold() not in nonplugins}
    if on_disk != seen_paths:
        raise DistError("Module directory has missing or unregistered DLLs: " +
                        str(sorted(on_disk.symmetric_difference(seen_paths))))
    for capability in ("engine.ecs", "engine.render", "engine.audio", "engine.input", "engine.physics", "engine.assets", "engine.logger", "engine.perlin"):
        if not any(p["capability"] == capability for p in info["plugins"]):
            raise DistError("Required capability is missing: " + capability)
    if (executable_dir / "northstarcpp.modules").exists():
        raise DistError("Deprecated northstarcpp.modules file included in release")
    # bootstrap.json is optional. Without it, all capabilities resolve from bin/ (./).
    return info, defaults


def check_binary_imports(package: Path, objdump: str) -> dict[str, list[dict]]:
    binaries = [
        qt_tool_path(package, name) if name in QT_APPLICATIONS
        else package / "bin" / name
        for name in REQUIRED_BINARIES
    ]
    for name in QT_OPTIONAL_APPLICATIONS:
        optional = qt_tool_path(package, name)
        if optional.is_file():
            binaries.append(optional)
    tool = package / "tools" / "shader_generator.exe"
    if tool.is_file():
        binaries.append(tool)
    world_generator = package / "tools" / "perlin_world_generator.exe"
    if not world_generator.is_file():
        raise DistError("Native Perlin world generator is missing")
    binaries.append(world_generator)
    binaries.extend(sorted((package / "bin").rglob("*.dll")))
    report: dict[str, list[dict]] = {}
    for executable in binaries:
        if not executable.is_file():
            raise DistError("Required binary absent: " + str(executable))
        try:
            result = subprocess.run([objdump, "-p", str(executable)],
                capture_output=True, text=True, errors="replace", timeout=25)
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise DistError("Cannot analyze imports: " + str(exc)) from exc
        if result.returncode:
            raise DistError("objdump could not inspect " + str(executable))
        dependencies = sorted(set(re.findall(r"DLL Name:\s*(\S+)", result.stdout, re.I)), key=str.casefold)
        data = []
        for imported in dependencies:
            lower = imported.lower()
            win_system = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32" / imported
            if lower in SYSTEM_DLLS or win_system.is_file() or lower.startswith(
                ("api-ms-win-", "ext-ms-win-")):
                status = "windows_system"
            elif lower in EXTERNAL_PREREQUISITES and executable == tool:
                status = "external_prerequisite"
            elif (executable.parent / imported).is_file():
                status = "bundled"
            elif (package / "bin/qt_tools" in executable.parents and
                  (package / "bin/qt_tools" / imported).is_file()):
                status = "bundled"  # Qt plugin loads its own Qt DLL root.
            elif (package / "bin" / imported).is_file() and (
                    executable.parent == package / "bin" or
                    package / "bin" in executable.parents or
                    executable.parent == package / "tools"):
                # Qt image/platform plugins in bin/<plugin-type>/ resolve
                # MinGW/Qt imports from the process root bin directory.
                status = "bundled"
            else:
                raise DistError("Unresolved/unsupported DLL import in {}: {}"
                                .format(executable.name, imported))
            data.append({"name": imported, "status": status})
        report[executable.relative_to(package).as_posix()] = data
    return report


def package_smoke(package: Path) -> None:
    # Exercise the installed app + runtime module paths, not build directory DLLs.
    bin_dir = package / "bin"
    run([str(bin_dir / "bugtrap.exe"), "--self-test"], cwd=bin_dir, timeout=15, silent=True)
    with tempfile.TemporaryDirectory(prefix="northstarcpp-scene-") as temporary:
        scene = Path(temporary) / "dist_smoke.nscn"
        environment = os.environ.copy()
        for key in ("NORTHSTARCPP_MODULES", "NORTHSTARCPP_PHYSICS_PROVIDER", "NORTHSTARCPP_ECS_PLUGIN",
                    "NORTHSTARCPP_PHYSICS_PLUGIN", "NORTHSTARCPP_RENDER_PLUGIN", "NORTHSTARCPP_LOGGER_PLUGIN",
                    "NORTHSTARCPP_AUDIO_PLUGIN", "NORTHSTARCPP_INPUT_PLUGIN", "NORTHSTARCPP_ASSETS_PLUGIN",
                    "NORTHSTARCPP_PERLIN_PLUGIN"):
            environment.pop(key, None)
        run([str(bin_dir / "northstarcpp_editor.exe"), "--warp", "--smoke",
             "--smoke-output", str(scene)], cwd=bin_dir, timeout=45, silent=True, env=environment)
        if not scene.is_file():
            raise DistError("Packaged editor did not write a test scene")
        run([str(bin_dir / "northstarcpp_player.exe"), "--warp", "--smoke",
             "--scene", str(scene)], cwd=bin_dir, timeout=45, silent=True, env=environment)
        shared_assets = package / "sharedAssets"
        if (shared_assets / "models" / "skydome.ydd").is_file():
            run([str(bin_dir / "northstarcpp_player.exe"), "--check-shared-assets", str(shared_assets)],
                cwd=bin_dir, timeout=30, silent=True, env=environment)
            run([str(bin_dir / "northstarcpp_player.exe"), "--warp", "--check-scene",
                 "--scene", str(scene), "--shared-assets", str(shared_assets),
                 "--asset-zones", str(ROOT / "tests" / "fixtures" / "asset_zones.json")],
                cwd=bin_dir, timeout=45, silent=True, env=environment)
        environment["LOCALAPPDATA"] = temporary
        isolated_qt = environment.copy()
        qt_dir = qt_tool_path(package, QT_APPLICATIONS[0]).parent
        isolated_qt["PATH"] = os.pathsep.join([
            str(qt_dir), str(Path(os.environ.get("SystemRoot","C:/Windows")) / "System32"),
            os.environ.get("SystemRoot","C:/Windows")])
        isolated_qt.pop("QT_PLUGIN_PATH", None)
        isolated_qt.pop("QT_QPA_PLATFORM_PLUGIN_PATH", None)
        run([str(qt_tool_path(package, "northstarcpp_project_manager.exe")), "--smoke"],
            cwd=qt_dir, timeout=30, silent=True, env=isolated_qt)
        run([str(qt_tool_path(package, "northstarcpp_camera_timeline_editor.exe")), "--self-test"],
            cwd=qt_dir, timeout=30, silent=True, env=isolated_qt)
        # The native Qt browser is a required installed editor component.
        run([str(qt_tool_path(package, "northstarcpp_asset_browser.exe")), "--smoke"],
            cwd=qt_dir, timeout=30, silent=True, env=isolated_qt)
        for name in QT_OPTIONAL_APPLICATIONS:
            executable = qt_tool_path(package, name)
            if executable.is_file():
                run([str(executable), "--smoke"], cwd=qt_dir,
                    timeout=30, silent=True, env=isolated_qt)


def sdk_smoke(package: Path, cmake: str, ninja: str, gpp: str) -> None:
    # Verify another project can consume the installed SDK without checkout paths.
    public_headers = package / "sdk" / "include" / "northstarcpp"
    if (public_headers / "compat").exists():
        raise DistError("SDK contains the private compat source tree")
    with tempfile.TemporaryDirectory(prefix="northstarcpp-sdk-") as temp:
        source = Path(temp)
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.21)\n"
            "project(Consumer LANGUAGES CXX)\n"
            "find_package(NorthStarCpp CONFIG REQUIRED)\n"
            "add_executable(consumer main.cpp)\n"
            "target_link_libraries(consumer PRIVATE NorthStarCpp::northstarcpp_runtime)\n",
            encoding="utf-8"
        )
        (source / "main.cpp").write_text(
            "#include <northstarcpp/collision_queries.hpp>\n"
            "#include <northstarcpp/geometry.hpp>\n"
            "#include <northstarcpp/math.hpp>\n"
            "#include <northstarcpp/perlin.hpp>\n"
            "#include <northstarcpp/physics.hpp>\n"
            "#include <northstarcpp/scene.hpp>\n"
            "int main() {\n"
            "    auto scene = northstarcpp::make_startup_scene();\n"
            "    using namespace northstarcpp::math;\n"
            "    auto transform = create_translation({0, 2, 0});\n"
            "    auto cube = northstarcpp::geometry::make_mesh(\n"
            "        northstarcpp::geometry::SolidPrimitive::cube, {1, 1, 1}, transform);\n"
            "    aabb bounds;\n"
            "    bounds.identity();\n"
            "    auto hit = northstarcpp::collision::ray_box_entry(bounds, {0, 0, -2}, {0, 0, 1}, 5);\n"
            "    northstarcpp::PerlinNoise procedural({42u, 17u, 31u, 13u});\n"
            "    float generated = procedural.sample2(1.25, 2.75);\n"
            "    float waveform = procedural.sample1(1.25);\n"
            "    return scene.objects.empty() || cube.vertices.size() != 8 ||\n"
            "        !similar(cube.vertices.front(), {-1, 3, -1}) || !hit || *hit != 1.5f ||\n"
            "        generated != procedural.sample2(18.25, 2.75) ||\n"
            "        waveform != procedural.sample1(18.25);\n"
            "}\n",
            encoding="utf-8"
        )
        output = source / "build"
        run([cmake, "-S", str(source), "-B", str(output), "-G", "Ninja",
             "-DCMAKE_PREFIX_PATH=" + str(package / "sdk"),
             "-DCMAKE_CXX_COMPILER=" + gpp,
             "-DCMAKE_MAKE_PROGRAM=" + ninja], timeout=70, silent=True)
        run([cmake, "--build", str(output), "--parallel", "2"], timeout=70, silent=True)
        sdk_environment = os.environ.copy()
        perlin = package / "bin" / "perlin_improved.dll"
        if perlin.is_file():  # Required only by older distributions.
            sdk_environment["NORTHSTARCPP_PERLIN_PLUGIN"] = str(perlin)
        else:
            sdk_environment.pop("NORTHSTARCPP_PERLIN_PLUGIN", None)
        run([str(output / "consumer.exe")], cwd=output, timeout=15,
            silent=True, env=sdk_environment)


def manifest_for(package: Path, plugins: dict, import_report: dict, build_type: str,
                 tests_passed: bool) -> dict:
    files = {}
    for p in sorted(package.rglob("*")):
        if p.is_file() and p.name != "manifest.json":
            key = p.relative_to(package).as_posix()
            files[key] = {"sha256": sha256(p), "size": p.stat().st_size}
    return {
        "schema_version": 1,
        "engine": "NorthStarCpp",
        "version": plugins["engine_version"],
        "target": "windows-x64",
        "build_type": build_type,
        "host_abi": plugins["host_abi"],
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "tests_passed": tests_passed,
        "installed_runtime_smoke": True,
        "installed_sdk_consumer_smoke": True,
        "redistribution_notice": "Review rights to all original engine source code before public redistribution.",
        "plugins": plugins["plugins"],
        "imports": import_report,
        "files": files
    }


def verify_manifest(package: Path) -> dict:
    manifest = package / "manifest.json"
    if not manifest.is_file():
        raise DistError("No package manifest: " + str(manifest))
    data = json.loads(manifest.read_text(encoding="utf-8"))
    if data.get("engine") != "NorthStarCpp" or not isinstance(data.get("files"), dict):
        raise DistError("Unrecognized package manifest")
    actual = {p.relative_to(package).as_posix()
              for p in package.rglob("*") if p.is_file() and p != manifest}
    expected = set(data["files"])
    if expected != actual:
        raise DistError("Package file list differs: " +
                        str(sorted(expected.symmetric_difference(actual))[:20]))
    for name, recorded in data["files"].items():
        file = confined(package, name)
        if file.stat().st_size != recorded["size"] or sha256(file) != recorded["sha256"]:
            raise DistError("Package hash mismatch: " + name)
    plugins, _ = parse_registry(package)
    if plugins["plugins"] != data["plugins"]:
        raise DistError("Module registry differs from package manifest")
    return data


def archive_package(package: Path, output: Path, name: str) -> None:
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
        for file in sorted(package.rglob("*")):
            if file.is_file():
                archive.write(file, arcname=name + "/" + file.relative_to(package).as_posix())
    with ZipFile(output) as archive:
        failed_file = archive.testzip()
        if failed_file is not None:
            raise DistError("Archive integrity check failed: " + failed_file)


def release(args: argparse.Namespace) -> dict:
    if os.name != "nt":
        raise DistError("Current distributable target requires Windows MinGW-w64.")
    cfg = config_load()
    found = toolchain(cfg, required=True)
    cmake, ninja, gcc, gpp = (str(found[name]) for name in ("cmake", "ninja", "gcc", "g++"))
    triple = subprocess.run([gpp, "-dumpmachine"], capture_output=True,
                            text=True, errors="replace", timeout=15).stdout.strip()
    if not triple.startswith("x86_64-w64-mingw32"):
        raise DistError("Expected MinGW-w64 x64; found " + triple)
    if args.from_build:
        # Explicit fast path for an already-built tree, useful when another
        # developer owns the Ninja build process. Tests and package smoke are
        # still mandatory; no artifact is created from missing modules.
        build = (ROOT / args.from_build).resolve()
        outputs = (ROOT / cfg.get("build_root", "out")).resolve()
        if outputs not in build.parents or not (build / "CMakeCache.txt").is_file():
            raise DistError("--from-build must name an existing build beneath out/")
        cache = (build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
        if "CMAKE_BUILD_TYPE:STRING=" + args.build_type not in cache:
            raise DistError("--from-build configuration type does not match " + args.build_type)
        if "BUILD_TESTING:BOOL=ON" not in cache:
            raise DistError("--from-build must have BUILD_TESTING=ON; test-free builds are not releasable")
        if not (build / "northstarcpp.plugins.json").is_file():
            raise DistError("--from-build is missing the generated CMake registry")
        # A stale --from-build can run old codecs against newly serialized
        # scenes and report misleading CTest failures or false positives.
        # Dry-run the ENTIRE Ninja graph before accepting reused binaries.
        stale = subprocess.run(
            [ninja, "-C", str(build), "-n", "all"],
            capture_output=True, text=True, errors="replace", timeout=40)
        if stale.returncode or re.search(r"^\[\d+/\d+\]", stale.stdout, re.M):
            raise DistError(
                "--from-build is stale: regenerate/rebuild Ninja all targets "
                "before packaging (pending build work detected)")
        print("Using existing CMake build:", build, flush=True)
    else:
        # A fresh uniquely named build tree prevents lock/contention with
        # concurrent engine development and stale Ninja state.
        builds = ROOT / cfg.get("build_root", "out") / "distribution"
        builds.mkdir(parents=True, exist_ok=True)
        build = Path(tempfile.mkdtemp(prefix="gcc-{}-".format(args.build_type.lower()),
                                      dir=builds))
        print("NorthStarCpp distribution build:", args.build_type, flush=True)
        run([cmake, "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=" + args.build_type,
             "-DCMAKE_C_COMPILER=" + gcc, "-DCMAKE_CXX_COMPILER=" + gpp,
             "-DCMAKE_MAKE_PROGRAM=" + ninja, "-DBUILD_TESTING=ON"], timeout=100)
        run([cmake, "--build", str(build), "--target", "all", "--parallel", str(args.jobs)],
            timeout=None)
    ctest = str(Path(cmake).with_name("ctest.exe"))
    listed = subprocess.run(
        [ctest, "--test-dir", str(build), "--show-only=json-v1"],
        capture_output=True, text=True, errors="replace", timeout=45)
    if listed.returncode:
        raise DistError("CTest test discovery failed")
    try:
        discovered = json.loads(listed.stdout)["tests"]
    except (KeyError, ValueError, TypeError) as exc:
        raise DistError("CTest did not return a valid test inventory") from exc
    if not isinstance(discovered, list) or len(discovered) < 10:
        raise DistError("Release must have at least 10 registered CTests")
    run([ctest, "--test-dir", str(build), "--output-on-failure",
         "--parallel", str(args.jobs)], timeout=None)
    dist = args.output_dir
    dist.mkdir(exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".northstarcpp-stage-", dir=dist))
    temp_zip: Path | None = None
    try:
        for component in COMPONENTS:
            run([cmake, "--install", str(build), "--prefix", str(stage),
                 "--config", args.build_type, "--component", component], timeout=110)
        # Qt Widgets requires a distributable DLL/Windows-platform plugin set.
        # Keep this separate from the provider registry consumed by games.
        deploy_qt(stage, gpp, SYSTEM_DLLS)
        if args.v8_provider_dir:
            # The provider must have independently reviewed, signed source
            # provenance and binary identity. Editable version strings alone
            # cannot authorize a public release.
            header = args.v8_sdk_root / "include/v8-version.h"
            if not header.is_file():
                raise DistError("V8 SDK version header missing")
            approved, detail = verified_v8_attestation(
                args.v8_attestation, parse_v8_version(header),
                args.v8_provider_dir / "scripting_v8.dll")
            if not approved:
                raise DistError("V8 security approval required: " + detail)
            objdump_exe = Path(gpp).with_name("objdump.exe")
            if not objdump_exe.is_file():
                raise DistError("Cannot audit Qt/V8 DLL imports: objdump unavailable")
            compatibility = audit_runtime(stage / "bin", args.v8_provider_dir, objdump_exe)
            # Preserve the differential hashes as audit evidence. Qt is
            # moved into a process-private directory before V8's CRT/ICU DLLs
            # are installed in the native Player directory.
            isolated = isolate_qt_runtime(stage)
            if isolated.get("layout") != "process-isolated":
                raise DistError("Qt process isolation failed")
            (stage / "qt-v8-collision-audit.json").write_text(
                json.dumps(compatibility, indent=2) + "\n", encoding="utf-8")
            stage_v8_provider(stage, args.v8_provider_dir, args.v8_sdk_root)
            evidence = stage / "attestations"
            evidence.mkdir()
            shutil.copy2(args.v8_attestation, evidence / "v8-security-approval.json")
            shutil.copy2(args.v8_attestation.with_name(
                args.v8_attestation.name + ".sig"),
                evidence / "v8-security-approval.json.sig")
            smoke = build / "northstarcpp_scripting_smoke.exe"
            if not smoke.is_file():
                raise DistError("Native V8 validation requires the host scripting smoke executable")
            env = os.environ.copy()
            env["NORTHSTARCPP_SCRIPTING_PLUGIN"] = str(stage / "bin/scripting_v8.dll")
            env["PATH"] = str(stage / "bin") + os.pathsep + env.get("PATH", "")
            run([str(smoke), "v8", str(stage / "bin/scripting_v8.dll"),
                 str(ROOT / "tests/fixtures")],
                cwd=stage / "bin", timeout=40, env=env, silent=True)
        plugins, defaults = parse_registry(stage)
        version = str(plugins["engine_version"])
        if not re.fullmatch(r"\d+\.\d+\.\d+(?:[-.][A-Za-z0-9.]+)?", version):
            raise DistError("Unsafe/unrecognized engine version in CMake registry")
        name = "NorthStarCpp-{}-windows-x64-{}".format(version, args.build_type)
        package_smoke(stage)
        sdk_smoke(stage, cmake, ninja, gpp)
        objdump = shutil.which("objdump") or str(Path(gpp).with_name("objdump.exe"))
        if not Path(objdump).is_file():
            raise DistError("GNU objdump is required to validate Windows DLL imports")
        imported = check_binary_imports(stage, objdump)
        notice = (
            "NorthStarCpp distribution - licensing review required\n\n"
            "The GNU/Windows package contains code adapted from earlier engine sources.\n"
            "Bundling a binary does not establish permission for its public distribution.\n"
            "Review original code provenance, rights and all transitive dependencies before release.\n"
            "Third-party notices: flecs.txt (MIT), bullet.txt (zlib), jolt.txt (MIT).\n"
            "The shader generator additionally requires Microsoft's D3DX9_43 DLL, typically\n"
            "installed using Microsoft's DirectX End-User Runtimes (June 2010).\n"
            "The Microsoft runtime is not included in this archive.\n"
            "The C++ Project Browser uses Qt6 DLLs (LGPL; see licenses/Qt6Base).\n"
            "Font Awesome Free icons use the SIL OFL (see licenses/FontAwesome).\n"
            "Qt DLLs can be replaced by end users; preserve notices and review LGPL compliance.\n"
        )
        (stage / "licenses" / "RELEASE_REVIEW_REQUIRED.txt").write_text(notice, encoding="utf-8")
        record = manifest_for(stage, plugins, imported, args.build_type, True)
        (stage / "manifest.json").write_text(
            json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        verify_manifest(stage)
        if not args.qa_only:
            raise DistError(
                "Direct publication disabled: use --qa-only, independent signed "
                "clean Windows acceptance, then --promote-qa")
        qa_final = dist / (name + "-QA")
        if qa_final.exists():
            raise DistError("QA candidate already exists; refusing replacement")
        stage.rename(qa_final)
        qa_verified = verify_manifest(qa_final)
        print("Non-shipping QA candidate assembled:", qa_final, flush=True)
        return {
            "qa_package": str(qa_final), "release_approved": False,
            "files": len(qa_verified["files"]),
            "requires": "Independent signed clean Windows VM acceptance",
            "tests_passed": True
        }
    finally:
        if stage.exists():
            shutil.rmtree(stage)
        if temp_zip is not None and temp_zip.exists():
            temp_zip.unlink()


def promote_qa(args: argparse.Namespace) -> dict:
    """Publish EXACT approved QA bytes, never rebuild after clean-VM testing."""
    qa = args.promote_qa.resolve(strict=True)
    output_root = (ROOT / "out").resolve()
    if output_root not in qa.parents or not qa.is_dir() or qa.is_symlink():
        raise DistError("QA candidate must be a real directory under out/")
    if not args.clean_machine_report:
        raise DistError("Independent signed --clean-machine-report is required")
    verified = verify_manifest(qa)
    script = qa / "bin/scripting_v8.dll"
    approval = qa / "attestations/v8-security-approval.json"
    if not script.is_file() or not approval.is_file():
        raise DistError("QA candidate has no approved V8 provider")
    data = json.loads(approval.read_text("utf-8"))
    version = data.get("v8_version", "")
    if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", version):
        raise DistError("Invalid approved V8 SDK version")
    allowed, detail = verified_v8_attestation(approval, version, script)
    if not allowed:
        raise DistError("QA V8 signature invalid: " + detail)
    try:
        qt = json.loads((qa / "qt.runtime.json").read_text("utf-8"))
    except (OSError, ValueError) as exc:
        raise DistError("Missing isolated Qt runtime registry") from exc
    if qt.get("layout") != "process-isolated" or verified_extras(qa):
        raise DistError("Cannot publish a shared Qt/V8 process directory")
    approved, reason = verified_clean_machine_report(args.clean_machine_report, qa)
    if not approved:
        raise DistError("Independent Windows acceptance rejected: " + reason)
    objdump = shutil.which("objdump")
    if not objdump:
        raise DistError("GNU objdump is required before publication")
    check_binary_imports(qa, objdump)
    version = verified["version"]
    name = "NorthStarCpp-{}-windows-x64-{}".format(
        version, verified["build_type"])
    final = args.output_dir / name
    publishing = args.output_dir / (".publishing-" + name)
    if final.exists() or publishing.exists():
        raise DistError("Published destination already exists; manual rollback required")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    publishing.mkdir()
    try:
        payload = publishing / name
        shutil.copytree(qa, payload, symlinks=False)
        verify_manifest(payload)
        if not args.no_zip:
            tmp_zip = publishing / (name + ".zip")
            archive_package(payload, tmp_zip, name)
        payload.rename(final)
        if not args.no_zip:
            os.replace(tmp_zip, args.output_dir / (name + ".zip"))
    finally:
        if publishing.exists():
            shutil.rmtree(publishing)
    return {
        "package": str(final), "release_approved": True,
        "clean_machine_acceptance": True, "files": len(verified["files"]),
        "source_qa_package": str(qa)
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build/test/package NorthStarCpp and all registered plugins")
    parser.add_argument("--build-type", choices=["Debug", "Release", "RelWithDebInfo"], default="Release")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--no-zip", action="store_true")
    parser.add_argument("--qa-only", action="store_true",
                        help="Build tested installable candidate in a non-shipping out/ directory")
    parser.add_argument("--promote-qa", type=Path,
                        help="Publish exact signed and independently tested QA candidate")
    parser.add_argument("--clean-machine-report", type=Path,
                        help="Detached-signature clean Windows VM acceptance report")
    parser.add_argument("--from-build", metavar="BUILD_DIR", help="Reuse tested existing CMake build")
    parser.add_argument("--v8-provider-dir", type=Path,
                        help="Optional V8 provider build bin directory (requires security-reviewed SDK)")
    parser.add_argument("--v8-sdk-root", type=Path,
                        help="Matching SDK root, with pkgconfig version and V8 license notices")
    parser.add_argument("--v8-attestation", type=Path,
                        help="Security-reviewed V8 binary approval JSON and detached .sig")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist",
                        help="Directory for the verified package and archive (default: dist)")
    parser.add_argument("--verify", metavar="PACKAGE", help="Verify a package under the output directory")
    args = parser.parse_args(argv)
    try:
        if args.jobs < 1 or args.jobs > 64:
            raise DistError("Build parallelism must be between 1 and 64")
        args.output_dir = args.output_dir.resolve()
        if bool(args.v8_provider_dir) != bool(args.v8_sdk_root):
            raise DistError("--v8-provider-dir and --v8-sdk-root must be specified together")
        if args.v8_provider_dir and not args.v8_attestation:
            raise DistError("V8 release requires a signed --v8-attestation")
        if args.v8_attestation and not args.v8_provider_dir:
            raise DistError("--v8-attestation requires --v8-provider-dir")
        if args.v8_attestation:
            args.v8_attestation = args.v8_attestation.resolve(strict=True)
        if args.v8_provider_dir:
            args.v8_provider_dir = args.v8_provider_dir.resolve(strict=True)
            args.v8_sdk_root = args.v8_sdk_root.resolve(strict=True)
        if not args.verify and not args.promote_qa:
            if not args.qa_only:
                raise DistError("Direct publication disabled: use --qa-only then --promote-qa")
            shipping = (ROOT / "dist").resolve()
            if args.output_dir == shipping or shipping in args.output_dir.parents:
                raise DistError("QA candidates cannot be written under production dist")
        if args.verify:
            package = Path(args.verify).resolve()
            if args.output_dir not in package.parents:
                raise DistError("Verification path must be a subdirectory of " + str(args.output_dir))
            data = verify_manifest(package)
            result = {"package": str(package), "files": len(data["files"]),
                      "plugins": len(data["plugins"]), "valid": True}
        elif args.promote_qa:
            if args.qa_only:
                raise DistError("--qa-only and --promote-qa cannot be combined")
            result = promote_qa(args)
        else:
            result = release(args)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except (DistError, BadZipFile, ValueError, KeyError, OSError, subprocess.TimeoutExpired) as exc:
        print("DIST ERROR: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
