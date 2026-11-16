"""Exercise the actual BugTrap Win32 dialog, its controls, resize and DWM-independent paint."""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import zlib

u32, gdi = C.windll.user32, C.windll.gdi32
LRESULT = C.c_ssize_t
WNDPROC = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
u32.EnumWindows.argtypes = [WNDPROC, W.LPARAM]
u32.EnumChildWindows.argtypes = [W.HWND, WNDPROC, W.LPARAM]
u32.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
u32.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u32.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u32.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u32.GetWindowDC.argtypes = [W.HWND]
u32.ReleaseDC.argtypes = [W.HWND, W.HDC]
u32.PrintWindow.argtypes = [W.HWND, W.HDC, W.UINT]
u32.MoveWindow.argtypes = [W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.BOOL]
u32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
gdi.CreateCompatibleDC.argtypes = [W.HDC]
gdi.CreateCompatibleBitmap.argtypes = [W.HDC, C.c_int, C.c_int]
gdi.SelectObject.argtypes = [W.HDC, W.HGDIOBJ]
gdi.DeleteDC.argtypes = [W.HDC]
gdi.DeleteObject.argtypes = [W.HGDIOBJ]
gdi.GetDIBits.argtypes = [W.HDC, W.HBITMAP, W.UINT, W.UINT, C.c_void_p, C.c_void_p, W.UINT]
gdi.BitBlt.argtypes = [W.HDC, C.c_int, C.c_int, C.c_int, C.c_int,
                       W.HDC, C.c_int, C.c_int, W.DWORD]

class BIH(C.Structure):
    _fields_ = [("size", W.DWORD), ("width", W.LONG), ("height", W.LONG),
                ("planes", W.WORD), ("bitcount", W.WORD), ("compression", W.DWORD),
                ("sizeimage", W.DWORD), ("xpels", W.LONG), ("ypels", W.LONG),
                ("colorsused", W.DWORD), ("important", W.DWORD)]

def title(hwnd):
    buffer = C.create_unicode_buffer(256)
    u32.GetWindowTextW(hwnd, buffer, 256)
    return buffer.value

def window_for_pid(pid):
    result=[]
    @WNDPROC
    def callback(hwnd, unused):
        process=W.DWORD()
        u32.GetWindowThreadProcessId(hwnd, C.byref(process))
        if process.value == pid and title(hwnd) == "NorthStarCpp - BugTrap":
            result.append(hwnd)
        return True
    u32.EnumWindows(callback, 0)
    return result[0] if result else 0

def children(hwnd):
    entries=[]
    @WNDPROC
    def callback(child, unused):
        c=C.create_unicode_buffer(80)
        u32.GetClassNameW(child,c,80)
        entries.append((c.value,title(child)))
        return True
    u32.EnumChildWindows(hwnd,callback,0)
    return entries

def screenshot(hwnd, image_path):
    rect=W.RECT()
    if not u32.GetWindowRect(hwnd,C.byref(rect)):
        raise AssertionError("GetWindowRect failed")
    width, height = rect.right-rect.left, rect.bottom-rect.top
    dc=u32.GetWindowDC(hwnd)
    mem=gdi.CreateCompatibleDC(dc)
    bitmap=gdi.CreateCompatibleBitmap(dc,width,height)
    old=gdi.SelectObject(mem,bitmap)
    try:
        ok=gdi.BitBlt(mem,0,0,width,height,dc,0,0,0x00CC0020)
        if not ok:
            ok=u32.PrintWindow(hwnd,mem,2)
        if not ok:
            ok=u32.PrintWindow(hwnd,mem,0)
        if not ok:
            raise AssertionError("PrintWindow failed")
        header=BIH(C.sizeof(BIH),width,-height,1,32,0,width*height*4,0,0,0,0)
        pixels=(C.c_ubyte*(width*height*4))()
        if gdi.GetDIBits(mem,bitmap,0,height,C.byref(pixels),C.byref(header),0) != height:
            raise AssertionError("GetDIBits failed")
        def chunk(kind,data):
            return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xffffffff)
        rows=[]
        for y in range(height):
            scan=bytearray(b"\0")
            offset=y*width*4
            for x in range(width):
                b,g,r,a=pixels[offset+x*4:offset+x*4+4]
                scan.extend((r,g,b))
            rows.append(bytes(scan))
        image=b"\x89PNG\r\n\x1a\n"
        image+=chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8,2,0,0,0))
        image+=chunk(b"IDAT",zlib.compress(b"".join(rows),6))
        image+=chunk(b"IEND",b"")
        image_path.write_bytes(image)
        if len(image)<2000:
            raise AssertionError("Screenshot suspiciously small")
        return width,height,len(image)
    finally:
        gdi.SelectObject(mem,old)
        gdi.DeleteObject(bitmap)
        gdi.DeleteDC(mem)
        u32.ReleaseDC(hwnd,dc)

