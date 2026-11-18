#!/usr/bin/env python3
"""D3D12/Vulkan parity regression: matching camera and scene, real GPU images.

A successful probe is not a pixel-parity pass. We require a coarse image
similarity and silhouette coverage, and print exact MAE/IoU for the QA report.
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def read_ppm(file: Path) -> tuple[int, int, bytes]:
    parts = file.read_bytes().split(b"\n", 3)
    if len(parts) != 4 or parts[0] != b"P6" or parts[2] != b"255":
        raise AssertionError(f"Invalid PPM capture: {file}")
    width, height = map(int, parts[1].split())
    if len(parts[3]) != width * height * 3:
        raise AssertionError(f"Invalid PPM byte size: {file}")
    return width, height, parts[3]


def capture(probe: Path, dll: Path, output: Path, provider: str) -> None:
    env = os.environ.copy()
    env["NORTHSTARCPP_GPU_CAPTURE_MODE"] = "11"
    env["NORTHSTARCPP_GPU_CAPTURE_PPM"] = str(output)
    if provider == "d3d12":
        env.update(
            NORTHSTARCPP_ENABLE_HDR="1",
            NORTHSTARCPP_ENABLE_BLOOM="0",
            NORTHSTARCPP_ENABLE_SHADOWS="0",
        )
    else:
        env.update(
            NORTHSTARCPP_ENABLE_VULKAN_DEFERRED="1",
            NORTHSTARCPP_ENABLE_VULKAN_HDR="1",
            NORTHSTARCPP_VULKAN_AUTO_EXPOSURE="1",
            NORTHSTARCPP_VULKAN_BLOOM_QUALITY="0",
            NORTHSTARCPP_GBUFFER_VERSION="2",
        )
    result = subprocess.run(
        [str(probe), str(dll), "--parity"],
        env=env, capture_output=True, text=True, timeout=65,
    )
    if result.returncode == 77:
        raise SystemExit(77)
    if result.returncode != 0 or not output.is_file():
        raise AssertionError(
            f"{provider} controlled GPU fixture failed: "
            f"{result.stdout[-2600:]} {result.stderr[-1000:]}"
        )


def mask(image: bytes, threshold: int = 45) -> set[int]:
    background = image[:3]
    return {
        pixel for pixel in range(len(image) // 3)
        if sum(
            abs(image[3 * pixel + channel] - background[channel])
            for channel in range(3)
        ) > threshold
    }


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("Usage: parity.py <probe> <d3d12.dll> <vulkan.dll>")
    probe, d3d12, vulkan = (Path(value).resolve() for value in sys.argv[1:4])
    with tempfile.TemporaryDirectory(prefix="northstar_parity_") as temp:
        d3d_image = Path(temp) / "d3d12.ppm"
        vulkan_image = Path(temp) / "vulkan.ppm"
        capture(probe, d3d12, d3d_image, "d3d12")
        capture(probe, vulkan, vulkan_image, "vulkan")
        w, h, a = read_ppm(d3d_image)
        w2, h2, b = read_ppm(vulkan_image)
        if (w, h) != (w2, h2):
            raise AssertionError("D3D12/Vulkan image size mismatch")
        mae = sum(abs(x - y) for x, y in zip(a, b)) / len(a)
        first, second = mask(a), mask(b)
        if len(first) < 500 or len(second) < 500:
            raise AssertionError("Insufficient real foreground pixel coverage")
        union = len(first | second)
        iou = len(first & second) / union if union else 0.0
        print(
            f"Controlled D3D12/Vulkan GPU fixture {w}x{h}: "
            f"MAE={mae:.5f}/255, silhouette IoU={iou:.5f}, "
            f"foreground D3D12={len(first)}, Vulkan={len(second)}",
            flush=True,
        )
        # Tight golden fixture: same camera, geometry, unlit materials and HDR.
        # Other PBR/postprocessing combinations remain separate QA targets.
        if mae > 0.50 or iou < 0.98:
            raise AssertionError(
                "Renderer parity regression: image delta or silhouette "
                "overlap worsened (visual parity not yet achieved)"
            )


if __name__ == "__main__":
    main()
