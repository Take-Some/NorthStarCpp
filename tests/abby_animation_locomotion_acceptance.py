"""Real Player / Jolt / V8 acceptance of Abby's animation and bone-held pistol.

Copies the project to an isolated temp directory. It changes no authored scene
or source prefab, and exits after simulated movement phases.
"""
from pathlib import Path
import json, os, shutil, subprocess, sys, tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.v8_provider.player_e2e import read_marker

QA_JS = r"""
const Q=NorthStar.modules.engine;
const origDown=Q.Input.isDown.bind(Q.Input);
const origAnimation=Q.Scene.playAnimation.bind(Q.Scene);
const clips=[];
const positions=[];
let frame=0;
Q.Input.isDown=function(key) {
    const move=frame>=55&&frame<225;
    // Turn back before reaching the Torus wall so all three gait bands
    // actually exercise Jolt motion rather than collision response.
    if(key===Q.Keys.W && move) return frame<112;
    if(key===Q.Keys.S && move) return frame>=112;
    if(key===Q.Keys.Control && frame>=55 && frame<111) return true;
    if(key===Q.Keys.Shift && frame>=170 && frame<225) return true;
    return origDown(key);
};
Q.Scene.playAnimation=function(index,ref,rate=1) {
    clips.push({frame,index,ref,rate});
    return origAnimation(index,ref,rate);
};
Q.Events.on("update",()=>{
    frame++;
    if(frame===10 || frame===100 || frame===165 ||
       frame===212 || frame===277) {
        const pawn=Q.Scene.findPawn();
        const pistol=Q.Scene.findByName("Pistol");
        let muzzle=null;
        if(pistol) {
            try { muzzle=Q.Scene.attachmentPoint(
                pistol.index,Q.Player.config.weapon.muzzleLocal); }
            catch(e) { muzzle={error:String(e)}; }
        }
        positions.push({frame,pawn:pawn?.position??null,
            pistol:pistol?.position??null,muzzle});
    }
    if(frame>=278) {
        engine.save.write("abby_locomotion_acceptance",
            JSON.stringify({frame,clips,positions}));
        engine.session.quit();
    }
});
"""
def run(player):
    player=Path(player).resolve(strict=True)
    output=ROOT/"out/qa_abby_animation"
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="northstar-anim-qa-") as directory:
        base=Path(directory)
        project=base/"project"
        shutil.copytree(ROOT/"Projects/AbbyPistolDemo",project)
        script=project/"assets/scripts/main.js"
        script.write_text(script.read_text("utf8")+"\n"+QA_JS,encoding="utf8")
        appdata=base/"localapp"; appdata.mkdir()
        logs=base/"logs";logs.mkdir()
        env=os.environ.copy()
        env.update({"LOCALAPPDATA":str(appdata),
            "NORTHSTARCPP_LOG_DIRECTORY":str(logs),
            "NORTHSTARCPP_RENDER_PROVIDER":"d3d12",
            "NORTHSTARCPP_PHYSICS_PROVIDER":"jolt"})
        command=[str(player),"--warp","--explore","--scene",
                 str((project/"main.nscn").resolve())]
        if os.environ.get("NORTHSTARCPP_QA_ARCHIVE_ONLY")!="1":
            command.extend(["--shared-assets",
                 str((ROOT/"engine/northstarcpp/resources/sharedAssets").resolve())])
        try:
            finished=subprocess.run(command,cwd=player.parent,env=env,
                                    capture_output=True,timeout=80)
            exit_code=finished.returncode
            stderr=finished.stderr.decode("utf8","replace")
        except subprocess.TimeoutExpired as e:
            exit_code="TIMEOUT"
            stderr=(e.stderr or b"").decode("utf8","replace")
        (output/"player_stderr.log").write_text(stderr,"utf8")
        try:result=read_marker(appdata,"abby_locomotion_acceptance")
        except Exception as e:result={"marker_error":str(e),"clips":[],"positions":[]}
        result.update({"playerExitCode":exit_code,
            "animationStartError":"scene.setAnimation(index,clip_reference" in stderr,
            "animatedSocketLog":"bone_attachment=PASS" in stderr,
            "ikGripSolved":"fps_grip=SOLVED" in stderr,
            "skinMaskLog":"first-person viewmodel arm vertices=" in stderr,
            "rendererInitialized":"loaded sky.ytd/clouds.ytd" in stderr})
        records=result.get("clips",[])
        moving=[k for k in ["walk-loop","run-loop","sprint-loop"]
            if any(k in c.get("ref","") for c in records)]
        result["movingClipsSeen"]=moving
        points=result.get("positions",[])
        result["muzzleAvailableAtRest"]=any(
            isinstance(q.get("muzzle"),dict) and "x" in q["muzzle"]
            for q in points if q.get("frame",0)>=100)
        result["passed"]=bool(exit_code==0 and not result["animationStartError"]
            and result["animatedSocketLog"] and result["ikGripSolved"]
            and result["skinMaskLog"]
            and len(moving)==3 and result["muzzleAvailableAtRest"])
        (output/"last_report.json").write_text(json.dumps(
            result,ensure_ascii=False,indent=2),"utf8")
        print(json.dumps(result,ensure_ascii=False))
        return result["passed"]

if __name__=="__main__":
    raise SystemExit(0 if run(sys.argv[1] if len(sys.argv)>1
        else ROOT/"out/build/northstarcpp_player.exe") else 1)
