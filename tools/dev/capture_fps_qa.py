"""Capture actual NorthStar Player frame from Windows desktop for FPS QA."""
import argparse
import base64
import ctypes
import os
import struct
import subprocess
import time
import zlib
from ctypes import wintypes
from pathlib import Path

R=Path(__file__).resolve().parents[2]
class Rect(ctypes.Structure):
    _fields_=[('left',wintypes.LONG),('top',wintypes.LONG),
              ('right',wintypes.LONG),('bottom',wintypes.LONG)]
class Point(ctypes.Structure):
    _fields_=[('x',wintypes.LONG),('y',wintypes.LONG)]
class Header(ctypes.Structure):
    _fields_=[('biSize',wintypes.DWORD),('biWidth',wintypes.LONG),('biHeight',wintypes.LONG),
              ('biPlanes',wintypes.WORD),('biBitCount',wintypes.WORD),('biCompression',wintypes.DWORD),
              ('biSizeImage',wintypes.DWORD),('biXPelsPerMeter',wintypes.LONG),
              ('biYPelsPerMeter',wintypes.LONG),('biClrUsed',wintypes.DWORD),('biClrImportant',wintypes.DWORD)]
class Info(ctypes.Structure):
    _fields_=[('bmiHeader',Header),('bmiColors',wintypes.DWORD*3)]
u=ctypes.windll.user32
g=ctypes.windll.gdi32
u.GetForegroundWindow.restype=wintypes.HWND
u.GetDC.restype=wintypes.HDC
g.CreateCompatibleDC.restype=wintypes.HDC
g.CreateCompatibleBitmap.restype=wintypes.HBITMAP
g.SelectObject.restype=wintypes.HGDIOBJ
u.GetDC.argtypes=[wintypes.HWND]
g.CreateCompatibleDC.argtypes=[wintypes.HDC]
g.CreateCompatibleBitmap.argtypes=[wintypes.HDC,ctypes.c_int,ctypes.c_int]
g.SelectObject.argtypes=[wintypes.HDC,wintypes.HGDIOBJ]
g.StretchBlt.argtypes=[wintypes.HDC,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,
                       wintypes.HDC,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.DWORD]
g.GetDIBits.argtypes=[wintypes.HDC,wintypes.HBITMAP,wintypes.UINT,wintypes.UINT,
                      ctypes.c_void_p,ctypes.POINTER(Info),wintypes.UINT]
g.SetStretchBltMode.argtypes=[wintypes.HDC,ctypes.c_int]
g.DeleteDC.argtypes=[wintypes.HDC]
g.DeleteObject.argtypes=[wintypes.HGDIOBJ]
u.ReleaseDC.argtypes=[wintypes.HWND,wintypes.HDC]
u.ClientToScreen.argtypes=[wintypes.HWND,ctypes.POINTER(Point)]
u.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(Rect)]

def chunk(tag,data):
    return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
def encode(w,h,rgba):
    rows=bytearray()
    for y in range(h):
        row=rgba[y*w*4:(y+1)*w*4]
        rows.append(0)
        rgb=bytearray(w*3)
        rgb[0::3]=row[2::4];rgb[1::3]=row[1::4];rgb[2::3]=row[0::4]
        rows.extend(rgb)
    return (bytes([137,80,78,71,13,10,26,10])+
        chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+
        chunk(b'IDAT',zlib.compress(rows,6))+chunk(b'IEND',b''))
def target_window(pid):
    windows=[]
    CALLBACK=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    def callback(hwnd,_):
        process=wintypes.DWORD()
        u.GetWindowThreadProcessId(hwnd,ctypes.byref(process))
        if process.value==pid and u.IsWindowVisible(hwnd):windows.append(hwnd)
        return True
    cb=CALLBACK(callback)
    u.EnumWindows(cb,0)
    return windows[0] if windows else None
def capture(hwnd,max_width):
    u.ShowWindow(hwnd,9)
    u.BringWindowToTop(hwnd)
    u.SetForegroundWindow(hwnd)
    time.sleep(.35)
    rect=Rect();pt=Point()
    if not u.GetClientRect(hwnd,ctypes.byref(rect)) or not u.ClientToScreen(hwnd,ctypes.byref(pt)):
        raise RuntimeError('Window rectangle unavailable')
    width=rect.right-rect.left;height=rect.bottom-rect.top
    if width<50 or height<50:raise RuntimeError('Player surface too small')
    w=min(width,max_width);h=max(1,round(height*w/width))
    screen=u.GetDC(0);mem=g.CreateCompatibleDC(screen);bitmap=g.CreateCompatibleBitmap(screen,w,h)
    if not screen or not mem or not bitmap:raise RuntimeError('GDI unavailable')
    previous=g.SelectObject(mem,bitmap)
    try:
        g.SetStretchBltMode(mem,4)
        if not g.StretchBlt(mem,0,0,w,h,screen,pt.x,pt.y,width,height,0x00CC0020):
            raise RuntimeError('GDI capture refused')
        g.SelectObject(mem,previous)
        previous=0
        image=ctypes.create_string_buffer(w*h*4)
        info=Info();info.bmiHeader.biSize=ctypes.sizeof(Header)
        info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32
        info.bmiHeader.biCompression=0;info.bmiHeader.biSizeImage=len(image)
        result=g.GetDIBits(mem,bitmap,0,h,image,ctypes.byref(info),0)
        if result!=h:raise RuntimeError('GetDIBits failed')
        return encode(w,h,image.raw),w,h
    finally:
        if previous:g.SelectObject(mem,previous)
        if bitmap:g.DeleteObject(bitmap)
        if mem:g.DeleteDC(mem)
        u.ReleaseDC(0,screen)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--player',default='out/build/northstarcpp_player.exe')
    parser.add_argument('--output',default='Projects/AbbyPistolDemo/docs/fps_viewmodel_qa.png')
    parser.add_argument('--preview',action='store_true')
    parser.add_argument('--topmost',action='store_true',
                        help='Temporarily show the spawned QA Player above desktop windows')
    args=parser.parse_args()
    player=(R/args.player).resolve(strict=True)
    output=R/args.output
    env=os.environ.copy();env['NORTHSTARCPP_RENDER_PROVIDER']='d3d12'
    env['NORTHSTARCPP_PHYSICS_PROVIDER']='jolt'
    proc=subprocess.Popen([str(player),'--warp','--explore','--scene',
        str(R/'Projects/AbbyPistolDemo/main.nscn'),'--shared-assets',str(R/'sharedAssets')],
        cwd=player.parent,env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    try:
        hwnd=None
        for _ in range(70):
            if proc.poll() is not None:raise RuntimeError('Native Player stopped before capture')
            hwnd=target_window(proc.pid)
            if hwnd:break
            time.sleep(.1)
        if not hwnd:raise RuntimeError('No native Player HWND')
        time.sleep(5)
        if args.topmost:
            u.SetWindowPos(hwnd,-1,0,0,0,0,0x0003)
        output.parent.mkdir(parents=True,exist_ok=True)
        png,w,h=capture(hwnd,1280)
        output.write_bytes(png)
        print(f'CAPTURED {w}x{h} bytes={len(png)} path={output}',flush=True)
        if args.preview:
            reduced,w2,h2=capture(hwnd,512)
            print('BASE64:'+base64.b64encode(reduced).decode(),flush=True)
    finally:
        if args.topmost and 'hwnd' in locals() and hwnd:
            u.SetWindowPos(hwnd,-2,0,0,0,0,0x0003)
        if proc.poll() is None:
            subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],
                capture_output=True,text=True,errors='replace',timeout=8)
if __name__=='__main__':main()
