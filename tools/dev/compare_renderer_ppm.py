#!/usr/bin/env python3
"""Compare GPU-readback PPM captures from identical renderer QA scenes.

No third-party dependencies. Emits JSON metrics, a contact sheet and a
per-channel absolute-difference PNG. Differences are diagnostic, not an
automatic assertion of visual equivalence.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import zlib
from pathlib import Path


def load_ppm(path: Path) -> tuple[int, int, bytes]:
    payload = path.read_bytes()
    header = payload.split(b"\n", 3)
    if len(header) != 4 or header[0] != b"P6" or header[2] != b"255":
        raise ValueError(f"{path}: expected binary RGB P6 PPM")
    width, height = map(int, header[1].split())
    if not 0 < width < 16384 or not 0 < height < 16384:
        raise ValueError(f"{path}: invalid size")
    rgb = header[3]
    if len(rgb) != width * height * 3:
        raise ValueError(f"{path}: size mismatch")
    return width, height, rgb


def save_png(path: Path, width: int, height: int, pixels: bytes) -> None:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack("!I", len(data)) + kind + data + struct.pack(
            "!I", zlib.crc32(kind + data) & 0xFFFFFFFF
        )

    stride = width * 3
    raw = b"".join(
        b"\x00" + pixels[y * stride : (y + 1) * stride]
        for y in range(height)
    )
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack("!2I5B", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("d3d12", type=Path)
    parser.add_argument("vulkan", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    width, height, a = load_ppm(args.d3d12)
    vw, vh, b = load_ppm(args.vulkan)
    if (width, height) != (vw, vh):
        raise ValueError("Capture resolutions do not match")
    args.out.mkdir(parents=True, exist_ok=True)
    count = len(a)
    abs_diff = bytes(abs(x - y) for x, y in zip(a, b))
    sq = sum(float(d) ** 2 for d in abs_diff)
    mae = sum(abs_diff) / count
    rmse = math.sqrt(sq / count)
    psnr = math.inf if rmse == 0 else 20.0 * math.log10(255.0 / rmse)
    by_channel_a = [sum(a[i::3]) / (count / 3) for i in range(3)]
    by_channel_b = [sum(b[i::3]) / (count / 3) for i in range(3)]
    sorted_delta = sorted(abs_diff)
    p95 = sorted_delta[math.ceil(0.95 * count) - 1]
    p99 = sorted_delta[math.ceil(0.99 * count) - 1]
    equal = sum(x == y for x, y in zip(a, b))
    report = {
        "width": width,
        "height": height,
        "pixel_rgb_channels": count,
        "mae_rgb_8bit": round(mae, 4),
        "rmse_rgb_8bit": round(rmse, 4),
        "psnr_db": round(psnr, 4) if math.isfinite(psnr) else "identical",
        "absolute_error_p95": p95,
        "absolute_error_p99": p99,
        "exact_channel_fraction": round(equal / count, 7),
        "d3d12_mean_rgb": [round(v, 3) for v in by_channel_a],
        "vulkan_mean_rgb": [round(v, 3) for v in by_channel_b],
        "status": "measured; parity not asserted",
        "note": "First verify-mode-1 GPU probe frame. Same scene input, "
                "different provider render paths and HDR configurations."
    }
    (args.out / "comparison.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    doubled = bytearray(width * 2 * height * 3)
    stride = width * 3
    for y in range(height):
        src = y * stride
        dst = y * stride * 2
        doubled[dst : dst + stride] = a[src : src + stride]
        doubled[dst + stride : dst + 2 * stride] = b[src : src + stride]
    save_png(args.out / "side_by_side.png", width * 2, height, doubled)
    contrast = bytes(min(255, d * 3) for d in abs_diff)
    save_png(args.out / "absolute_difference_x3.png", width, height, contrast)
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
