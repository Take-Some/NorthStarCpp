#!/usr/bin/env python3
"""Generate Vulkan 1.3 deferred-path SPIR-V with a locally installed glslc."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"engine/northstarcpp/modules/render/vulkan"
SOURCES=[
    ("deferred_primitive.vert","deferred_primitive_vertex"),
    ("deferred_gbuffer.frag","deferred_gbuffer_fragment"),
    ("deferred_gbuffer.frag","deferred_gbuffer_fragment_v2"),
    ("deferred_resolve.vert","deferred_resolve_vertex"),
    ("deferred_resolve.frag","deferred_resolve_fragment"),
    ("deferred_resolve.frag","deferred_resolve_fragment_v2"),
    ("deferred_resolve.frag","deferred_resolve_fragment_ssao"),
    ("deferred_resolve.frag","deferred_resolve_fragment_v2_ssao"),
    ("deferred_ssao.frag","deferred_ssao_fragment"),
    ("deferred_shadow.vert","deferred_shadow_vertex"),
    ("deferred_resolve.frag","deferred_resolve_fragment_csm"),
    ("deferred_resolve.frag","deferred_resolve_fragment_v2_csm"),
    ("deferred_resolve.frag","deferred_resolve_fragment_ssao_csm"),
    ("deferred_resolve.frag","deferred_resolve_fragment_v2_ssao_csm"),
    ("deferred_bloom.frag","deferred_bloom_fragment"),
    ("deferred_composite.frag","deferred_composite_fragment"),
    ("deferred_luminance.frag","deferred_luminance_fragment"),
    ("deferred_velocity.frag","deferred_velocity_fragment"),
    ("deferred_resolve.frag","deferred_resolve_fragment_hdr"),
    ("deferred_resolve.frag","deferred_resolve_fragment_v2_hdr"),
    ("deferred_resolve.frag","deferred_resolve_fragment_ssao_hdr"),
    ("deferred_resolve.frag","deferred_resolve_fragment_v2_ssao_hdr"),
    ("deferred_resolve.frag","deferred_resolve_fragment_csm_hdr"),
    ("deferred_resolve.frag","deferred_resolve_fragment_v2_csm_hdr"),
    ("deferred_resolve.frag","deferred_resolve_fragment_ssao_csm_hdr"),
    ("deferred_resolve.frag","deferred_resolve_fragment_v2_ssao_csm_hdr"),
]
def main():
    glslc=shutil.which("glslc")
    if not glslc:
        sdk=os.environ.get("VULKAN_SDK")
        if sdk:
            candidate=Path(sdk)/"Bin/glslc.exe"
            if candidate.is_file():glslc=str(candidate)
    if not glslc:raise RuntimeError("Vulkan SDK glslc required; no network downloads")
    out=["#pragma once","// Generated from deferred_*.frag/.vert by tools/generate_vulkan_deferred_spirv.py.",
         "#include <cstdint>","namespace northstarcpp::render::vulkan::shaders {"]
    with tempfile.TemporaryDirectory(prefix="ns-vk-deferred-") as tmp:
        for filename,name in SOURCES:
            output=Path(tmp)/(filename+".spv")
            flags=[]
            if "_v2" in name:flags.append("-DNS_GBUFFER_V2=1")
            if "_ssao" in name and filename=="deferred_resolve.frag":
                flags.append("-DNS_SSAO_HALF=1")
            if "_csm" in name and filename=="deferred_resolve.frag":
                flags.append("-DNS_CSM=1")
            if "_hdr" in name and filename=="deferred_resolve.frag":
                flags.append("-DNS_HDR_LINEAR=1")
            subprocess.run([glslc,"-O","--target-env=vulkan1.3",*flags,
                str(SRC/filename),"-o",str(output)],check=True)
            data=output.read_bytes()
            if len(data)<20 or len(data)%4:raise RuntimeError("Invalid SPIR-V: "+filename)
            words=struct.unpack("<"+"I"*(len(data)//4),data)
            if words[0]!=0x07230203:raise RuntimeError("Invalid SPIR-V magic")
            out.append(f"inline constexpr std::uint32_t {name}[]={{")
            for start in range(0,len(words),8):
                out.append("    "+",".join(f"0x{w:08x}u" for w in words[start:start+8])+",")
            out.append("};")
    out.append("}")
    dest=SRC/"deferred_spirv.hpp"
    dest.write_text("\n".join(out)+"\n",encoding="utf-8")
    print("Generated",dest,"bytes",dest.stat().st_size)
if __name__=="__main__":main()
