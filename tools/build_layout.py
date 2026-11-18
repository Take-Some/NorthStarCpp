"""Canonical source/build/package layout for NorthStarCpp.

One CMake binary tree (out/build) serves Debug, Release and RelWithDebInfo.
Reconfigure the build type before building; do not build types concurrently.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "engine" / "northstarcpp" / "modules"
OUTPUT_ROOT = ROOT / "out"
BUILD_DIR = OUTPUT_ROOT / "build"
LOG_DIR = OUTPUT_ROOT / "logs"
QA_DIR = OUTPUT_ROOT / "qa"
PACKAGE_DIR = OUTPUT_ROOT / "package"
QA_PACKAGE_DIR = PACKAGE_DIR / "qa"
RELEASE_PACKAGE_DIR = PACKAGE_DIR / "release"
GAME_PACKAGE_DIR = PACKAGE_DIR / "games"

def canonical_build_path(kind="Release"):
    if kind not in ("Debug", "Release", "RelWithDebInfo"):
        raise ValueError("Unsupported build type: " + str(kind))
    return BUILD_DIR

def require_canonical_build(path):
    candidate = Path(path).resolve()
    if candidate != BUILD_DIR.resolve():
        raise ValueError("Only one NorthStarCpp CMake build tree is supported: "
                         + str(BUILD_DIR) + " (requested " + str(candidate) + ")")
    return candidate

def require_inside_output(path, purpose="Output"):
    candidate = Path(path).resolve()
    if OUTPUT_ROOT.resolve() not in candidate.parents:
        raise ValueError(purpose + " must be inside " + str(OUTPUT_ROOT) +
                         ": " + str(candidate))
    return candidate

def configured_build_type(path=BUILD_DIR):
    cache = Path(path) / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_BUILD_TYPE:STRING="):
            return line.partition("=")[2]
    return None
