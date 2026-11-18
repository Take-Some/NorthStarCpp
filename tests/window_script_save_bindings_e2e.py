"""Live native ScriptRuntime integration of the private project-save capability.

Requires a working hardware render provider; intended as an explicit QA run,
not part of a headless/CI CTest suite. Source project is never modified.
Usage: python tests/window_script_save_bindings_e2e.py out/build/northstarcpp_player_script_bindings_qa.exe
"""
from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SCRIPT=r'''import { Events } from "@northstar/engine";
let frames=0;
Events.on("start", () => {
    // Verify that every App-free domain registrar installed callable methods.
    const required={
        scene:["objectCount","objects","spawn"],
        camera:["available","mode","cut"],
        ecs:["entities","query","queryMany"],
        input:["isDown","mouseDown","pointerDelta","wheelDelta","gamepad"],
        player:["controlledEntity","controlledIndex","animationState","possess",
                "unpossess","sprint","move","motion","ground","jump","eye","setEyeHeight"],
        audio:["play","playNac","supportsVoices","stopVoice",
               "setVoiceGain","setVoicePosition"]
    };
    for (const [domain,methods] of Object.entries(required))
        for (const method of methods)
            if (typeof engine[domain]?.[method] !== "function")
                throw new Error("Missing engine."+domain+"."+method);
    if (typeof engine.scene.objectCount() !== "number" ||
        typeof engine.camera.available() !== "boolean" ||
        !Array.isArray(engine.ecs.entities()) ||
        typeof engine.player.motion()?.x !== "number" ||
        typeof engine.audio.supportsVoices() !== "boolean")
        throw new Error("Script domain callback returns invalid value");
    if (typeof engine.input.isDown(0) !== "boolean" ||
        typeof engine.input.mouseDown(0) !== "boolean" ||
        !Number.isFinite(engine.input.pointerDelta().x) ||
        !Number.isFinite(engine.input.wheelDelta()))
        throw new Error("Independent native input callback is invalid");
    const payload={schema:"binding_schema_v1",value:127};
    if (engine.save.exists("script_bindings_temp"))
        engine.save.remove("script_bindings_temp");
    if (engine.save.write("script_bindings_temp",JSON.stringify(payload))!==true)
        throw new Error("engine.save.write failed");
    if (!engine.save.exists("script_bindings_temp"))
        throw new Error("engine.save.exists missed the new slot");
    const found=JSON.parse(engine.save.read("script_bindings_temp"));
    if (found.schema!==payload.schema || found.value!==payload.value)
        throw new Error("engine.save.read returned the wrong payload");
    if (!engine.save.remove("script_bindings_temp") ||
        engine.save.exists("script_bindings_temp"))
        throw new Error("engine.save.remove did not remove the slot");
    engine.save.write("script_bindings_pass",
        JSON.stringify({schema:"binding_schema_v1",ok:true}));
});
Events.on("update", () => { if (++frames===5)engine.session.quit(); });
'''

def main() -> None:
    exe=Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="northstar-script-save-") as folder:
        tmp=Path(folder)
        project=tmp/"project"
        shutil.copytree(ROOT/"Projects/NorthStarFirstPersonPlatform",project)
        script=project/"assets/scripts/main.js"
        assert script.is_file(),script
        script.write_text(SCRIPT,encoding="utf-8")
        config=project/"assets/scripts/playerConfig.js"
        config.parent.mkdir(parents=True,exist_ok=True)
        config.write_text("NorthStar.player.configure({enabled:false});\n",encoding="utf-8")
        appdata=tmp/"LocalAppData"
        appdata.mkdir()
        env=os.environ.copy()
        env["LOCALAPPDATA"]=str(appdata)
        env["NORTHSTARCPP_RENDER_PROVIDER"]=os.environ.get("NORTHSTARCPP_TEST_BACKEND","d3d12")
        env["NORTHSTARCPP_PHYSICS_PROVIDER"]="jolt"
        cmd=[str(exe),"--fps","--scene",str(project/"main.nscn")]
        try:
            completed=subprocess.run(cmd,cwd=exe.parent,env=env,
                stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                text=True,errors="replace",timeout=55)
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError("Player did not exit after the native save-binding smoke") from exc
        if completed.returncode:
            raise RuntimeError(f"Player exit={completed.returncode}\n{completed.stderr[-3500:]}")
        save_root=appdata/"Kaylas Systems"/"NorthStarCpp"/"Saves"
        found=False
        for entry in save_root.rglob("*") if save_root.exists() else []:
            if entry.is_file() and entry.stat().st_size<1024*1024:
                if b'"schema":"binding_schema_v1"' in entry.read_bytes() and b'"ok":true' in entry.read_bytes():
                    found=True
                    break
        if not found:
            raise AssertionError(f"Native script did not persist binding success into {save_root}; stderr: {completed.stderr[-2800:]}")
        print(f"PASS: native engine.save write/read/exists/remove and script lifecycle; {env['NORTHSTARCPP_RENDER_PROVIDER']}")

if __name__=="__main__":
    main()
