"""Real Win32 pointer + text input into native engine.ui -> V8 callbacks."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import os,shutil,subprocess,sys,tempfile,time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.v8_provider.player_e2e import read_marker
user32=ctypes.WinDLL("user32",use_last_error=True)
Proc=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
user32.EnumWindows.argtypes=[Proc,wintypes.LPARAM]
user32.EnumChildWindows.argtypes=[wintypes.HWND,Proc,wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
user32.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
user32.SendMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
user32.SendMessageW.restype=ctypes.c_ssize_t
user32.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
SCRIPT=r"""
import { UI, Events, Session } from "@northstar/engine";
let root;
let clickActivationRemaining=null;
Events.on("start",()=>{
    root=UI.createRoot("clicktest");
    root.image("backdrop","textures/menu.ytd@bg",{
        x:0,y:0,width:1280,height:720,
        position:"absolute",fit:"cover",layer:"background",
        ui_shader_hlsl:"shaders/ui/menu_hover.hlsl",
        ui_shader_spirv:"shaders/ui/menu_hover.frag.spv"
    });
    root.image("brand","textures/menu.ytd@logo",{
        x:410,y:12,width:290,height:160,
        position:"absolute",fit:"contain",layer:"foreground"
    });
    root.panel("frame",{
        x:10,y:10,width:320,height:170,padding:8,gap:5,
        background:"#1c2936ee"
    })
    .button("button","CLICK ME",{width:200,height:40,style:"cyberpunk",fx_origin:3,primary:false},"frame")
    .input("field",{width:200,height:38,text:""},"frame")
    .onPointer("button",event=>{
        if(event.value===true) {
            const voice=engine.audio.playNac("audio/ui.nac","menu_hover",0,0.25);
            engine.save.write("ui_sound_hover",JSON.stringify({voice}));
        }
    })
    .onPress("button",event=>{
        if(event.value===true) {
            const voice=engine.audio.playNac("audio/ui.nac","menu_press",0,0.30);
            engine.save.write("ui_sound_press",JSON.stringify({voice}));
        }
    })
    .onClick("button",event=>{
        const voice=engine.audio.playNac("audio/ui.nac","menu_confirm",0,0.45);
        engine.save.write("ui_sound_confirm",JSON.stringify({voice}));
        engine.save.write("ui_click",JSON.stringify({
            id:event.id,type:event.name,value:event.value
        }));
        // Same 185ms model as KaylaNightCity, tested with the actual
        // Win32 UI provider and paused-independent runtime UI tick.
        clickActivationRemaining=0.185;
    })
    .onChange("field",event=>{
        engine.save.write("ui_change",JSON.stringify({
            id:event.id,type:event.name,value:event.value
        }));
        Session.quit();
    }).mount();
    // Release FPS mouse capture so interactive UI owns pointer/text events.
    Session.pause(true);
    engine.save.write("scripting_start",JSON.stringify({
        mounted:root.mounted,size:root.size
    }));
    engine.save.write("engine_version",JSON.stringify({
        value:engine.render.provider.engineVersion()
    }));
});
let uiTicks=0;
Events.on("ui.update",dt=>{
    if (clickActivationRemaining!==null && Number.isFinite(dt) && dt>0) {
        clickActivationRemaining-=Math.min(dt,0.10);
        if(clickActivationRemaining<=0) {
            clickActivationRemaining=null;
            engine.save.write("ui_delayed_activation",JSON.stringify({
                finished:true,action:"open_settings"
            }));
        }
    }
    // This must run even though the title fixture pauses gameplay.
    // Standard engine "update" is intentionally suppressed while paused.
    if (++uiTicks===3) {
        engine.save.write("ui_tick_while_paused",JSON.stringify({
            frames:uiTicks,seconds:dt,valueType:typeof dt
        }));
    }
});
Events.on("stop",()=>root?.dispose());
"""
def classname(hwnd):
    buffer=ctypes.create_unicode_buffer(150)
    user32.GetClassNameW(hwnd,buffer,len(buffer))
    return buffer.value
def viewport(pid):
    windows=[]
    @Proc
    def top(hwnd,_):
        result=wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd,ctypes.byref(result))
        if result.value==pid and classname(hwnd)=="NorthStarCppWindow":
            windows.append(hwnd)
        return True
    user32.EnumWindows(top,0)
    if not windows:return None
    children=[]
    @Proc
    def child(hwnd,_):
        if classname(hwnd)=="NorthStarCppViewport":children.append(hwnd)
        return True
    user32.EnumChildWindows(windows[0],child,0)
    return children[0] if children else None
def click(hwnd,x,y):
    pt=((y&65535)<<16)|(x&65535)
    user32.SendMessageW(hwnd,0x201,1,pt)
    user32.SendMessageW(hwnd,0x202,0,pt)
def verify(player:Path):
    with tempfile.TemporaryDirectory(prefix="northstar-ui-interactive-") as temp:
        path=Path(temp)
        project=path/"Project"
        # Hermetic UI fixture: no FPS prefab, weapon, skinned animation, or
        # material import can block pointer/render acceptance.
        project.mkdir()
        shutil.copy2(ROOT/"Projects/KaylaNightCity/menu.nscn",project/"menu.nscn")
        (project/"assets"/"scripts").mkdir(parents=True)
        (project/"assets/scripts/main.js").write_text(SCRIPT,encoding="utf-8")
        src=ROOT/"Projects/KaylaNightCity/assets"
        (project/"assets/textures").mkdir(parents=True)
        shutil.copy2(src/"textures/menu.ytd",project/"assets/textures/menu.ytd")
        shutil.copytree(src/"shaders",project/"assets/shaders")
        (project/"assets/audio").mkdir()
        shutil.copy2(src/"audio/ui.nac",project/"assets/audio/ui.nac")
        appdata=path/"AppData"
        appdata.mkdir()
        env=os.environ.copy()
        env["LOCALAPPDATA"]=str(appdata)
        env["NORTHSTARCPP_PHYSICS_PROVIDER"]="jolt"
        env["NORTHSTARCPP_RENDER_PROVIDER"]=os.environ.get("NORTHSTARCPP_TEST_UI_RENDER_PROVIDER","d3d12")
        cmd=[str(player.resolve())]+(["--warp"] if
            env["NORTHSTARCPP_RENDER_PROVIDER"]=="d3d12" else [])+[
            "--explore","--scene",
            str((project/"menu.nscn").resolve()),"--shared-assets",
            str((ROOT/"engine/northstarcpp/resources/sharedAssets").resolve())]
        with (path/"out.log").open("wb") as out,(path/"err.log").open("wb") as err:
            proc=subprocess.Popen(cmd,cwd=player.parent,env=env,
                stdout=out,stderr=err)
            try:
                deadline=time.monotonic()+16
                window=None
                while time.monotonic()<deadline:
                    if proc.poll() is not None:break
                    window=viewport(proc.pid)
                    if window:
                        try:
                            read_marker(appdata,"scripting_start")
                            break
                        except Exception:pass
                    time.sleep(.15)
                if not window:
                    raise RuntimeError("UI viewport did not appear")
                time.sleep(.6) # ensure raster context created on the first draw
                # Optional pixel-level press acceptance (not a hard
                # requirement if PrintWindow cannot capture this GPU path).
                visual=None
                capture_fn=None
                folder=ROOT.parent/"cache"/"northstar_press_visual"
                try:
                    from editor_workspace_visual_smoke import capture as capture_fn
                    folder.mkdir(parents=True,exist_ok=True)
                    visual=capture_fn(window,folder/"idle.png")
                    print("UI idle reference:",folder/"idle.png")
                except Exception as capture_error:
                    capture_fn=None
                    print("UI visual capture unavailable:",capture_error)
                if capture_fn:
                    pt=((40&65535)<<16)|(40&65535)
                    user32.SendMessageW(window,0x201,1,pt)
                    try:
                        time.sleep(.075)
                        pressed=capture_fn(window,folder/"held.png")
                    except Exception as capture_error:
                        pressed=None
                        print("Held-key visual capture unavailable:",capture_error)
                    finally:
                        user32.SendMessageW(window,0x202,0,pt)
                    try:
                        time.sleep(.055)
                        released=capture_fn(window,folder/"release.png")
                        if pressed and visual and visual[:2]==pressed[:2]:
                            width,height,original=visual
                            active=pressed[2]
                            # Check the on-screen button's actual pixel
                            # response, not only shader compilation success.
                            diff=0
                            for y in range(17,min(59,height)):
                                for x in range(18,min(220,width)):
                                    idx=(y*width+x)*4
                                    diff+=sum(abs(original[idx+c]-active[idx+c])
                                              for c in range(3))
                            print("GPU held-button pixel difference:",diff,
                                  "captures:",folder)
                    except Exception as capture_error:
                        print("Release capture unavailable:",capture_error)
                else:
                    click(window,40,40)
                try:
                    button=read_marker(appdata,"ui_click")
                except Exception:
                    time.sleep(.4)
                    click(window,40,40)
                    time.sleep(.2)
                    button=read_marker(appdata,"ui_click")
                time.sleep(.28) # complete 185ms game-menu activation
                click(window,40,85)
                user32.SendMessageW(window,0x102,ord("Z"),0)
                proc.wait(timeout=13)
                # Actual engine.audio.playNac() must resolve the five-track
                # project archive and play hover/press/confirm without throwing.
                hover_sound=read_marker(appdata,"ui_sound_hover")
                press_sound=read_marker(appdata,"ui_sound_press")
                confirm_sound=read_marker(appdata,"ui_sound_confirm")
                for sound in (hover_sound,press_sound,confirm_sound):
                    assert isinstance(sound["voice"],int) and sound["voice"]>=0,sound
                delayed=read_marker(appdata,"ui_delayed_activation")
                assert delayed=={"finished":True,"action":"open_settings"},delayed
                input_event=read_marker(appdata,"ui_change")
                version_event=read_marker(appdata,"engine_version")
                ui_tick=read_marker(appdata,"ui_tick_while_paused")
                assert ui_tick["frames"]==3 and ui_tick["seconds"]>=0,ui_tick
                print("UI tick runtime payload:",ui_tick)
                import re
                match=re.search(r"project\(NorthStarCpp VERSION ([0-9]+\.[0-9]+\.[0-9]+)",
                                (ROOT/"CMakeLists.txt").read_text(encoding="utf-8"))
                assert match, "CMake PROJECT_VERSION not found"
                assert version_event["value"]==match.group(1), (
                    "Runtime engine version differs from binary's declared version",
                    version_event,match.group(1))
                assert button["id"]=="clicktest.button" and button["type"]=="click",button
                assert input_event["id"]=="clicktest.field" and input_event["value"]=="Z",input_event
                assert proc.returncode==0,proc.returncode
                log=(path/"err.log").read_text(encoding="utf-8",errors="replace")
                active=env["NORTHSTARCPP_RENDER_PROVIDER"]
                shader="shaders/ui/menu_hover.hlsl" if active=="d3d12" else (
                    "shaders/ui/menu_hover.frag.spv")
                for expected in [
                    "Project UI shader loaded: "+shader,
                    "UI image loaded: textures/menu.ytd@bg",
                    "UI image loaded: textures/menu.ytd@logo",
                ]:
                    assert expected in log, ("Missing active project resource",expected,log[-2500:])
                print("Project HLSL/SPIR-V + bg/logo YTD runtime loading: PASS",active)
                print("Native UI click/change: PASS (Win32 pointer/keyboard -> provider -> Viewport -> V8)")
            except Exception as e:
                if proc.poll() is None:
                    proc.kill();proc.wait(timeout=5)
                errtext=(path/"err.log").read_text(errors="replace")
                raise RuntimeError(str(e)+"\n"+errtext[-3000:]) from e
if __name__=="__main__":
    verify(Path(sys.argv[1]))
