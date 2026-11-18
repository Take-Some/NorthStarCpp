#!/usr/bin/env python3
"""Compile the ACTUAL D3D11/D3D12 Sky HLSL literals with Windows D3DCompile.

This compiles shader bytecode but does not claim a rendered GPU screenshot.
"""
import ctypes, re, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LIB=ctypes.WinDLL("d3dcompiler_47.dll")

COMPILE=LIB.D3DCompile
COMPILE.argtypes=[
    ctypes.c_void_p,ctypes.c_size_t,ctypes.c_char_p,
    ctypes.c_void_p,ctypes.c_void_p,ctypes.c_char_p,
    ctypes.c_char_p,ctypes.c_uint,ctypes.c_uint,
    ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_void_p)]
COMPILE.restype=ctypes.c_long


def blob_bytes(ptr):
    vtable=ctypes.cast(ptr,ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    addr=ctypes.WINFUNCTYPE(ctypes.c_void_p,ctypes.c_void_p)(vtable[3])
    size=ctypes.WINFUNCTYPE(ctypes.c_size_t,ctypes.c_void_p)(vtable[4])
    return ctypes.string_at(addr(ptr),size(ptr))


def release(ptr):
    if not ptr:return
    vtable=ctypes.cast(ptr,ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    fn=ctypes.WINFUNCTYPE(ctypes.c_ulong,ctypes.c_void_p)(vtable[2])
    fn(ptr)


def main():
    for provider in ("d3d11","d3d12"):
        path=ROOT/f"engine/northstarcpp/modules/render/{provider}/shader_compiler.cpp"
        cpp=path.read_text("utf8")
        if provider=="d3d12":
            # D3D12 owns a compile-time embedded copy of the shared root
            # shader rather than a second, divergent string literal.
            assert "sky_atmosphere_hlsl" in cpp, (
                "D3D12 must compile the canonical engine root Sky shader")
            source=(ROOT/"engine/northstarcpp/resources/sharedAssets/shaders/sky/atmosphere.hlsl").read_bytes()
        else:
            match=re.search(r'constexpr char sky_shader_code\[\]\s*=\s*R"\((.*?)\)";',
                            cpp,re.DOTALL)
            assert match,f"Sky shader literal missing in {provider}"
            source=match.group(1).encode("utf8")
        assert b"sky_texture.Sample" in source
        assert b"p.uv" in source
        assert b"sky_noise" not in source, "Obsolete procedural-only sky shader still present"
        for entry,profile in (
                (b"sky_vertex_main",b"vs_5_0"),
                (b"sky_pixel_main",b"ps_5_0")):
            output=ctypes.c_void_p()
            errors=ctypes.c_void_p()
            try:
                hr=COMPILE(source,len(source),
                   f"NorthStar {provider} source sky".encode(),
                   None,None,entry,profile,1<<11,0,
                   ctypes.byref(output),ctypes.byref(errors))
                if hr<0:
                    reason=blob_bytes(errors).decode("utf8","replace") if errors else ""
                    raise RuntimeError(f"{provider} {entry.decode()} HLSL error {hr:x}: {reason}")
                assert output and blob_bytes(output)[:4]==b"DXBC"
                print(f"PASS {provider}: {entry.decode()} {profile.decode()} D3DCompile/DXBC")
            finally:
                release(output)
                release(errors)
    return 0


if __name__=="__main__":
    raise SystemExit(main())
