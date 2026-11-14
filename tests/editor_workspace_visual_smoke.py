#!/usr/bin/env python3
"""Windows native Editor UI geometry/screenshot integration smoke (no PIL)."""
from __future__ import annotations
import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import zlib

if sys.platform != "win32":
    raise SystemExit("This integration test requires Win32")

U=ctypes.WinDLL("user32",use_last_error=True)
G=ctypes.WinDLL("gdi32",use_last_error=True)
HWND=wintypes.HWND; HDC=wintypes.HDC;LPARAM=wintypes.LPARAM
EnumProc=ctypes.WINFUNCTYPE(wintypes.BOOL,HWND,LPARAM)
U.EnumWindows.argtypes=[EnumProc,LPARAM];U.EnumChildWindows.argtypes=[HWND,EnumProc,LPARAM]
U.GetWindowThreadProcessId.argtypes=[HWND,ctypes.POINTER(wintypes.DWORD)]
U.GetClassNameW.argtypes=[HWND,wintypes.LPWSTR,ctypes.c_int]
U.GetWindowRect.argtypes=[HWND,ctypes.POINTER(wintypes.RECT)]
U.IsWindowVisible.argtypes=[HWND];U.IsWindowVisible.restype=wintypes.BOOL
U.PrintWindow.argtypes=[HWND,HDC,wintypes.UINT]
U.PrintWindow.restype=wintypes.BOOL
U.GetWindowDC.argtypes=[HWND];U.GetWindowDC.restype=HDC
U.ReleaseDC.argtypes=[HWND,HDC]
U.PostMessageW.argtypes=[HWND,wintypes.UINT,wintypes.WPARAM,LPARAM]
U.SendMessageW.argtypes=[HWND,wintypes.UINT,wintypes.WPARAM,LPARAM]
U.SendMessageW.restype=ctypes.c_ssize_t
G.CreateCompatibleDC.argtypes=[HDC];G.CreateCompatibleDC.restype=HDC
G.DeleteDC.argtypes=[HDC]
G.SelectObject.argtypes=[HDC,wintypes.HGDIOBJ]
G.SelectObject.restype=wintypes.HGDIOBJ
G.DeleteObject.argtypes=[wintypes.HGDIOBJ]
class BitmapInfoHeader(ctypes.Structure):
    _fields_=[("biSize",wintypes.DWORD),("biWidth",wintypes.LONG),
        ("biHeight",wintypes.LONG),("biPlanes",wintypes.WORD),
        ("biBitCount",wintypes.WORD),("biCompression",wintypes.DWORD),
        ("biSizeImage",wintypes.DWORD),("biXPelsPerMeter",wintypes.LONG),
        ("biYPelsPerMeter",wintypes.LONG),("biClrUsed",wintypes.DWORD),
        ("biClrImportant",wintypes.DWORD)]
class BitmapInfo(ctypes.Structure):
    _fields_=[("header",BitmapInfoHeader),("colors",wintypes.DWORD*3)]
G.CreateDIBSection.argtypes=[HDC,ctypes.POINTER(BitmapInfo),wintypes.UINT,
    ctypes.POINTER(ctypes.c_void_p),wintypes.HANDLE,wintypes.DWORD]
G.CreateDIBSection.restype=wintypes.HBITMAP

def rect(hwnd):
    result=wintypes.RECT()
    if not U.GetWindowRect(hwnd,ctypes.byref(result)):
        raise RuntimeError("GetWindowRect failed")
    return result

def cls(hwnd):
    buff=ctypes.create_unicode_buffer(140)
    U.GetClassNameW(hwnd,buff,len(buff))
    return buff.value

def list_windows(pid):
    windows=[]
    @EnumProc
    def each(hwnd,_):
        actual=wintypes.DWORD()
        U.GetWindowThreadProcessId(hwnd,ctypes.byref(actual))
        if actual.value==pid and cls(hwnd)=="NorthStarCppWindow":
            windows.append(hwnd)
        return True
    U.EnumWindows(each,0)
    return windows

