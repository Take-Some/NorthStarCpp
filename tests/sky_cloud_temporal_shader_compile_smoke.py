#!/usr/bin/env python3
"""Compile both cloud-history resolve shaders without claiming GPU integration."""
import ctypes,sys,subprocess,os,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tests"))
from sky_hlsl_compile_smoke import COMPILE,blob_bytes,release
sys.path.insert(0,str(ROOT/"tools"))
from generate_vulkan_material_spirv import locate_glslc

def compile_hlsl(source,entry,profile):
    out=ctypes.c_void_p()
    error=ctypes.c_void_p()
    try:
        hr=COMPILE(source,len(source),b"NorthStar temporal cloud resolve",
                   None,None,entry.encode(),profile.encode(),1<<11,0,
                   ctypes.byref(out),ctypes.byref(error))
        if hr<0:
            reason=blob_bytes(error).decode("utf8","replace") if error else "none"
            raise RuntimeError(entry+" error: "+reason)
        assert blob_bytes(out)[:4]==b"DXBC"
        print("PASS HLSL",entry,profile)
    finally:
        release(out)
        release(error)

def main():
    shader=ROOT/"engine/northstarcpp/resources/sharedAssets/shaders/sky"
    source=(shader/"temporal_cloud_resolve.hlsl").read_bytes()
    compile_hlsl(source,"cloud_temporal_vs","vs_5_0")
    compile_hlsl(source,"cloud_temporal_ps","ps_5_0")
    compiler=locate_glslc()
    with tempfile.TemporaryDirectory(prefix="northstar-cloud-history-") as tmp:
        output=Path(tmp)/"cloud_temporal.spv"
        subprocess.run([compiler,"--target-env=vulkan1.3",
            "-fshader-stage=frag",str(shader/"temporal_cloud_resolve.glsl"),
            "-o",str(output)],check=True)
        assert output.read_bytes()[:4]==b"\x03\x02\x23\x07"
    print("PASS GLSL Vulkan SPIR-V cloud_temporal fragment")
if __name__=="__main__":main()
