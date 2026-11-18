"""Native V8 Scene/ECS lifecycle QA: destroy, restart, loadScene and stale IDs.

Hardware D3D12 intentionally avoids the independently failing WARP provider.
Copies source project, uses isolated LOCALAPPDATA, never edits shared assets.
"""
from __future__ import annotations
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SCRIPT=r'''import {Events} from "@northstar/engine";
const slot="script_context_lifetime_stage_v1";
const resultSlot="script_context_lifetime_pass_v1";
const raw=engine.save.read(slot);
const state=raw?JSON.parse(raw):{phase:0};
let requestedAction=null;
let frames=0;
const expect=(condition,message)=>{if(!condition)throw new Error(message)};
const stale=(fn,message)=>{
    let rejected=false;
    try {fn()} catch(error) {rejected=true}
    expect(rejected,message);
};
Events.on("start",()=>{
    if(state.phase===0){
        const spawned=engine.scene.spawn("LifeProbe",17,3,19,1,0,33333,"","QA");
        const id=spawned.id;
        expect(id && typeof id==="string","spawn returned no stable ID");
        const pos=engine.ecs.getPosition(id);
        expect(Math.abs(pos.x-17)<0.001,"Spawned stable ID did not resolve");
        expect(engine.ecs.entities().includes(id),"Spawned ECS ID not enumerated");
        expect(engine.scene.destroy(id)===true,"destroy() returned false");
        stale(()=>engine.ecs.getPosition(id),"Destroyed stable ID still resolves");
        stale(()=>engine.ecs.getScript(id),"Destroyed script ID still resolves");
        stale(()=>engine.scene.moveRuntime(id,18,3,19),"Destroyed runtime handle accepted");
        expect(!engine.ecs.entities().includes(id),"Destroyed ID still enumerable");
        engine.save.write(slot,JSON.stringify({phase:1,oldId:id}));
        requestedAction=()=>engine.session.restart();
    } else if(state.phase===1){
        const id=state.oldId;
        expect(!engine.ecs.entities().includes(id),"Stale ID survived restart");
        stale(()=>engine.ecs.getPosition(id),"Old ID resolves after restart");
        engine.save.write(slot,JSON.stringify({phase:2,oldId:id}));
        requestedAction=()=>expect(engine.session.loadScene("second.nscn")===true,"loadScene was not accepted");
    } else if(state.phase===2){
        expect(engine.session.scene().endsWith("second.nscn"),
               "Scene transition failed");
        const id=state.oldId;
        expect(!engine.ecs.entities().includes(id),"Old ID survived scene replacement");
        stale(()=>engine.ecs.getPosition(id),"Old ID resolves after loadScene");
        engine.save.write(resultSlot,
            JSON.stringify({ok:true,phase:2,scene:engine.session.scene()}));
        requestedAction=()=>engine.session.quit();
    } else {
        throw new Error("Unexpected lifecycle phase "+state.phase);
    }
});
// Start fires during App construction, before Win32 creates its HWND.
// Session commands must be deferred until the window's update loop runs.
Events.on("update",()=>{
    if(++frames===5 && requestedAction)requestedAction();
});
'''

def verify(executable: Path) -> None:
    player=executable.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="northstar-script-lifetime-") as tmp:
        base=Path(tmp)
        project=base/"project"
        shutil.copytree(ROOT/"Projects/NorthStarFirstPersonPlatform",project)
        second=project/"second.nscn"
        shutil.copy2(project/"main.nscn",second)
        script=project/"assets/scripts/main.js"
        assert script.is_file(),script
        script.write_text(SCRIPT,encoding="utf-8")
        config=project/"assets/scripts/playerConfig.js"
        config.parent.mkdir(parents=True,exist_ok=True)
        config.write_text("NorthStar.player.configure({enabled:false});\n",encoding="utf-8")
        appdata=base/"LocalAppData"
        appdata.mkdir()
        env=os.environ.copy()
        env["LOCALAPPDATA"]=str(appdata)
        env["NORTHSTARCPP_RENDER_PROVIDER"]=os.environ.get("NORTHSTARCPP_TEST_BACKEND","d3d12")
        env["NORTHSTARCPP_PHYSICS_PROVIDER"]="jolt"
        cmd=[str(player),"--fps","--scene",str(project/"main.nscn")]
        try:
            r=subprocess.run(cmd,cwd=player.parent,env=env,
                             stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                             text=True,errors="replace",timeout=75)
        except subprocess.TimeoutExpired as error:
            tail=(error.stderr or b"")
            if isinstance(tail,bytes): tail=tail.decode("utf-8","replace")
            raise RuntimeError("Scene/ECS lifecycle did not finish in 75s:\n"+tail[-6500:]) from error
        if r.returncode:
            raise RuntimeError(f"Scene/ECS lifecycle Player exit {r.returncode}: {r.stderr[-6500:]}")
        saves=appdata/"Kaylas Systems"/"NorthStarCpp"/"Saves"
        markers=[]
        for path in saves.rglob("*") if saves.exists() else []:
            if path.is_file() and path.stat().st_size<1_000_000:
                content=path.read_bytes()
                if b'"ok":true' in content and b'"phase":2' in content and b'second.nscn' in content:
                    markers.append(path)
        if not markers:
            raise AssertionError(
                f"destroy/restart/loadScene marker missing in {saves}\n{r.stderr[-7000:]}")
        print(f"PASS: real V8 destroy -> stale ECS rejected -> restart -> loadScene; {env['NORTHSTARCPP_RENDER_PROVIDER']}")

if __name__=="__main__":
    verify(Path(sys.argv[1]))
