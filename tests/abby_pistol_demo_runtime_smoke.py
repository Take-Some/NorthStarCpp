"""Runtime Jolt/V8 integration test for the independent AbbyPistolDemo project.

Temporarily adds QA callbacks to a copied project; never edits authored
project files. Requires a built player, scripting provider, and a Windows
desktop session with WARP-capable D3D12.
"""
from __future__ import annotations
import json
import re
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.v8_provider.player_e2e import read_marker

QA = r'''
const Events=NorthStar.modules.engine.Events;
let abbyQaFrames=0;
NorthStar.modules.engine.Events.on("start",()=>{
    const pawn=Scene.findPawn();
    const weapon=Scene.findByName("Pistol");
    const authoredVisuals=[...Scene.iterate()].filter(entity=>
        entity && (entity.name==="SkeletalMesh" || entity.name==="Abby"));
    if(!pawn || pawn.name!=="Demo Pawn" || Player.index!==pawn.index ||
       Player.entityId!=="1" || !weapon ||
       authoredVisuals.length!==1 || authoredVisuals[0].id!=="44" ||
       Scene.objectCount()!==45)
        throw new Error("Abby/Pawn/Pistol runtime composition or duplicate visual failed");
    engine.save.write("scripting_start",JSON.stringify({
        phase:"start",pawn:Player.entityId,index:Player.index,
        objects:Scene.objectCount(),weapon:weapon.name,eye:Player.eye()
    }));
});
Events.on("update",(dt)=>{
    abbyQaFrames++;
    const weapon=NorthStar.modules.engine.Scene.findByName("Pistol");
    if(!weapon)throw new Error("Pistol missing during animation QA");
    if(abbyQaFrames===2)
        NorthStar.modules.engine.Scene.playAnimation(weapon.index,
            "animations/weapon/pistol/military-pistol.ycd@p-fire");
    if(abbyQaFrames===3)
        NorthStar.modules.engine.Scene.playAnimation(weapon.index,
            "animations/weapon/pistol/military-pistol.ycd@p-reload");
    if(abbyQaFrames===4)
        NorthStar.modules.engine.Scene.playAnimation(weapon.index,
            "animations/weapon/pistol/military-pistol.ycd@idle");
    if(abbyQaFrames===5) {
        const weapon=Scene.findByName("Pistol");
        if(!weapon || ![weapon.position.x,weapon.position.y,
                         weapon.position.z].every(Number.isFinite))
            throw new Error("Pistol stopped following Abby");
        engine.save.write("scripting_update",JSON.stringify({
            phase:"update",frames:abbyQaFrames,weapon:weapon.position,dt
        }));
        engine.session.quit();
    }
});
Events.on("stop",()=>{
    engine.save.write("scripting_stop",JSON.stringify({
        phase:"stop",frames:abbyQaFrames
    }));
});
'''


QA_INDEPENDENT = r"""
const Events=NorthStar.modules.engine.Events;
const QaPrefab=NorthStar.modules.engine.Prefab;
let qaCharacter=null,qaFrames=0;
Events.on("start",()=>{
    // No character controller and no equipped weapon are required.
    qaCharacter=QaPrefab.instantiate("prefabs/abby.nprefab",{
        position:{x:4,y:0,z:9}
    });
    const abby=Scene.findByName("Abby");
    if(!qaCharacter?.alive || !abby ||
       Scene.findByName("Pistol") || Player.enabled)
        throw new Error("Independent Abby prefab improperly coupled to Player/weapon");
    // An NPC can override the prefab's authored initial idle with another YCD.
    Scene.playAnimation(qaCharacter.root.index,
      "characters/abby/animations/mm-explore.ycd@abby-mm-explore-walk-loop-fw");
    engine.save.write("scripting_start",JSON.stringify({
        phase:"start",prefab:"Abby",npc:abby.name,weapon:false,player:false
    }));
});
Events.on("update",()=>{
    ++qaFrames;
    if(qaFrames===5) {
        engine.save.write("scripting_update",JSON.stringify({
            phase:"update",frames:qaFrames,npcAlive:qaCharacter.alive
        }));
        engine.session.quit();
    }
});
Events.on("stop",()=>{
    if(qaCharacter)qaCharacter.destroy();
    engine.save.write("scripting_stop",JSON.stringify({
        phase:"stop",frames:qaFrames
    }));
});
"""

