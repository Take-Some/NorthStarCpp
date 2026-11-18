#!/usr/bin/env python3
"""Update only basic/world SPIR-V arrays after editing their source shaders.

Avoid regenerating the unrelated Material Graph variant/header by replacing
only the exact named arrays. Uses installed Vulkan SDK glslc; no downloads.
"""
from __future__ import annotations
from pathlib import Path
import os,re,shutil,struct,subprocess,tempfile

ROOT=Path(__file__).resolve().parents[2]
VK=ROOT/"engine/northstarcpp/modules/render/vulkan"

def compile_one(shader:str) -> str:
    sdk=os.environ.get("VULKAN_SDK","")
    compiler=shutil.which("glslc")
    if not compiler and sdk:
        candidate=Path(sdk)/"Bin/glslc.exe"
        if candidate.is_file():compiler=str(candidate)
    if not compiler:raise RuntimeError("Vulkan glslc compiler unavailable")
    with tempfile.TemporaryDirectory(prefix="northstar-forward-fog-") as temp:
        output=Path(temp)/"shader.spv"
        subprocess.run([compiler,"-O","--target-env=vulkan1.3",
                        str(VK/shader),"-o",str(output)],check=True)
        data=output.read_bytes()
    if len(data)%4 or data[:4]!=b"\x03\x02\x23\x07":
        raise RuntimeError("Invalid generated SPIR-V")
    words=struct.unpack("<"+"I"*(len(data)//4),data)
    return "\n".join("    "+",".join(f"0x{x:08x}u" for x in words[i:i+8])+","
                       for i in range(0,len(words),8))

def patch_array(header:Path,symbol:str,contents:str)->None:
    text=header.read_text(encoding="utf8")
    pattern=(r"(inline\s+constexpr\s+std::uint32_t\s+"+re.escape(symbol)+
            r"\s*\[\s*\]\s*=\s*\{)(.*?)(\};)")
    replacement=lambda m:m.group(1)+"\n"+contents+"\n"+m.group(3)
    result,count=re.subn(pattern,replacement,text,count=1,flags=re.S)
    if count!=1:raise RuntimeError(f"Could not find unique array {symbol}")
    header.write_text(result,encoding="utf8")
    print(f"PASS embedded {symbol}")

def main()->None:
    patch_array(VK/"shader_spirv.hpp","basic_vertex",
                compile_one("basic.vert"))
    patch_array(VK/"shader_spirv.hpp","basic_fragment",
                compile_one("basic.frag"))
    patch_array(VK/"material_spirv.hpp","world_fragment",
                compile_one("world.frag"))
if __name__=="__main__":main()
