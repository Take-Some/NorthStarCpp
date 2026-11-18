"""Run actual NorthStar Player + Jolt shots against isolated native Scene targets.

Uses a QA-only copy of AbbyPistolDemo. Never edits the production project.
A live rendered Player is launched with D3D12 WARP and logs saved to out/qa_pistol_aim.
"""
from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.v8_provider.player_e2e import read_marker

QA_JS = r"""
// Native Jolt integration, executed inside the SAME V8 world as gameplay.
const AimQa = NorthStar.modules.engine;
const QaDistances = [2,5,20,50];
const QaOriginalSolve = AimQa.WeaponBallistics.solve;
let qaRecordShot = false;
let qaLastShot = null;
AimQa.WeaponBallistics.solve = function(options) {
    const solved = QaOriginalSolve.call(this,options);
    if (qaRecordShot) qaLastShot = solved;
    return solved;
};
let qaStage = "boot", qaFrame = 0, qaWait = 0, qaTarget = null;
let qaIndex = 0, qaResults = [], qaInitialEye = null;
let qaFailure = null;
let qaSceneObjects = [];
const qaUnit = (p) => {
    const n = Math.hypot(p.x,p.y,p.z);
    return {x:p.x/n,y:p.y/n,z:p.z/n};
};
const qaDot=(a,b)=>a.x*b.x+a.y*b.y+a.z*b.z;
function qaWrite() {
    engine.save.write("aim_distance_acceptance",JSON.stringify({
        status:qaFailure?"ERROR":qaResults.every(x=>x.passed)?"PASS":"FAIL",
        frames:qaFrame,results:qaResults,error:qaFailure,
        eye:qaInitialEye,source:"native Player + Jolt + Scene",
        sceneObjects:qaSceneObjects
    }));
}
AimQa.Events.on("update",(dt)=>{
    try {
        qaFrame++;
        if (qaStage==="boot") {
            if (qaFrame<24) return;
            if (!qaWeaponController?.equipped) throw Error("Pistol not equipped");
            qaInitialEye=AimQa.Player.eye();
            qaSceneObjects=[...AimQa.Scene.iterate()]
                .map(x=>({index:x.index,name:x.name,position:x.position}));
            // Existing torus blocks the long-range centre lane; move only
            // the ephemeral QA copy so Jolt can test unobstructed targets.
            const torus=AimQa.Scene.findByName("Torus");
            if(torus) AimQa.Scene.moveStatic(torus.index,
                {x:30,y:torus.position.y,z:0});
            qaStage="spawn";
        }
        if (qaStage==="spawn") {
            if(qaIndex>=QaDistances.length){
                qaWrite();qaStage="done";engine.session.quit();return;
            }
            const distance=QaDistances[qaIndex];
            const eye=AimQa.Player.eye();
            const angles=AimQa.Camera.angles();
            const direction=qaUnit({
                x:Math.sin(angles.yaw)*Math.cos(angles.pitch),
                y:Math.sin(angles.pitch),
                z:Math.cos(angles.yaw)*Math.cos(angles.pitch)
            });
            const center={
                x:eye.x+direction.x*distance,
                y:eye.y+direction.y*distance,
                z:eye.z+direction.z*distance
            };
            // Only the target under test is present at each distance.
            qaTarget=AimQa.Scene.spawn({
                name:"Aim QA "+distance+"m",
                position:center,size:.55,mass:0,collision:true,
                color:0x72cde1
            });
            qaStage="wait";qaWait=0;
            return;
        }
        if (qaStage==="wait") {
            qaWait+=dt;
            if(qaWait<.45)return;
            qaStage="shoot";
        }
        if (qaStage==="shoot") {
            const targetIndex=qaTarget.index;
            const targetCenter=qaTarget.snapshot.position;
            qaLastShot=null;
            qaRecordShot=true;
            const fired=qaWeaponController.fire();
            qaRecordShot=false;
            const shot=qaLastShot;
            const muzzle=shot?.muzzle??null;
            const cameraHit=shot?.cameraRay ?
                AimQa.Collision.sweepSphere(shot.cameraRay.from,
                    shot.cameraRay.to,.006,AimQa.Player.index):null;
            const toCenter=muzzle&&{
                x:targetCenter.x-muzzle.x,
                y:targetCenter.y-muzzle.y,
                z:targetCenter.z-muzzle.z
            };
            const along=toCenter&&qaDot(toCenter,shot.direction);
            const perpendicular=toCenter&&Math.sqrt(Math.max(0,
                qaDot(toCenter,toCenter)-along*along));
            const passed=Boolean(fired&&shot&&!shot.blocked&&
                shot.mode==="sight"&&shot.hit?.index===targetIndex&&
                cameraHit?.index===targetIndex&&
                perpendicular<.18);
            qaResults.push({
                distanceMetres:QaDistances[qaIndex],fired,
                targetIndex,targetCenter,
                hitIndex:shot?.hit?.index??null,
                cameraHitIndex:cameraHit?.index??null,
                hitFraction:shot?.hit?.fraction??null,
                muzzle,aimPoint:shot?.aimPoint??null,
                muzzleToTargetErrorMetres:perpendicular,
                blocked:shot?.blocked??null,
                mode:shot?.mode??null,passed
            });
            qaTarget.destroy();qaTarget=null;qaIndex++;
            qaStage="spawn";
            return;
        }
    } catch(error) {
        qaFailure=String(error?.stack||error);
        qaRecordShot=false;
        qaWrite();qaStage="done";engine.session.quit();
    }
});
AimQa.Events.on("stop",()=>{if(qaStage!=="done")qaWrite();});
"""

