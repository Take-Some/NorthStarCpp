"""Profile the real AbbyPistolDemo FPS/TPS runtime on a hardware renderer.

Stages an ephemeral project copy; original assets/scripts and installed game
binaries remain unchanged. Produces CPU frame-time quantiles from exactly
1000 frames and keeps complete startup/runtime logs. Rendering time may
include VSync; GPU timestamps are measured by a separate renderer probe.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

QA_SOURCE = r'''
(() => {
    const api = NorthStar.modules.engine;
    const Camera = api.Camera;
    const Scene = api.Scene;
    const Events = api.Events;
    let frame = 0;
    let initialCount = -1;
    let initialVisual = null;
    const view = "VIEW_MODE";
    const abbyVisuals = () => [...Scene.iterate()].filter(entity =>
        entity && (entity.name === "SkeletalMesh" || entity.name === "Abby"));
    Events.on("start", () => {
        const visuals = abbyVisuals();
        const pistol = Scene.findByName("Pistol");
        if (visuals.length !== 1 || !pistol)
            throw new Error("Expected one Abby visual and one equipped Pistol for QA profile");
        initialVisual = visuals[0].id;
        initialCount = Scene.objectCount();
    });
    Events.on("update", () => {
        ++frame;
        if (frame === 1 && view === "tps") Camera.setMode("third_person",0);
        if (frame === 300 && view === "mixed") Camera.setMode("third_person",0);
        if (frame === 500 && view === "mixed") Camera.setMode("first_person",0);
        if (frame >= FRAME_GOAL) {
            const visuals = abbyVisuals();
            if (visuals.length !== 1 || visuals[0].id !== initialVisual ||
                !Scene.findByName("Pistol") || Scene.objectCount() !== initialCount)
                throw new Error("FPS/TPS lost its persistent entities");
            api.Logger.info("[QA FPS] frames=" + frame +
                " view=" + view + " mode=" + Camera.mode() +
                " entities=" + Scene.objectCount());
            engine.session.quit();
        }
    });
})();
'''

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("player", type=Path)
    parser.add_argument("--view", choices=("fps","tps","mixed"), default="fps")
    parser.add_argument("--backend", choices=("d3d12","vulkan"), default="d3d12")
    parser.add_argument("--frames", type=int, default=1000)
    parser.add_argument("--out", type=Path, default=ROOT/"_build_full_clean_2026-11-16"/"qa_fps_profiles")
    parser.add_argument("--timeout", type=int, default=170)
    args = parser.parse_args()
    if args.frames < 1000 or args.frames > 100000:
        raise ValueError("Profiler requires 1000..100000 frames")
    out = args.out.resolve()
    out.mkdir(parents=True,exist_ok=True)
    player = args.player.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="northstar-fps-profiler-") as folder:
        working = Path(folder)
        project = working / "project"
        shutil.copytree(ROOT/"Projects/AbbyPistolDemo",project)
        script = project/"scripts/main.js"
        qa = (QA_SOURCE.replace("VIEW_MODE",args.view)
                       .replace("FRAME_GOAL",str(args.frames+4)))
        script.write_text(script.read_text(encoding="utf-8")+
                          "\n"+qa+"\n",encoding="utf-8")
        env = os.environ.copy()
        env["NORTHSTARCPP_RENDER_PROVIDER"] = args.backend
        env["NORTHSTARCPP_PHYSICS_PROVIDER"] = "jolt"
        env["NORTHSTARCPP_PROFILE_FRAMES"] = str(args.frames)
        env["NORTHSTARCPP_LOG_DIRECTORY"] = str(out/"logs")
        env["LOCALAPPDATA"] = str(working/"LocalAppData")
        (working/"LocalAppData").mkdir()
        (out/"logs").mkdir(exist_ok=True)
        cmd=[str(player),"--explore","--scene",str((project/"main.nscn").resolve()),
             "--shared-assets",str((ROOT/"engine/northstarcpp/resources/sharedAssets").resolve())]
        timed_out = False
        try:
            proc = subprocess.run(cmd,cwd=player.parent,env=env,
                capture_output=True,text=True,errors="replace",timeout=args.timeout)
            rc=proc.returncode
            output=proc.stdout
            errors=proc.stderr
        except subprocess.TimeoutExpired as e:
            timed_out=True
            rc=124
            output=(e.stdout or b"").decode("utf-8",errors="replace") if isinstance(e.stdout,bytes) else (e.stdout or "")
            errors=(e.stderr or b"").decode("utf-8",errors="replace") if isinstance(e.stderr,bytes) else (e.stderr or "")
    prefix=args.backend+"_"+args.view
    (out/(prefix+".stdout.log")).write_text(output,encoding="utf-8",errors="replace")
    (out/(prefix+".stderr.log")).write_text(errors,encoding="utf-8",errors="replace")
    reports=[]
    for line in errors.splitlines():
        if '"event":"northstarcpp.frame_profile"' in line:
            try:reports.append(json.loads(line[line.index("{"):]))
            except ValueError:pass
    profile=reports[-1] if reports else None
    result={"renderer":args.backend,"view":args.view,
            "frames_requested":args.frames,"exit_code":rc,"timed_out":timed_out,
            "pass":rc==0 and not timed_out and profile is not None and
                profile.get("frames")==args.frames,
            "profile":profile,
            "grip_diagnostics":[line for line in errors.splitlines()
                if "fps_grip=" in line or "bone_attachment=PASS" in line][:9],
            "qa_markers":[line for line in errors.splitlines()
                if "[QA FPS]" in line][-4:]}
    (out/(prefix+".json")).write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result,ensure_ascii=False))
    return 0 if result["pass"] else 1

if __name__=="__main__":
    raise SystemExit(main())
