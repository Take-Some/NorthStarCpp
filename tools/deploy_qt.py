#!/usr/bin/env python3
"""Stage Qt 6 runtime for the native Project Browser without a global Qt install.

Qt DLLs and platform plugins must be bundled separately from NorthStar engine
capabilities. Returns an audited file manifest for the distribution verifier.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

class QtDeploymentError(RuntimeError):
    pass


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def imported_dlls(binary: Path, objdump: Path) -> list[str]:
    p = subprocess.run([str(objdump), "-p", str(binary)], capture_output=True,
                       text=True, errors="replace", timeout=25)
    if p.returncode:
        raise QtDeploymentError("Cannot inspect Qt dependency: " + str(binary))
    return sorted({x.lower() for x in re.findall(
        r"DLL Name:\s*(\S+)", p.stdout, flags=re.IGNORECASE)})


def deploy(package: Path, gpp: str, system_dlls: set[str]) -> dict:
    bin_dir = package / "bin"
    browser = bin_dir / "northstarcpp_project_manager.exe"
    native_content = bin_dir / "northstarcpp_asset_browser.exe"
    if not browser.is_file():
        if not native_content.is_file():
            raise QtDeploymentError("No Qt project/content browser executable found")
        browser = native_content

    mingw = Path(gpp).resolve().parent
    tool = mingw / "windeployqt6.exe"
    objdump = mingw / "objdump.exe"
    if not tool.is_file() or not objdump.is_file():
        raise QtDeploymentError("Qt deploy tool / objdump missing from MinGW toolchain")

    # The isolated Runtime staging directory initially holds the engine
    # provider DLLs, before windeployqt contributes any Qt dependencies.
    # Capture all existing DLLs rather than enumerating historical providers:
    # newly registered capabilities must never be misclassified as Qt extras.
    before = {p.name.lower() for p in bin_dir.glob("*.dll")}
    environment = os.environ.copy()
    environment["PATH"] = str(mingw) + os.pathsep + environment.get("PATH", "")
    cmd = [str(tool), "--release", "--no-translations",
           "--no-system-d3d-compiler", "--no-system-dxc-compiler",
           "--no-opengl-sw", "--no-network",
           "--skip-plugin-types", "generic,iconengines,networkinformation,tls",
           "--no-compiler-runtime", "--dir", str(bin_dir), str(browser)]
    result = subprocess.run(cmd, cwd=bin_dir, capture_output=True, text=True,
                            errors="replace", timeout=120, env=environment)
    if result.returncode:
        raise QtDeploymentError("Qt6 deployment failed: " +
                                (result.stdout + result.stderr)[-4500:])

    # The native asset browser has an explicit QtConcurrent deployment
    # contract even when the linker eliminates unused QtConcurrent imports.
    # Seed that DLL before recursing so that its own imports are audited.
    if native_content.is_file():
        concurrent = mingw / "Qt6Concurrent.dll"
        if not concurrent.is_file():
            raise QtDeploymentError("QtConcurrent runtime is missing from MinGW")
        target = bin_dir / concurrent.name
        if not target.is_file():
            shutil.copy2(concurrent, target)

    # QtConcurrent::run may be entirely instantiated in headers; the linker
    # can omit its import table even though this module is part of our Qt
    # build contract. Package its library explicitly for future ABI use.
    if native_content.is_file():
        concurrent = mingw / "Qt6Concurrent.dll"
        if not concurrent.is_file():
            raise QtDeploymentError("Qt6Concurrent.dll is missing from toolchain")
        shutil.copy2(concurrent, bin_dir / concurrent.name)

    # Recursively walk imported dependencies, including plugins: windeployqt
    # is not guaranteed to include every transitive MinGW dependency.
    queue = [browser]
    if native_content.is_file() and native_content != browser:
        queue.append(native_content)
    queue.extend(bin_dir.rglob("*.dll"))
    visited: set[str] = set()
    installed = {p.name.lower() for p in bin_dir.glob("*.dll")}
    while queue:
        binary = queue.pop()
        key = str(binary.resolve()).casefold()
        if key in visited:
            continue
        visited.add(key)
        for name in imported_dlls(binary, objdump):
            system_dir = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32"
            if (name in installed or name in system_dlls or
                (system_dir / name).is_file() or name.startswith(
                    ("api-ms-win-", "ext-ms-win-"))):
                continue
            file = mingw / name
            if not file.is_file():
                raise QtDeploymentError(
                    "Qt standalone package unresolved dependency: " +
                    name + " (importer " + binary.name + ")")
            destination = bin_dir / file.name
            shutil.copy2(file, destination)
            installed.add(name)
            queue.append(destination)

    extras: dict[str, dict] = {}
    for file in sorted(bin_dir.rglob("*.dll")):
        relative = file.relative_to(package).as_posix()
        if file.parent == bin_dir and file.name.lower() in before:
            continue  # existing NorthStar capability, never a Qt runtime record
        extras[relative] = {"size": file.stat().st_size, "sha256": digest(file)}

    required_files = ["bin/Qt6Core.dll", "bin/Qt6Gui.dll",
                      "bin/Qt6Widgets.dll", "bin/platforms/qwindows.dll"]
    if native_content.is_file():
        required_files.append("bin/Qt6Concurrent.dll")
    for required in required_files:
        if required not in extras:
            raise QtDeploymentError("Qt standalone runtime missing " + required)

    # Preserve upstream notices (Qt is dynamically linked; DLL replacement is
    # permitted). Font Awesome's SIL OFL notice is installed by CMake.
    source_licenses = mingw.parent / "share" / "licenses" / "qt6-base"
    if not source_licenses.is_dir():
        source_licenses = mingw / ".." / "share" / "licenses" / "qt6-base"
    source_licenses = source_licenses.resolve()
    if not source_licenses.is_dir():
        raise QtDeploymentError("Qt 6 licenses are missing")
    dest_licenses = package / "licenses" / "Qt6Base"
    shutil.copytree(source_licenses, dest_licenses, dirs_exist_ok=True)
    record = {"schema": 1, "runtime": "Qt6Widgets",
              "dlls": extras, "qt_licenses": True,
              "fontawesome_resource": True}
    (package / "qt.runtime.json").write_text(
        json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return record



QT_TOOL_EXECUTABLES = (
    "northstarcpp_project_manager.exe",
    "northstarcpp_asset_browser.exe",
    "northstarcpp_camera_timeline_editor.exe",
    "northstarcpp_editor_qt.exe",
)


def isolate_qt_runtime(package: Path) -> dict:
    """Keep the Qt runtime in bin/qt_tools, with native launchers in bin.

    Direct execution from Explorer works because the small public EXE starts
    the Qt-linked program from the directory containing its Qt dependencies.
    Qt and native V8 may use conflicting CRT builds, so never move Qt DLLs
    back beside the Player.
    """
    record_path = package / "qt.runtime.json"
    if not record_path.is_file():
        raise QtDeploymentError("Qt deployment manifest missing")
    record = json.loads(record_path.read_text(encoding="utf-8"))
    if record.get("schema") != 1 or record.get("runtime") != "Qt6Widgets":
        raise QtDeploymentError("Invalid Qt deployment manifest")
    if record.get("layout", "flat") != "flat":
        raise QtDeploymentError("Qt runtime must start in flat deployment layout")
    verified_extras(package)
    bin_dir = package / "bin"
    qt = bin_dir / "qt_tools"
    if qt.exists():
        raise QtDeploymentError("Qt private runtime directory already exists")
    paths = record["dlls"]
    if not paths:
        raise QtDeploymentError("Qt manifest contains no runtime libraries")
    tool_names = [name for name in QT_TOOL_EXECUTABLES
                  if (bin_dir / name).is_file()]
    if not tool_names:
        raise QtDeploymentError("No Qt tool to isolate")

    # Validate every source and destination before modifying the stage.
    new_paths = {}
    for rel, info in paths.items():
        part = Path(rel)
        if (not isinstance(rel, str) or len(part.parts) < 2 or
                part.parts[0] != "bin" or ".." in part.parts):
            raise QtDeploymentError("Qt manifest contains unsafe path")
        moved = Path("bin") / "qt_tools" / Path(*part.parts[1:])
        if (package / moved).exists():
            raise QtDeploymentError("Qt isolate destination already exists")
        new_paths[moved.as_posix()] = info
    launchers = {}
    for name in tool_names:
        private = qt / name
        launcher_name = Path(name).stem + "_launcher.exe"
        launcher = package / "tools" / "qt_launchers" / launcher_name
        if private.exists():
            raise QtDeploymentError("Qt isolate tool collision: " + name)
        if not launcher.is_file():
            raise QtDeploymentError("Required native Qt launcher is missing: " + str(launcher))
        launchers[name] = launcher

    qt.mkdir()
    for name in tool_names:
        (bin_dir / name).replace(qt / name)
    for rel in paths:
        moved = Path("bin") / "qt_tools" / Path(*Path(rel).parts[1:])
        target = package / moved
        target.parent.mkdir(parents=True, exist_ok=True)
        (package / rel).replace(target)
    public = {}
    for name, launcher in launchers.items():
        destination = bin_dir / name
        shutil.copy2(launcher, destination)
        public["bin/" + name] = {
            "sha256": digest(destination), "size": destination.stat().st_size}
    record["dlls"] = new_paths
    record["layout"] = "process-isolated"
    record["executables"] = ["bin/qt_tools/" + name for name in tool_names]
    record["launchers"] = public
    record_path.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    verified_extras(package)
    return record


def verified_extras(package: Path, additional_extras: set[str] | None = None) -> set[str]:
    # Native scripting runtimes are validated separately by build_dist.
    additional_extras = additional_extras or set()
    manifest = package / "qt.runtime.json"
    if not manifest.is_file():
        raise QtDeploymentError("Qt runtime registry not present in distribution")
    record = json.loads(manifest.read_text(encoding="utf-8"))
    extras = record.get("dlls")
    if record.get("schema") != 1 or record.get("runtime") != "Qt6Widgets" or not isinstance(extras, dict):
        raise QtDeploymentError("Qt runtime registry invalid")
    layout = record.get("layout", "flat")
    if layout not in ("flat", "process-isolated"):
        raise QtDeploymentError("Unknown Qt runtime layout")
    qt_prefix = "bin/qt_tools" if layout == "process-isolated" else "bin"
    required_files = [qt_prefix + "/" + name for name in
                      ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
                       "platforms/qwindows.dll")]
    if ((package / qt_prefix / "northstarcpp_asset_browser.exe").is_file() or
            (layout == "flat" and
             (package / "bin/northstarcpp_asset_browser.exe").is_file())):
        required_files.append(qt_prefix + "/Qt6Concurrent.dll")
    if layout == "process-isolated":
        executable_paths = record.get("executables")
        launchers = record.get("launchers")
        if not isinstance(executable_paths, list) or not executable_paths:
            raise QtDeploymentError("Private Qt executable registry missing")
        if not isinstance(launchers, dict):
            raise QtDeploymentError("Public Qt launcher registry missing")
        expected = set()
        for name in executable_paths:
            if not isinstance(name, str) or not name.startswith(qt_prefix + "/") or (
                    Path(name).parent.as_posix() != qt_prefix or
                    not (package / name).is_file()):
                raise QtDeploymentError("Invalid Qt private executable: " + str(name))
            public_name = "bin/" + Path(name).name
            expected.add(public_name)
            info = launchers.get(public_name)
            launcher = package / public_name
            if not isinstance(info, dict) or not launcher.is_file() or (
                    launcher.stat().st_size != info.get("size") or
                    digest(launcher) != info.get("sha256")):
                raise QtDeploymentError("Public Qt launcher is missing or modified: " +
                                        public_name)
        if set(launchers) != expected:
            raise QtDeploymentError("Unexpected public Qt launchers in registry")
        if any((package / "bin" / lib).exists() for lib in
               ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
                "Qt6Concurrent.dll")):
            raise QtDeploymentError("Qt DLL leaked into public bin")
    for required in required_files:
        if required not in extras:
            raise QtDeploymentError("Qt runtime registry missing " + required)
    for relative, info in extras.items():
        parts = Path(relative).parts
        if len(parts) < 2 or parts[0] != "bin" or relative.find("..") != -1:
            raise QtDeploymentError("Unsafe Qt runtime path")
        file = package / relative
        if not file.is_file() or not isinstance(info, dict) or \
                info.get("size") != file.stat().st_size or \
                info.get("sha256") != digest(file):
            raise QtDeploymentError("Qt runtime tampered or missing: " + relative)
    if not (package / "licenses/FontAwesome/LICENSE.txt").is_file() or \
       not (package / "licenses/Qt6Base/LGPL-3.0-only.txt").is_file():
        raise QtDeploymentError("Required Qt/Font Awesome license notices absent")
    if layout == "process-isolated":
        for candidate in (package / qt_prefix).rglob("*.dll"):
            if candidate.relative_to(package).as_posix() not in extras:
                raise QtDeploymentError("Unregistered Qt private DLL: " +
                                        str(candidate))
        return set()  # No Qt DLL is permitted beside the native Player.
    actual_extra = {f.relative_to(package).as_posix() for f in (package / "bin").rglob("*.dll")
                    if f.parent != package / "bin" or f.name.lower().startswith(
                        ("qt6", "lib", "zlib"))}
    if not actual_extra.difference(additional_extras).issubset(set(extras)):
        raise QtDeploymentError("Unregistered Qt dependency in distribution")
    return {Path(name).name.casefold() for name in extras
            if Path(name).parent.as_posix() == "bin"}


def deploy_developer_binary(executable: Path, gpp: str, system_dlls: set[str]) -> int:
    """Copy Qt runtime beside a freshly built developer Project Browser.

    Only build-time automation needs Python. The resulting application is pure C++.
    """
    from tempfile import TemporaryDirectory
    exe = executable.resolve(strict=True)
    with TemporaryDirectory(prefix="northstar-qt-debug-") as tmp:
        stage = Path(tmp)
        bin_dir = stage / "bin"
        bin_dir.mkdir()
        shutil.copy2(exe, bin_dir / exe.name)
        license_file = Path(__file__).resolve().parents[1] / (
            "engine/northstarcpp/resources/fontawesome/LICENSE.txt")
        target = stage / "licenses/FontAwesome/LICENSE.txt"
        target.parent.mkdir(parents=True)
        shutil.copy2(license_file, target)
        record = deploy(stage, gpp, system_dlls)
        for relative in record["dlls"]:
            source = stage / relative
            dest = exe.parent / Path(relative).relative_to("bin")
            dest.parent.mkdir(parents=True, exist_ok=True)
            # Do not replace a running Qt DLL if its content is already correct.
            # Rewriting identical libraries can fail with ERROR_SHARING_VIOLATION
            # when a developer has the editor / Project Manager open.
            if dest.is_file() and dest.stat().st_size == source.stat().st_size and \
                    digest(dest) == digest(source):
                continue
            shutil.copy2(source, dest)
        # Include Qt's optional headless platform plugin in developer builds.
        # The audited production Qt package remains unchanged.
        offscreen = (Path(gpp).resolve().parent.parent /
                     "share/qt6/plugins/platforms/qoffscreen.dll")
        if offscreen.is_file():
            destination = exe.parent / "platforms/qoffscreen.dll"
            destination.parent.mkdir(parents=True, exist_ok=True)
            if (not destination.is_file() or
                    digest(destination) != digest(offscreen)):
                shutil.copy2(offscreen, destination)
        return len(record["dlls"])


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--developer-executable",type=Path,required=True)
    parser.add_argument("--gpp",required=True)
    options = parser.parse_args()
    # The same policy as build_dist; System32 libraries are automatically
    # recognized by deploy() independent of this supplementary small list.
    windows = {"kernel32.dll","user32.dll","gdi32.dll","advapi32.dll",
               "ntdll.dll","msvcrt.dll","shell32.dll","ole32.dll",
               "oleaut32.dll","ws2_32.dll","comdlg32.dll","comctl32.dll"}
    try:
        count = deploy_developer_binary(options.developer_executable,
                                        options.gpp, windows)
        print("NorthStar Qt development runtime deployed:", count, "DLLs")
        return 0
    except (QtDeploymentError, OSError, subprocess.TimeoutExpired) as e:
        parser.exit(2,str(e)+"\\n")


if __name__ == "__main__":
    raise SystemExit(main())
