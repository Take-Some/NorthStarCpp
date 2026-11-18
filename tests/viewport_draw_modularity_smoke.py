#!/usr/bin/env python3
"""Static regression gate for the independently compiled Viewport::draw."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
sources = ROOT / "engine/northstarcpp/modules/viewport"
cmake = (ROOT / "engine/northstarcpp/CMakeLists.txt").read_text(encoding="utf-8")
main = (sources / "viewport.cpp").read_text(encoding="utf-8")
draw = (sources / "viewport_draw.cpp").read_text(encoding="utf-8")
internal = (sources / "viewport_internal.hpp").read_text(encoding="utf-8")
state = (sources / "viewport_state.inc").read_text(encoding="utf-8")
legacy = sources / "viewport_draw.inc"

assert len(re.findall(r"\bvoid\s+Viewport::draw\s*\(", draw)) == 1, (
    "Viewport::draw must have exactly one independent definition")
assert "Viewport::draw(" not in main, (
    "Viewport::draw must not be compiled inside viewport.cpp")
assert '#include "viewport_draw.inc"' not in main and '#include "viewport_draw.inc"' not in draw, (
    "Legacy draw .inc must not be compiled")
assert '#include "viewport_internal.hpp"' in main
assert '#include "viewport_internal.hpp"' in draw
assert '#include "viewport_state.inc"' in internal
assert len(re.findall(r"\bstruct\s+Viewport::State\s*\{", state)) == 1
assert not re.search(r"\bstruct\s+Viewport::State\s*\{", main + draw + internal)
assert re.search(r"(?m)^\s*modules/viewport/viewport_draw\.cpp\s*$", cmake), (
    "CMake must compile the draw translation unit separately")
assert "void Viewport::draw(" in draw
print("viewport draw modularity: PASS (independent translation unit and shared private state)")
