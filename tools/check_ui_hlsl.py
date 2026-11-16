"""Compile the embedded D3D12 UI HLSL in both shader stages."""
import ctypes
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (
    root / "engine/northstarcpp/modules/render/d3d12/renderer_ui.cpp"
).read_text(encoding="utf-8")
match = re.search(r'R"HLSL\((.*?)\)HLSL"', source, re.DOTALL)
if not match:
    raise SystemExit("Embedded UI HLSL not found")
project=root / "Projects/KaylaNightCity/assets/shaders/ui/menu_hover.hlsl"
if not project.is_file():
    raise SystemExit("Project HLSL not found")
programs={"generic engine fallback":match.group(1).encode("utf-8"),
          "KaylaNightCity menu":project.read_bytes()}
dll = ctypes.WinDLL("d3dcompiler_47.dll")
compile_shader = dll.D3DCompile
compile_shader.argtypes = [
    ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p,
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p,
    ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint,
    ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p),
]
compile_shader.restype = ctypes.c_long
for label,hlsl in programs.items():
  for entry, target in [(b"vs", b"vs_5_0"), (b"ps", b"ps_5_0")]:
    compiled, errors = ctypes.c_void_p(), ctypes.c_void_p()
    status = compile_shader(
        hlsl, len(hlsl), None, None, None, entry, target, 0, 0,
        ctypes.byref(compiled), ctypes.byref(errors)
    )
    if status < 0:
      if errors.value:
        vtable = ctypes.cast(
            errors, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))
        ).contents
        data = ctypes.WINFUNCTYPE(
            ctypes.c_void_p, ctypes.c_void_p
        )(vtable[3])(errors)
        size = ctypes.WINFUNCTYPE(
            ctypes.c_size_t, ctypes.c_void_p
        )(vtable[4])(errors)
        print(ctypes.string_at(data, size).decode("utf-8", "replace"))
      raise SystemExit(f"{label}: {entry.decode()} {target.decode()} compilation FAILED")
    print(f"{label}: {entry.decode()} {target.decode()} PASS")
