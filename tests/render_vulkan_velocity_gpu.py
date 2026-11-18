#!/usr/bin/env python3
"""Vulkan RG16F velocity GPU acceptance: reset frame and moved camera frame.

Both captures are from a native Vulkan provider on an actual GPU. The first
frame must encode zero motion; the second frame must show screen-space motion
for the cube while uncovered sky remains stationary.
"""
from __future__ import annotations
import os
import subprocess
import sys
import tempfile
from pathlib import Path

def capture(probe: Path,dll: Path,path: Path,mode: str) -> bytes:
    env=os.environ.copy()
    env.update(
        NORTHSTARCPP_ENABLE_VULKAN_DEFERRED="1",
        NORTHSTARCPP_ENABLE_VULKAN_HDR="1",
        NORTHSTARCPP_ENABLE_VULKAN_VELOCITY="1",
        NORTHSTARCPP_VULKAN_VELOCITY_DEBUG="1",
        NORTHSTARCPP_GBUFFER_VERSION="2",
        NORTHSTARCPP_VULKAN_AUTO_EXPOSURE="0",
        NORTHSTARCPP_VULKAN_BLOOM_QUALITY="0",
        NORTHSTARCPP_GPU_CAPTURE_MODE="11",
        NORTHSTARCPP_GPU_CAPTURE_PPM=str(path),
    )
    result=subprocess.run(
        [str(probe),str(dll),mode],env=env,capture_output=True,
        text=True,timeout=70)
    if result.returncode==77:
        raise SystemExit(77)
    if result.returncode!=0 or not path.is_file():
        raise AssertionError(
            f"Vulkan velocity GPU capture failed ({mode}) "+
            result.stdout[-1800:]+result.stderr[-1200:])
    parts=path.read_bytes().split(b"\n",3)
    if len(parts)!=4 or parts[0]!=b"P6" or parts[2]!=b"255":
        raise AssertionError("Invalid binary GPU PPM")
    w,h=map(int,parts[1].split())
    if w!=496 or h!=345 or len(parts[3])!=w*h*3:
        raise AssertionError("Unexpected Vulkan capture dimensions")
    return parts[3]

def main() -> None:
    if len(sys.argv)!=3:
        raise SystemExit("Usage: velocity_gpu.py <probe.exe> <render_vulkan.dll>")
    probe,dll=(Path(arg).resolve() for arg in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="northstar_motion_gpu_") as temp:
        folder=Path(temp)
        idle=capture(probe,dll,folder/"zero.ppm","--parity")
        motion=capture(probe,dll,folder/"moving.ppm","--velocity")
        if idle[:3]!=motion[:3]:
            raise AssertionError("Sky velocity changed despite static camera basis")
        pixels=len(idle)//3
        changed=sum(
            sum(abs(idle[i*3+c]-motion[i*3+c]) for c in range(3))>12
            for i in range(pixels))
        center=3*((345//2)*496+496//2)
        if changed<1000 or changed>pixels//2:
            raise AssertionError(f"GPU motion not spatially localized: {changed}")
        if motion[center]<=idle[center]+20:
            raise AssertionError("Moving camera did not generate positive X motion")
        if max(abs(idle[i]-127) for i in (center,center+1))>2:
            raise AssertionError("History reset produced phantom motion")
        print(
            f"Vulkan velocity hardware PASS: {changed} changed pixels, "
            f"center R {idle[center]} -> {motion[center]}, "
            f"sky RGB {list(idle[:3])}",
            flush=True,
        )

if __name__=="__main__":
    main()
