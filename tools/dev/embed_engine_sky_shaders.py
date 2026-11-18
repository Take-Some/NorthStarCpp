#!/usr/bin/env python3
"""Embed globally shared NorthStar Sky shaders into native D3D12/Vulkan providers.

The canonical GLSL/HLSL lives in engine/northstarcpp/resources/sharedAssets,
so every project sees the same root shader assets without copying them.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SKY = ROOT / "engine/northstarcpp/resources/sharedAssets/shaders/sky"
SOURCES = (
    ("sky_atmosphere_hlsl", "atmosphere.hlsl"),
    ("sky_atmosphere_glsl", "atmosphere.glsl"),
    ("sky_sun_optics_hlsl", "sun_optics.hlsl"),
    ("sky_sun_optics_glsl", "sun_optics.glsl"),
)


def render() -> str:
    parts = [
        "// AUTO-GENERATED. DO NOT EDIT. Edit sharedAssets/shaders/sky/ instead.\n",
        "#pragma once\n",
        "namespace northstarcpp::shaders {\n",
    ]
    for symbol, filename in SOURCES:
        src = (SKY / filename).read_text(encoding="utf-8")
        if ")NS_SKY_SHADER\"" in src:
            raise ValueError(f"Reserved C++ raw-string delimiter in {filename}")
        sha = hashlib.sha256(src.encode("utf-8")).hexdigest()
        parts.extend(
            (
                f"// {filename} SHA-256 {sha}\n",
                f"inline constexpr char {symbol}[] = R\"NS_SKY_SHADER({src.rstrip() + chr(10)})NS_SKY_SHADER\";\n",
            )
        )
    parts.append("} // namespace northstarcpp::shaders\n")
    return "".join(parts)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    data = render()
    if args.verify:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != data:
            raise SystemExit(f"Sky shader header out of date: {args.output}")
        print("PASS: root Engine Sky shaders are in sync")
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != data:
        args.output.write_text(data, encoding="utf-8")
    print(f"Embedded {len(SOURCES)} root Sky shaders: {args.output}")


if __name__ == "__main__":
    main()
