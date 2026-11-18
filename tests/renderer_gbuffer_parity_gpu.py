#!/usr/bin/env python3
"""Real GPU D3D12/Vulkan GBuffer-debug parity, identical camera and geometry."""
from __future__ import annotations
import os
import sys
import tempfile
from pathlib import Path
from renderer_cross_api_fixture_gpu import capture, mask, read_ppm

def main() -> None:
    if len(sys.argv)!=4:
        raise SystemExit("Usage: gbuffer_parity.py <probe> <d3d12.dll> <vulkan.dll>")
    probe,d3d12,vulkan=(Path(arg).resolve() for arg in sys.argv[1:4])
    with tempfile.TemporaryDirectory(prefix="northstar_gbuffer_parity_") as temp:
        for mode in ("albedo","normal","roughness","coverage"):
            os.environ["NORTHSTARCPP_GBUFFER_DEBUG"]=mode
            a_path=Path(temp)/(mode+"_d3d12.ppm")
            b_path=Path(temp)/(mode+"_vulkan.ppm")
            capture(probe,d3d12,a_path,"d3d12")
            capture(probe,vulkan,b_path,"vulkan")
            width,height,a=read_ppm(a_path)
            w2,h2,b=read_ppm(b_path)
            if (width,height)!=(w2,h2):
                raise AssertionError(f"{mode}: GPU capture dimensions differ")
            mae=sum(abs(x-y) for x,y in zip(a,b))/len(a)
            first,second=mask(a),mask(b)
            if len(first)<500 or len(second)<500:
                raise AssertionError(f"{mode}: no foreground")
            iou=len(first&second)/len(first|second)
            print(f"{mode}: MAE={mae:.5f}/255, IoU={iou:.5f}",flush=True)
            if mae>0.5 or iou<0.98:
                raise AssertionError(f"{mode}: GBuffer parity regression")

if __name__=="__main__":
    main()