def run(player:Path,seconds:int=30,no_weapon:bool=False):
    player=player.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="northstar-abby-qa-",ignore_cleanup_errors=True) as tmp:
        root=Path(tmp)
        game=root/"game"
        shutil.copytree(ROOT/"Projects/AbbyPistolDemo",game)
        if os.environ.get("NORTHSTARCPP_QA_NO_ARMS")=="1":
            config=game/"scripts/playerConfig.js"
            config.write_text(config.read_text("utf-8").replace(
                "firstPersonArms: true","firstPersonArms: false"),encoding="utf-8")
        if no_weapon:
            config=game/"scripts/playerConfig.js"
            source=config.read_text("utf-8")
            assert source.count("Player.configure({\n    enabled: true,")==1
            source=source.replace("Player.configure({\n    enabled: true,",
                "Player.configure({\n    enabled: false,",1)
            assert source.count("    weapon: {\n        enabled: true,")==1
            source=source.replace("    weapon: {\n        enabled: true,",
                "    weapon: {\n        enabled: false,",1)
            config.write_text(source,encoding="utf-8")
        entry=game/"scripts/main.js"
        source=entry.read_text("utf-8")
        # Only the QA copy drops the HUD when testing native skinning on
        # providers that do not implement the optional GPU UI compositor.
        if os.environ.get("NORTHSTARCPP_QA_NO_HUD")=="1":
            first=source.index("    #hud() {")
            last=source.index("    #follow() {",first)
            source=source[:first]+"    #hud() {}\n\n"+source[last:]
        entry.write_text(source+"\n"+
            (QA_INDEPENDENT if no_weapon else QA),encoding="utf-8")
        appdata=root/"LocalAppData"
        appdata.mkdir()
        logs=root/"logs"
        logs.mkdir()
        env=os.environ.copy()
        env["LOCALAPPDATA"]=str(appdata)
        env["NORTHSTARCPP_LOG_DIRECTORY"]=str(logs)
        env["NORTHSTARCPP_PHYSICS_PROVIDER"]="jolt"
        env["NORTHSTARCPP_RENDER_PROVIDER"]=os.environ.get("NORTHSTARCPP_QA_RENDER_PROVIDER","d3d12")
        cmd=[str(player),*([] if os.environ.get("NORTHSTARCPP_QA_HW")=="1" else ["--warp"]),
            "--explore","--scene",
            str((game/"main.nscn").resolve()),"--shared-assets",
            str((ROOT/"engine/northstarcpp/resources/sharedAssets").resolve())]
        stdout=root/"stdout.log";stderr=root/"stderr.log"
        with stdout.open("wb") as out,stderr.open("wb") as err:
            proc=subprocess.Popen(cmd,cwd=player.parent,env=env,stdout=out,stderr=err)
            timed_out=False
            try:proc.wait(seconds)
            except subprocess.TimeoutExpired:
                timed_out=True
                proc.kill()
                proc.wait(10)
        errors=stderr.read_text("utf-8",errors="replace")
        loglines=[]
        for log in logs.glob("*.log"):
            loglines.extend(log.read_text("utf-8",errors="replace").splitlines()[-25:])
        skydome_ok=any("phase=renderer.skydome status=PASS" in line
                       for log in logs.glob("*.log")
                       for line in log.read_text("utf-8",errors="replace").splitlines())
        viewmodel_lines=[line for line in errors.splitlines()
            if "[engine.character]" in line or "[WARN]" in line or "[PERF]" in line or "YCD animation" in line]
        if timed_out or proc.returncode!=0:
            raise RuntimeError(
                f"Native Player exit={proc.returncode}, timed_out={timed_out}\n"
                + errors[-18000:]+"\n" + "\n".join(loglines[-90:]))
        if not skydome_ok:
            raise RuntimeError("Renderer did not complete authored skydome bootstrap")
        if no_weapon:
            markers={name:read_marker(appdata,name)
                     for name in ("scripting_start","scripting_update","scripting_stop")}
            assert markers["scripting_start"]["npc"]=="Abby",markers
            assert markers["scripting_start"]["player"] is False,markers
            assert markers["scripting_start"]["weapon"] is False,markers
            assert markers["scripting_update"]["npcAlive"] is True,markers
            assert markers["scripting_stop"]["frames"]==5,markers
            assert "[engine.character] " not in errors or "arm vertices" not in errors
            print(json.dumps({"native_runtime":"PASS",
                "scenario":"independent animated Abby NPC, no Player, no weapon",
                "skydome_pass":skydome_ok,"markers":markers,
                "log_tail":loglines[-5:]},ensure_ascii=False))
            return
        if "bone_attachment=PASS" not in errors or "joint=r_hand_prop_attachment" not in errors:
            raise RuntimeError("Pistol did not resolve the animated Abby hand socket")
        markers={name:read_marker(appdata,name)
                 for name in ("scripting_start","scripting_update","scripting_stop")}
        match=re.search(r"bone_attachment=PASS[^\n]*at=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\)",errors)
        assert match, "Missing computed hand socket coordinates"
        socket=tuple(float(match.group(i)) for i in (1,2,3))
        axis_match=re.search(r"axis_z=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\)",errors)
        assert axis_match, "Missing socket muzzle-forward diagnostic"
        muzzle_axis=tuple(float(axis_match.group(i)) for i in (1,2,3))
        assert muzzle_axis[2]>0.90 and abs(muzzle_axis[0])<0.3 and abs(muzzle_axis[1])<0.3, muzzle_axis
        eye=markers["scripting_start"]["eye"]
        assert eye["x"]<socket[0]<eye["x"]+0.85, (eye,socket,viewmodel_lines[-20:])
        assert eye["y"]-0.7<socket[1]<eye["y"]+0.2, (eye,socket,viewmodel_lines[-20:])
        assert eye["z"]+0.35<socket[2]<eye["z"]+2.5, (eye,socket,viewmodel_lines[-12:])
        assert markers["scripting_start"]["pawn"]=="1",markers
        assert markers["scripting_start"]["weapon"]=="Pistol",markers
        assert markers["scripting_update"]["frames"]==5,markers
        assert markers["scripting_stop"]["phase"]=="stop",markers
        print(json.dumps({"native_runtime":"PASS","skydome_pass":skydome_ok,
            "socket_resolved":True,"socket_world":socket,
            "muzzle_forward":muzzle_axis,"viewmodel_diagnostics":viewmodel_lines[:8]+viewmodel_lines[-12:],"markers":markers,
            "log_tail":loglines[-6:]},ensure_ascii=False))

if __name__=="__main__":
    candidate=Path(sys.argv[1] if len(sys.argv)>1 else
        ROOT/"_qa_editor_mixed"/"northstarcpp_player.exe")
    run(candidate)