def children(hwnd):
    items=[]
    @EnumProc
    def each(child,_):
        items.append((cls(child),child,rect(child)))
        return True
    U.EnumChildWindows(hwnd,each,0)
    return items

def chunk(tag,data):
    payload=tag+data
    return struct.pack(">I",len(data))+payload+struct.pack(">I",zlib.crc32(payload)&0xffffffff)

def write_png(path,width,height,bgra):
    # Top-down 32-bit DIB -> RGB PNG; deterministic and independent of PIL.
    scanlines=bytearray()
    for y in range(height):
        scanlines.append(0)
        for x in range(width):
            i=(y*width+x)*4
            scanlines.extend((bgra[i+2],bgra[i+1],bgra[i]))
    data=(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",
        width,height,8,2,0,0,0))+
        chunk(b"IDAT",zlib.compress(scanlines,6))+chunk(b"IEND",b""))
    path.write_bytes(data)

def capture(hwnd,out):
    bounds=rect(hwnd)
    width=bounds.right-bounds.left
    height=bounds.bottom-bounds.top
    if width<500 or height<400 or width>4096 or height>4096:
        raise RuntimeError("Unexpected editor screenshot dimensions")
    source=U.GetWindowDC(hwnd)
    if not source:raise RuntimeError("GetWindowDC failed")
    memory=G.CreateCompatibleDC(source)
    info=BitmapInfo()
    info.header.biSize=ctypes.sizeof(BitmapInfoHeader)
    info.header.biWidth=width
    info.header.biHeight=-height
    info.header.biPlanes=1
    info.header.biBitCount=32
    ptr=ctypes.c_void_p()
    bmp=G.CreateDIBSection(source,ctypes.byref(info),0,ctypes.byref(ptr),None,0)
    if not bmp or not ptr.value:
        G.DeleteDC(memory);U.ReleaseDC(hwnd,source)
        raise RuntimeError("CreateDIBSection failed")
    old=G.SelectObject(memory,bmp)
    try:
        rendered=bool(U.PrintWindow(hwnd,memory,2)) or bool(U.PrintWindow(hwnd,memory,0))
        if not rendered:raise RuntimeError("PrintWindow unavailable for this session")
        raw=ctypes.string_at(ptr,width*height*4)
        if len(set(raw[::4096]))<2:
            raise RuntimeError("Window capture has no visual variation")
        write_png(out,width,height,raw)
    finally:
        G.SelectObject(memory,old)
        G.DeleteObject(bmp)
        G.DeleteDC(memory)
        U.ReleaseDC(hwnd,source)
    return width,height,raw

