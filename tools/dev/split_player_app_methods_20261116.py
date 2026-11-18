#!/usr/bin/env python3
"""Split large stateful App inline methods into ownership-focused class-member includes.
These .inc files compile in window.cpp until App private fields are moved into a Pimpl.
This preserves identical in-class method definitions, ABI and per-frame operations.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
folder=root/"engine/northstarcpp/modules/editor"
src=folder/"window.cpp"
source=src.read_text(encoding="utf-8")
blocks=[
 ("window_runtime_bootstrap.inc", "    explicit App(Options settings,", "    Scene load_startup_scene() const"),
 ("window_runtime_win32_viewport.inc", "    LRESULT viewport_message(HWND handle,UINT msg,WPARAM wp,LPARAM lp)", "    void initialize_asset_zones()"),
 ("window_runtime_scene_streaming.inc", "    void initialize_asset_zones()", "    void update_fps_title()"),
 ("window_runtime_win32_messages.inc", "    LRESULT message(HWND handle, UINT msg, WPARAM wparam, LPARAM lparam)", "    HWND child(wchar_t const* type, wchar_t const* label, DWORD style, int id)"),
 ("window_runtime_frame_loop.inc", "    void frame()", "    void smoke()"),
]
segments=[]
for filename,start,end in blocks:
 assert source.count(start)==1, (filename,start,source.count(start))
 assert source.count(end)==1, (filename,end,source.count(end))
 a=source.index(start)
 z=source.index(end,a)
 assert a<z
 segments.append((filename,a,z,source[a:z]))
assert "class App {" in source and "int WINAPI WinMain(" in source
# Coordinates are derived from the original snapshot before any replacement.
for filename,a,z,body in reversed(segments):
 assert len(body)>250
 out=folder/filename
 if out.exists():
  assert out.read_text(encoding="utf-8").startswith("// NorthStarCpp "+filename)
 marker='    // Stateful App methods remain in the same class until Pimpl extraction.\n'
 out.write_text('// NorthStarCpp '+filename+' -- included in class App; do not compile separately.\n'+body,encoding="utf-8")
 source=source[:a]+marker+'#include "'+filename+'"\n\n'+source[z:]
src.write_text(source,encoding="utf-8")
print('App decomposition:')
for filename,_,_ in blocks:
 f=folder/filename
 print(' ',filename,len(f.read_text(encoding="utf-8").splitlines()),'lines')
print('Remaining window.cpp lines',len(source.splitlines()))
