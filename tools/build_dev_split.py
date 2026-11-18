"""Create independent unapproved NorthStarCpp developer packages from out/q7."""
import json
import re
from pathlib import Path
import shutil
import tempfile

from tools.dev.cli import ROOT, config_load, toolchain
from tools.build_dist import (
    COMPONENTS, SYSTEM_DLLS, check_binary_imports, manifest_for,
    parse_registry, package_smoke, run, sdk_smoke, stage_native_runtime,
    verify_manifest,
)
from tools.deploy_qt import deploy, isolate_qt_runtime
from tools.split_distribution import assemble


def main():
    build = ROOT / "out/q7"
    project_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(NorthStarCpp VERSION (\d+\.\d+\.\d+)", project_cmake)
    if not match:
        raise RuntimeError("Cannot determine NorthStarCpp project version from CMakeLists.txt")
    version = match.group(1)
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    assert "CMAKE_BUILD_TYPE:STRING=Release" in cache
    tools = toolchain(config_load(), required=True)
    cmake, ninja, gpp = [str(tools[key]) for key in ("cmake", "ninja", "g++")]
    dist = ROOT / "out/package/development"
    dest = dist / f"NorthStarCpp-{version}-windows-x64-Release-Development"
    if dest.exists():
        raise RuntimeError("Refusing to overwrite: " + str(dest))
    dist.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".development-", dir=dist) as tmp:
        stage = Path(tmp) / "stage"
        stage.mkdir()
        for component in COMPONENTS:
            run([cmake, "--install", str(build), "--prefix", str(stage),
                 "--config", "Release", "--component", component], timeout=140)
        deploy(stage, gpp, SYSTEM_DLLS)
        isolate_qt_runtime(stage)
        stage_native_runtime(stage, gpp)
        registry, _ = parse_registry(stage)
        if registry["engine_version"] != version:
            raise RuntimeError("Wrong engine version")
        package_smoke(stage)
        sdk_smoke(stage, cmake, ninja, gpp)
        imported = check_binary_imports(stage, str(Path(gpp).with_name("objdump.exe")))
        notice = stage / "licenses/DEVELOPMENT_ONLY.txt"
        notice.write_text(f"NorthStarCpp {version} development-only. Full QA and external release approval pending.\n", encoding="utf-8")
        record = manifest_for(stage, registry, imported, "Release", False)
        (stage / "manifest.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        verify_manifest(stage)
        result = assemble(stage, dest, approved=False, development=True)
        print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