def verify_real_thumbnail(target,window,grid,capture_data):
    expected=target/"skydome-ytd.ppm"
    if not expected.exists():
        raise RuntimeError("Missing VFS reference image: run editor_asset_pipeline first")
    image=expected.read_bytes()
    header=b"P6\n96 96\n255\n"
    if not image.startswith(header):
        raise RuntimeError("Invalid integration reference PPM")
    expected_rgb=image[len(header):]
    width,height,pixels=capture_data
    window_rect=rect(window);grid_rect=rect(grid)
    base_x=grid_rect.left-window_rect.left
    base_y=grid_rect.top-window_rect.top
    samples=((18,20),(35,20),(58,20),(35,37),(45,42),(64,52),
             (25,70),(65,75),(47,47),(70,30))
    best=0
    for oy in range(0,19):
        for ox in range(8,41):
            matched=0
            for px,py in samples:
                x=base_x+ox+px
                y=base_y+oy+py
                if not 0<=x<width or not 0<=y<height:
                    continue
                offset=(y*width+x)*4
                got=(pixels[offset+2],pixels[offset+1],pixels[offset])
                sample=(py*96+px)*3
                want=tuple(expected_rgb[sample:sample+3])
                if got==want:matched+=1
            best=max(best,matched)
    if best<8:
        raise RuntimeError(f"Live YTD thumbnail pixels diverge from VFS decode ({best}/10 matching samples)")
    return f"live YTD pixels match VFS reference ({best}/10)"

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--editor",type=Path,required=True)
    parser.add_argument("--fixture",type=Path,required=True)
    parser.add_argument("--shared-assets",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    target=args.output.resolve()
    target.mkdir(parents=True,exist_ok=True)
    project=target/"VisualSmokeProject"
    assets=project/"assets"
    assets.mkdir(parents=True,exist_ok=True)
    for rel in ("textures/skydome.ytd","models/skydome.ydd","materials/skydome.ymat"):
        src=args.shared_assets/rel
        dst=assets/rel
        dst.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(src,dst)
    fixture=project/"main.nscene"
    shutil.copyfile(args.fixture,fixture)
    editor=args.editor.resolve()
    # This specifically exercises legacy pane binding while Qt browser tests
    # cover the new default Content Browser independently.
    environment=os.environ.copy()
    environment["NORTHSTARCPP_TEST_LEGACY_CONTENT"]="1"
    process=subprocess.Popen([str(editor),"--warp","--scene",str(fixture)],
        cwd=editor.parent,env=environment,
        creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    window=None
    try:
        deadline=time.monotonic()+22
        while time.monotonic()<deadline:
            if process.poll() is not None:
                raise RuntimeError(f"Editor exited unexpectedly with {process.returncode}")
            windows=list_windows(process.pid)
            if windows and len(children(windows[0]))>=18:
                window=windows[0]
                break
            time.sleep(.2)
        if not window:raise RuntimeError("Native editor workspace never appeared")
        time.sleep(.7)
        items=children(window)
        names=[x[0] for x in items]
        assert "NorthStarCppViewport" in names,names
        assert "SysListView32" in names,names
        assert "SysTreeView32" in names,names
        assert "ListBox" in names,names
        view=next(r for cl,_,r in items if cl=="NorthStarCppViewport")
        filegrid=next(r for cl,_,r in items if cl=="SysListView32")
        outliner=next(r for cl,_,r in items if cl=="ListBox")
        assert view.right>view.left+300 and view.bottom>view.top+100
        assert outliner.right<view.left
        assert filegrid.top>=view.bottom
        assert U.IsWindowVisible(window)
        assert any(cl=="SysTreeView32" and U.IsWindowVisible(hwnd)
                   for cl,hwnd,_ in items)
        dimensions=capture(window,target/"northstar-editor-visual-smoke.png")
        grid_hwnd=next(hwnd for cl,hwnd,_ in items if cl=="SysListView32")
        if U.SendMessageW(grid_hwnd,0x1004,0,0)!=3:
            raise RuntimeError("Expected the three sample asset folders in Content Browser")
        # Native pointer-free mouse messages. Never pass ctypes pointers via
        # WM_NOTIFY/LVM_SETITEMSTATE across Windows processes.
        x,y=330,48  # third folder icon ("textures") in the sample grid
        xy=((y&0xffff)<<16)|(x&0xffff)
        for message,buttons in ((0x0201,1),(0x0202,0),(0x0203,1),(0x0202,0)):
            U.SendMessageW(grid_hwnd,message,buttons,xy)
            time.sleep(.08)
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:
            count=U.SendMessageW(grid_hwnd,0x1004,0,0)
            if count==1:break
            time.sleep(.2)
        if count!=1:
            raise RuntimeError(f"Native folder navigation failed (item count={count})")
        # A VFS job must finish and present its preview without blocking UI.
        time.sleep(1.5)
        preview=capture(window,target/"northstar-editor-texture-preview.png")
        comparison=verify_real_thumbnail(target,window,grid_hwnd,preview)
        print(f"PASS: {len(items)} native controls; screenshot {dimensions[0]}x{dimensions[1]}; "
              f"asset grid and viewport non-overlapping; {comparison}")
    finally:
        if window:U.PostMessageW(window,0x0010,0,0)
        try:process.wait(timeout=7)
        except subprocess.TimeoutExpired:
            process.kill();process.wait(timeout=4)

if __name__=="__main__":
    main()
