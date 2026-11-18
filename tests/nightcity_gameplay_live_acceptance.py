"""Real KaylaNightCity gameplay QA with Jolt + V8 + renderer.
The project is hardlinked into an isolated temporary tree, and ONLY the
instrumented main.js is copy-on-written. Never mutates authored source/assets.
"""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.v8_provider.player_e2e import read_marker

QA_JS=r"""
// Read-only project instrumentation, injected into a temporary project copy.
const NorthStarNPCQA=(()=>{
    let frame=0;
    // Isolated QA must not react to the user's Escape/Interact keys.
    const physicalDown=Input.isDown.bind(Input);
    Input.isDown=function(key) {
        if(key===Keys.Escape || key===Keys.E) return false;
        return physicalDown(key);
    };
    const samples=[];
    const clips=[];
    const previous=Scene.playAnimation.bind(Scene);
    Scene.playAnimation=function(index,clip,rate=1) {
        clips.push({frame,index,clip,rate});
        return previous(index,clip,rate);
    };
    Events.on("update",()=>{
        frame++;
        if(frame===1 || frame===90 || frame===180 ||
           frame===270 || frame===390 || frame===520) {
            const sample={frame,actors:{}};
            for(const name of ["NPC Ellie","NPC Isaac"]) {
                const a=Scene.handleByName(name);
                const p=a?.position;
                sample.actors[name]=p?{
                    id:String(a.id),x:p.x,y:p.y,z:p.z
                }:null;
            }
            samples.push(sample);
        }
        if(frame>=520) {
            try {
                Storage.write("nightcity_npc_qa",
                    JSON.stringify({frame,samples,clips}));
            } catch(error) {
                Logger.error("NPC QA marker: "+String(error));
            }
            Session.quit();
        }
    });
    return true;
})();
"""
def run(player:Path)->bool:
    player=player.resolve(strict=True)
    output=ROOT/"out/qa_nightcity_live"
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="northstar-nightcity-e2e-") as folder:
        base=Path(folder)
        project=base/"project"
        shutil.copytree(ROOT/"Projects/KaylaNightCity",project,
                        copy_function=os.link,
                        ignore=shutil.ignore_patterns(
                            "migration","docs","tests","tools",
                            "*.bak","*.bak-*","*.tmp"))
        script=project/"assets/scripts/main.js"
        source=script.read_text("utf8")
        script.unlink() # copy-on-write: never alter linked source
        script.write_text(source+"\n"+QA_JS,encoding="utf8")
        appdata=base/"localapp";appdata.mkdir()
        logs=base/"logs";logs.mkdir()
        env=os.environ.copy()
        env.update({"LOCALAPPDATA":str(appdata),
            "NORTHSTARCPP_LOG_DIRECTORY":str(logs),
            "NORTHSTARCPP_RENDER_PROVIDER":"d3d12",
            "NORTHSTARCPP_PHYSICS_PROVIDER":"jolt"})
        cmd=[str(player),"--explore","--scene",
             str((project/"main.nscn").resolve()),"--shared-assets",
             str((ROOT/"engine/northstarcpp/resources/sharedAssets").resolve())]
        if os.environ.get("NORTHSTARCPP_QA_GDB")=="1":
            cmd=["C:/msys64/mingw64/bin/gdb.exe","-q","-batch",
                 "-ex","set pagination off","-ex","run",
                 "-ex","thread apply all bt 12","--args"]+cmd
        try:
            process=subprocess.run(cmd,cwd=player.parent,env=env,
                                   capture_output=True,timeout=100)
            exit_code=process.returncode
            stderr=process.stderr.decode("utf8","replace")
            stdout=process.stdout.decode("utf8","replace")
        except subprocess.TimeoutExpired as e:
            exit_code="TIMEOUT"
            stderr=(e.stderr or b"").decode("utf8","replace")
            stdout=(e.stdout or b"").decode("utf8","replace")
        (output/"player_stderr.log").write_text(stderr,"utf8")
        (output/"player_stdout.log").write_text(stdout,"utf8")
        try: marker=read_marker(appdata,"nightcity_npc_qa")
        except Exception as error:
            marker={"marker_error":str(error),"samples":[],"clips":[]}
        marker["exit_code"]=exit_code
        marker["startup_mt28"]=(
            "[KaylaNightCity] MT28 ready: 8 wheels" in stderr)
        marker["stod_error"]="Error: stod" in stderr
        marker["ai_error"]=[
            line for line in stderr.splitlines()
            if "engine.ai" in line and ("[WARN]" in line or "[error]" in line)]
        marker["animation_errors"]=[
            line for line in stderr.splitlines()
            if "NPC YCD switch failed" in line]
        samples=marker.get("samples",[])
        displacements={}
        for actor in ("NPC Ellie","NPC Isaac"):
            points=[s.get("actors",{}).get(actor) for s in samples]
            points=[p for p in points if isinstance(p,dict)]
            if len(points)>=2:
                p0=points[0]; p1=points[-1]
                displacements[actor]=round(
                    ((p1["x"]-p0["x"])**2+
                     (p1["z"]-p0["z"])**2)**.5,4)
        marker["net_displacements"]=displacements
        marker["clips_seen"]={
            name:[c["clip"] for c in marker.get("clips",[])
                if c.get("index") is not None and
                name.split(" ")[-1].lower() in c.get("clip","")]
            for name in ("NPC Ellie","NPC Isaac")}
        marker["passed"]=bool(exit_code==0 and
            marker.get("frame",0)>=520 and marker["startup_mt28"] and
            not marker["stod_error"] and not marker["ai_error"] and
            not marker["animation_errors"] and
            all(displacements.get(n,0)>.18
                for n in ("NPC Ellie","NPC Isaac")) and
            all(any("npc-walk" in v for v in marker["clips_seen"][n])
                for n in ("NPC Ellie","NPC Isaac")))
        (output/"last_report.json").write_text(json.dumps(
            marker,ensure_ascii=False,indent=2),"utf8")
        print(json.dumps({k:marker.get(k) for k in (
            "exit_code","frame","startup_mt28","stod_error",
            "net_displacements","clips_seen","ai_error",
            "animation_errors","marker_error","passed")},
            ensure_ascii=False,indent=2))
        return marker["passed"]

if __name__=="__main__":
    raise SystemExit(0 if run(Path(sys.argv[1]) if len(sys.argv)>1
        else ROOT/"out/build/northstarcpp_player.exe") else 1)
