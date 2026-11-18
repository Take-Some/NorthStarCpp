#!/usr/bin/env python3
"""Hardware acceptance: translucent blending must not depend on batch order.

Renders identical 3-layer translucent scenes twice through the selected native
provider, reversing material submission order. Compares actual GPU readback
pixels, tolerating one 8-bit LSB due to FP16 rounding and exposure.
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    parts = path.read_bytes().split(b"\n", 3)
    if len(parts) != 4 or parts[0] != b"P6" or parts[2] != b"255":
        raise AssertionError(f"Invalid PPM GPU capture: {path}")
    w, h = map(int, parts[1].split())
    image = parts[3]
    if len(image) != w * h * 3:
        raise AssertionError(f"Incorrect PPM byte count: {path}")
    return w, h, image


def run(probe: Path, dll: Path, capture: Path, reverse: bool) -> None:
    env = os.environ.copy()
    env.update(
        NORTHSTARCPP_ENABLE_VULKAN_DEFERRED="1",
        NORTHSTARCPP_ENABLE_VULKAN_HDR="1",
        # Isolate alpha-blend order from auto exposure / bloom temporal noise.
        NORTHSTARCPP_VULKAN_AUTO_EXPOSURE="0",
        NORTHSTARCPP_VULKAN_BLOOM_QUALITY="0",
        NORTHSTARCPP_TEST_TRANSPARENT_LAYERS="1",
        NORTHSTARCPP_TEST_TRANSPARENT_REVERSE="1" if reverse else "0",
        NORTHSTARCPP_GPU_CAPTURE_MODE="10",
        NORTHSTARCPP_GPU_CAPTURE_PPM=str(capture),
    )
    if "d3d12" in dll.name.lower():
        env.update(
            NORTHSTARCPP_ENABLE_HDR="1",
            NORTHSTARCPP_ENABLE_BLOOM="0",
        )
    completed = subprocess.run(
        [str(probe), str(dll), "--full"],
        env=env,
        capture_output=True,
        text=True,
        timeout=70,
    )
    if completed.returncode == 77:
        raise SystemExit(77)  # No Vulkan hardware: CTest SKIP_RETURN_CODE.
    if completed.returncode != 0 or not capture.is_file():
        raise AssertionError(
            f"{dll.name} layered GPU probe failed for reverse={reverse}: "
            f"{completed.stdout[-2500:]} {completed.stderr[-1500:]}"
        )


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("Usage: script.py <gpu_provider_probe.exe> <render_vulkan/d3d12.dll>")
    probe, dll = map(lambda x: Path(x).resolve(), sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="northstar_vk_layers_") as directory:
        folder = Path(directory)
        normal = folder / "normal.ppm"
        reversed_ = folder / "reversed.ppm"
        run(probe, dll, normal, False)
        run(probe, dll, reversed_, True)
        w, h, a = read_ppm(normal)
        wr, hr, b = read_ppm(reversed_)
        if (w, h) != (wr, hr):
            raise AssertionError("GPU output dimensions do not match")
        # Reject a false positive caused by shader PSOs still compiling:
        # both reference captures must contain foreground layers, not sky.
        center=((h // 2) * w + (w // 2)) * 3
        for label, rgb in (("normal", a), ("reversed", b)):
            center_delta=max(abs(rgb[center + c] - rgb[c])
                             for c in range(3))
            if center_delta < 30:
                raise AssertionError(
                    f"{dll.stem} {label}: translucent geometry missing "
                    f"from GPU center pixel (delta={center_delta})"
                )
        differences = [abs(x - y) for x, y in zip(a, b)]
        largest = max(differences)
        mean = sum(differences) / len(differences)
        if largest > 1 or mean > 0.5:
            raise AssertionError(
                f"Transparent submission-order regression: MAE={mean:.4f}, "
                f"maximum channel error={largest}; expected <=1"
            )
        # Tests should measure an actual 3-layer image and not two empty
        # captures. The GPU probe itself checks geometry/readback success.
        print(
            f"{dll.stem} translucent layers hardware PASS: {w}x{h}, "
            f"MAE={mean:.5f}, max_channel_delta={largest}"
        )


if __name__ == "__main__":
    main()