def main() -> int:
    player=Path(sys.argv[1] if len(sys.argv)>1 else
        ROOT/"out/build/northstarcpp_player.exe").resolve(strict=True)
    result_root=ROOT/"out/qa_pistol_aim"
    result_root.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="northstar-aim-qa-") as t:
        base=Path(t)
        game=base/"game"
        shutil.copytree(ROOT/"Projects/AbbyPistolDemo",game)
        source_path=game/"assets/scripts/main.js"
        source=source_path.read_text(encoding="utf-8")
        assert source.count("Factory.weapon({")==1
        source=source.replace("Factory.weapon({",
                              "const qaWeaponController=Factory.weapon({",1)
        source_path.write_text(source+"\n"+QA_JS,encoding="utf-8")
        appdata=base/"AppData"
        appdata.mkdir()
        logdir=base/"logs"
        logdir.mkdir()
        env=os.environ.copy()
        env.update({
          "LOCALAPPDATA":str(appdata),
          "NORTHSTARCPP_LOG_DIRECTORY":str(logdir),
          "NORTHSTARCPP_RENDER_PROVIDER":os.environ.get("NORTHSTARCPP_QA_RENDER_PROVIDER","d3d12"),
          "NORTHSTARCPP_PHYSICS_PROVIDER":"jolt"
        })
        command=[str(player),"--warp","--explore","--scene",
            str((game/"main.nscn").resolve()),"--shared-assets",
            str((ROOT/"engine/northstarcpp/resources/sharedAssets").resolve())]
        try:
            completed=subprocess.run(command,cwd=player.parent,env=env,
                capture_output=True,timeout=75)
            stdout=completed.stdout.decode("utf-8",errors="replace")
            stderr=completed.stderr.decode("utf-8",errors="replace")
            exit_code=completed.returncode
        except subprocess.TimeoutExpired as e:
            stdout=(e.stdout or b"").decode("utf-8",errors="replace")
            stderr=(e.stderr or b"").decode("utf-8",errors="replace")
            exit_code="timeout"
        (result_root/"player_stdout.log").write_text(stdout,encoding="utf-8")
        (result_root/"player_stderr.log").write_text(stderr,encoding="utf-8")
        logs={}
        for file in logdir.glob("*.log"):
            logs[file.name]=file.read_text(encoding="utf-8",errors="replace")
        (result_root/"engine_logs.json").write_text(
            json.dumps(logs,ensure_ascii=False,indent=2),encoding="utf-8")
        try: data=read_marker(appdata,"aim_distance_acceptance")
        except Exception as e: data={"status":"NO_MARKER","error":str(e)}
        data["playerExitCode"]=exit_code
        data["rendererInitialized"]=any(
            "phase=renderer.skydome status=PASS" in l for l in logs.values())
        (result_root/"last_report.json").write_text(
            json.dumps(data,ensure_ascii=False,indent=2),encoding="utf-8")
        print(json.dumps(data,ensure_ascii=False))
        return 0 if data.get("status")=="PASS" and exit_code==0 else 1

if __name__=="__main__":
    raise SystemExit(main())