def main(exe:Path, output:Path):
    with tempfile.TemporaryDirectory(prefix="northstar-bugtrap-gui-") as t:
        report=Path(t)/"smoke report.txt"
        report.write_text("NorthStarCpp Crash Report\r\n"
            "UTC: 2026-11-15 19:00:00.000\r\n"
            "Application: northstarcpp_editor_qt.exe\r\n"
            "Process ID: 1234\r\nThread ID: 5678\r\n"
            "Exception code: 0xC0000005\r\n"
            "Exception address: 0xDEADBEEF\r\n"
            "Minidump: saved\r\nPrivacy: Local only.\r\n",encoding="utf-8")
        previous=u32.GetForegroundWindow()
        process=subprocess.Popen([str(exe),"--report",str(report)],
                                 stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            hwnd=0
            for _ in range(40):
                hwnd=window_for_pid(process.pid)
                if hwnd: break
                if process.poll() is not None: break
                time.sleep(.1)
            if not hwnd:
                raise AssertionError(f"GUI did not open, exit={process.poll()}")
            controls=children(hwnd)
            names=[t for c,t in controls if c=="Button"]
            for required in ("Open report folder","Copy diagnostics","Close"):
                if required not in names:
                    raise AssertionError(f"Missing button: {required}: {controls}")
            if not any(c=="Edit" for c,t in controls):
                raise AssertionError("Readonly report view missing")
            u32.MoveWindow(hwnd,60,60,900,650,True)
            time.sleep(.5)
            w,h,length=screenshot(hwnd,output)
            dpi=u32.GetDpiForWindow(hwnd)
            if w<740 or h<530:
                raise AssertionError(f"Bad window size: {w}x{h}")
            print(f"PASS GUI: {w}x{h}, DPI={dpi}, {len(controls)} controls, screenshot={length} bytes, image={output}")
            u32.MoveWindow(hwnd,60,60,740,530,True)
            time.sleep(.2)
            mw,mh,mlen=screenshot(hwnd,output.with_name("bugtrap-ui-min.png"))
            if mw!=740 or mh!=530:
                raise AssertionError(f"Min-size layout unexpected: {mw}x{mh}")
            print(f"PASS GUI minimum size: {mw}x{mh}, screenshot={mlen} bytes")
            # Exercise the actual Close button without touching the user's clipboard.
            close=[]
            @WNDPROC
            def find_close(child,unused):
                cls=C.create_unicode_buffer(80)
                u32.GetClassNameW(child,cls,80)
                if cls.value=="Button" and title(child)=="Close":
                    close.append(child)
                return True
            u32.EnumChildWindows(hwnd,find_close,0)
            if not close: raise AssertionError("Close control missing")
            u32.SendMessageW(close[0],0x00F5,0,0)  # BM_CLICK
            if process.wait(timeout=3)!=0: raise AssertionError("Close did not exit cleanly")
            print("PASS GUI Close button")
        finally:
            hwnd=window_for_pid(process.pid)
            if hwnd: u32.PostMessageW(hwnd,0x0010,0,0) # WM_CLOSE
            try: process.wait(timeout=4)
            except subprocess.TimeoutExpired: process.kill();process.wait(timeout=2)
            if previous: u32.SetForegroundWindow(previous)

if __name__=="__main__":
    if len(sys.argv)!=3: sys.exit("Usage: test.py <bugtrap.exe> <screenshot.png>")
    main(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())
