#!/usr/bin/env python3
"""Cross-provider hardware velocity check using real GPU RG16F readbacks.

D3D12 copies its RG16F center texel to CPU (QA only); Vulkan displays the
same RG16F vector encoded into RGBA8 diagnostic output. The two APIs must
agree within the diagnostic 8-bit quantization tolerance.
"""
from __future__ import annotations

import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def invoke(exe: Path,dll: Path,environment: dict[str,str],flag: str) -> subprocess.CompletedProcess[str]:
    result=subprocess.run([str(exe),str(dll),flag],
                          env=environment,text=True,capture_output=True,
                          timeout=85)
    if result.returncode==77:
        raise SystemExit(77)
    if result.returncode!=0:
        raise AssertionError(
            f"GPU velocity {dll.name} {flag} failed: "+
            result.stdout[-2000:]+result.stderr[-2000:])
    return result


def d3d12_center(probe: Path,dll: Path,flag: str) -> tuple[float,float]:
    env=os.environ.copy()
    env.update(NORTHSTARCPP_ENABLE_HDR="1",
               NORTHSTARCPP_ENABLE_D3D12_VELOCITY="1",
               NORTHSTARCPP_D3D12_VELOCITY_PROBE="1",
               NORTHSTARCPP_GBUFFER_VERSION="2",
               NORTHSTARCPP_ENABLE_BLOOM="0")
    output=invoke(probe,dll,env,flag)
    matches=re.findall(
        r"\[D3D12\]\[Motion\] center half_bits_x=(\d+) half_bits_y=(\d+)",
        output.stderr)
    if not matches:
        raise AssertionError("D3D12 GPU RG16F readback missing")
    # The parity/velocity fixture may execute additional material passes:
    # first motion measurement corresponds to the requested camera test.
    return tuple(
        struct.unpack("<e",int(bits).to_bytes(2,"little"))[0]
        for bits in matches[0]
    )


def vulkan_center(probe: Path,dll: Path,flag: str,path: Path) -> tuple[float,float]:
    env=os.environ.copy()
    env.update(NORTHSTARCPP_ENABLE_VULKAN_DEFERRED="1",
               NORTHSTARCPP_ENABLE_VULKAN_HDR="1",
               NORTHSTARCPP_ENABLE_VULKAN_VELOCITY="1",
               NORTHSTARCPP_VULKAN_VELOCITY_DEBUG="1",
               NORTHSTARCPP_GBUFFER_VERSION="2",
               NORTHSTARCPP_VULKAN_AUTO_EXPOSURE="0",
               NORTHSTARCPP_VULKAN_BLOOM_QUALITY="0",
               NORTHSTARCPP_GPU_CAPTURE_MODE="11",
               NORTHSTARCPP_GPU_CAPTURE_PPM=str(path))
    invoke(probe,dll,env,flag)
    parts=path.read_bytes().split(b"\n",3)
    if len(parts)!=4 or parts[0]!=b"P6":
        raise AssertionError("Invalid Vulkan velocity GPU capture")
    width,height=map(int,parts[1].split())
    if (width,height)!=(496,345):
        raise AssertionError("Unexpected Vulkan camera fixture resolution")
    at=3*((height//2)*width+(width//2))
    red,green=parts[3][at:at+2]
    # Shader diagnostic maps 0 pixel motion -> (0.5,0.5).
    return ((red/255.-.5)*32.,(green/255.-.5)*32.)


def main() -> None:
    if len(sys.argv)!=4:
        raise SystemExit(
            "Usage: velocity_parity.py <probe.exe> <d3d12.dll> <vulkan.dll>")
    probe,d3d12,vulkan=(Path(s).resolve() for s in sys.argv[1:4])
    with tempfile.TemporaryDirectory(prefix="northstar_motion_parity_") as tmp:
        directory=Path(tmp)
        dx0=d3d12_center(probe,d3d12,"--parity")
        vk0=vulkan_center(probe,vulkan,"--parity",directory/"idle.ppm")
        dx1=d3d12_center(probe,d3d12,"--velocity")
        vk1=vulkan_center(probe,vulkan,"--velocity",directory/"moving.ppm")
        print(
            f"D3D12 RG16F rest={dx0} moving={dx1}; "
            f"Vulkan diagnostic rest={vk0} moving={vk1}",flush=True)
        for value in dx0:
            if abs(value)>.08:
                raise AssertionError("D3D12 initial history produced phantom motion")
        for value in vk0:
            if abs(value)>.2:
                raise AssertionError("Vulkan initial history produced phantom motion")
        if dx1[0]<3 or vk1[0]<3:
            raise AssertionError("Camera movement produced no GPU X motion")
        if abs(dx1[0]-vk1[0])>.35 or abs(dx1[1]-vk1[1])>.35:
            raise AssertionError("GPU D3D12/Vulkan camera-motion parity failure")
        print(
            f"GPU camera-motion parity PASS: center X delta="
            f"{abs(dx1[0]-vk1[0]):.4f}px",flush=True)


if __name__=="__main__":
    main()
