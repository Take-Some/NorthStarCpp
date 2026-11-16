"""Compile Vulkan UI shaders and refresh the embedded SPIR-V header.

Usage: python tools/regenerate_ui_overlay_spirv.py
Requires glslc (Vulkan SDK or MSYS2).
"""
from __future__ import annotations

import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "engine/northstarcpp/modules/render/vulkan"
HEADER = SHADERS / "ui_overlay_spirv.hpp"


def main() -> None:
    compiler = shutil.which("glslc")
    if not compiler:
        raise SystemExit("glslc executable not found")
    validator = shutil.which("spirv-val")
    generated = {}
    with tempfile.TemporaryDirectory(prefix="northstar_ui_spirv_") as directory:
        for name, suffix in (("ui_vert", "vert"), ("ui_frag", "frag")):
            output = Path(directory) / (name + ".spv")
            subprocess.run(
                [compiler, "--target-env=vulkan1.3", "-O",
                 str(SHADERS / ("ui_overlay." + suffix)), "-o", str(output)],
                check=True,
            )
            if validator:
                subprocess.run([validator, "--target-env", "vulkan1.3",
                                str(output)], check=True)
            data = output.read_bytes()
            if len(data) % 4:
                raise ValueError("Unaligned SPIR-V")
            words = struct.unpack("<" + "I" * (len(data) // 4), data)
            rows = [
                "    " + ",".join(f"0x{value:08x}u" for value in words[i:i+8]) + ","
                for i in range(0, len(words), 8)
            ]
            generated[name] = (
                f"inline constexpr std::uint32_t {name}[] = {{\n"
                + "\n".join(rows) + "\n};"
            )
            print(f"{name}: {len(words)} SPIR-V words OK")

    source = HEADER.read_text(encoding="utf-8")
    for name, block in generated.items():
        pattern = (rf"inline constexpr std::uint32_t {name}\[\] = "
                   r"\{.*?\n\};")
        source, count = re.subn(
            pattern, lambda match: block, source, count=1, flags=re.DOTALL
        )
        if count != 1:
            raise ValueError(f"Missing SPIR-V symbol: {name}")
    staged = HEADER.with_suffix(".hpp.pending")
    staged.write_text(source, encoding="utf-8", newline="\n")
    staged.replace(HEADER)
    print("Updated:", HEADER)


if __name__ == "__main__":
    main()
