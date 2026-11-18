"""Real KaylaNightCity menu click -> settings callback on a running Player.

Usage: python tests/kaylanightcity_menu_navigation_runtime_smoke.py
Temporary project copies only. Original project and production builds stay unchanged.
"""
from pathlib import Path
import os, shutil, subprocess, sys, tempfile, time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tests"))
from ui_interaction_runtime_smoke import viewport, click, read_marker

SOURCE=ROOT/"Projects/KaylaNightCity"
PLAYER=ROOT/"out/build/northstarcpp_player.exe"
assert PLAYER.is_file(),PLAYER

def run(provider):
    with tempfile.TemporaryDirectory(prefix="kaylanightcity-menu-nav-") as temp:
        temp=Path(temp)
        project=temp/"Project"
        project.mkdir()
        shutil.copy2(SOURCE/"menu.nscn",project/"menu.nscn")
        (project/"assets"/"scripts").mkdir(parents=True)
        script=(SOURCE/"assets/scripts/main.js").read_text(encoding="utf-8")
        before='''    #switchScreen(screen, returnScreen = "main") {
        this.#screen = screen;'''
        after='''    #switchScreen(screen, returnScreen = "main") {
        engine.save.write("menu_action_screen", JSON.stringify({screen}));
        this.#screen = screen;'''
        assert script.count(before)==1
        (project/"assets/scripts/main.js").write_text(
            script.replace(before,after),encoding="utf-8")
        assets=project/"assets"
        (assets/"textures").mkdir(parents=True)
        shutil.copy2(SOURCE/"assets/textures/menu.ytd",
                     assets/"textures/menu.ytd")
        shutil.copytree(SOURCE/"assets/shaders",assets/"shaders")
        shutil.copytree(SOURCE/"assets/ui",assets/"ui")
        (assets/"audio").mkdir()
        shutil.copy2(SOURCE/"assets/audio/ui.nac",assets/"audio/ui.nac")
        local=temp/"AppData"
        local.mkdir()
        env=os.environ.copy()
        env["LOCALAPPDATA"]=str(local)
        env["NORTHSTARCPP_RENDER_PROVIDER"]=provider
        env["NORTHSTARCPP_PHYSICS_PROVIDER"]="jolt"
        command=[str(PLAYER.resolve())]+(
            ["--warp"] if provider=="d3d12" else [])+[
            "--explore","--scene",str((project/"menu.nscn").resolve()),
            "--shared-assets",str((ROOT/"engine/northstarcpp/resources/sharedAssets").resolve())
        ]
        with (temp/"out.log").open("wb") as out, (temp/"err.log").open("wb") as err:
            proc=subprocess.Popen(command,cwd=PLAYER.parent,env=env,
                                  stdout=out,stderr=err)
            try:
                window=None
                for _ in range(85):
                    if proc.poll() is not None:break
                    window=viewport(proc.pid)
                    if window:break
                    time.sleep(.15)
                if not window:raise AssertionError(
                    "Missing Player window\n"+(temp/"err.log").read_text(errors="replace")[-1500:])
                time.sleep(.7)
                # Card x=76+25, menu buttons 482x56. Settings is second:
                # y=260+25+2+10+38+10+56+10 =411...467.
                click(window,200,432)
                marker=None
                for _ in range(34):
                    try:
                        marker=read_marker(local,"menu_action_screen")
                        if marker:break
                    except Exception:pass
                    time.sleep(.1)
                print(provider,"actual project Settings click:",marker)
                if not marker or marker.get("screen")!="settings":
                    raise AssertionError("Settings callback did not run: "+
                        str(marker)+"\n"+(temp/"err.log").read_text(errors="replace")[-2500:])
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)

if __name__=="__main__":
    for provider in ("d3d12","vulkan"):
        run(provider)
    print("PASS actual KaylaNightCity Settings callback on both GPU providers")
