"""Measure actual rendered-attachment sight geometry from real Player, no keyboard injection."""
import json, os, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.v8_provider.player_e2e import read_marker
JS=r'''
const A=NorthStar.modules.engine;
let frame=0;
const cfg=A.Player.config.weapon;
// Physical iron sights recovered from the actual pistol.ydd vertices.
const rear={x:0,y:.09189,z:-.020},front={x:0,y:.09238,z:.178};
const norm=(v)=>{const l=Math.hypot(v.x,v.y,v.z);return {x:v.x/l,y:v.y/l,z:v.z/l};};
const sub=(a,b)=>({x:a.x-b.x,y:a.y-b.y,z:a.z-b.z});
const dot=(a,b)=>a.x*b.x+a.y*b.y+a.z*b.z;
let samples=[];
const mode=__MODE__;
// Inject the RMB state at the shared input facade in the temporary QA
// process. This tests the actual CharacterVisual/Input-driven ADS branch.
if(mode==="ads_input") {
  const nativeMouseDown=A.Input.mouseDown;
  A.Input.mouseDown=function(button){return button===1 || nativeMouseDown.call(this,button);};
}
A.Events.on("update",dt=>{
  frame++;
  try{
    if(frame===24 && mode!=="ads_input") {
      let actor=A.Scene.findByName("SkeletalMesh");
      if(!actor)throw Error("No SkeletalMesh");
      A.Scene.setVisualMode(actor.index,"first_person_body",mode==="ads"?
        {...A.Player.config.character.firstPersonViewmodel,
         ...cfg.grip.aimDownSights,ads:true}:
        {...A.Player.config.character.firstPersonViewmodel,ads:false});
    }
    if(frame>=25 && frame%5===0) {
      const gun=A.Scene.findByName("Pistol");
      const eye=A.Player.eye();
      const rot=A.Camera.angles();
      const forward=norm({x:Math.sin(rot.yaw)*Math.cos(rot.pitch),
           y:Math.sin(rot.pitch),z:Math.cos(rot.yaw)*Math.cos(rot.pitch)});
      const rearPoint=A.Scene.attachmentPoint(gun.index,rear);
      const frontPoint=A.Scene.attachmentPoint(gun.index,front);
      const muzzle=A.Scene.attachmentPoint(gun.index,cfg.muzzleLocal);
      const stats=(point)=> {
        if(!point)return null;
        const v=sub(point,eye),depth=dot(v,forward);
        const lateral={x:v.x-forward.x*depth,
           y:v.y-forward.y*depth,z:v.z-forward.z*depth};
        return {depth, sightRayOffset:Math.hypot(lateral.x,lateral.y,lateral.z),
                verticalOffset:point.y-eye.y};
      };
      samples.push({frame,eye,forward,rear:rearPoint,front:frontPoint,muzzle,
         rearMetrics:stats(rearPoint),frontMetrics:stats(frontPoint),
         muzzleMetrics:stats(muzzle)});
    }
    if(frame>=85) {
      engine.save.write("ads_alignment",JSON.stringify({mode,samples}));
      engine.session.quit();
    }
  }catch(e){
    engine.save.write("ads_alignment",JSON.stringify({mode,
      error:String(e?.stack??e),samples}));
    engine.session.quit();
  }
});
'''
def run(player,mode):
    player=Path(player).resolve(strict=True)
    out=ROOT/"out"/"qa_pistol_ads"
    out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="northstar-ads-") as t:
        root=Path(t)
        game=root/"game"
        shutil.copytree(ROOT/"Projects/AbbyPistolDemo",game)
        entry=game/"assets/scripts/main.js"
        source=entry.read_text("utf-8")
        entry.write_text(source+"\n"+JS.replace("__MODE__",json.dumps(mode)),encoding="utf-8")
        env=os.environ.copy()
        logdir=root/"logs";logdir.mkdir()
        appdata=root/"localAppData";appdata.mkdir()
        env.update({"LOCALAPPDATA":str(appdata),
          "NORTHSTARCPP_LOG_DIRECTORY":str(logdir),
          "NORTHSTARCPP_RENDER_PROVIDER":os.environ.get("NORTHSTARCPP_QA_RENDER_PROVIDER","d3d12"),
          "NORTHSTARCPP_PHYSICS_PROVIDER":"jolt"})
        command=[str(player),
                 *([] if env["NORTHSTARCPP_RENDER_PROVIDER"]=="vulkan"
                   else ["--warp"]),
                 "--explore","--scene",
                 str((game/"main.nscn").resolve()),
                 "--shared-assets",
                 str((ROOT/"engine/northstarcpp/resources/sharedAssets").resolve())]
        try:
            result=subprocess.run(command,cwd=player.parent,env=env,
                                  capture_output=True,timeout=65)
            exit_code=result.returncode
            err=result.stderr.decode("utf-8","replace")
        except subprocess.TimeoutExpired as e:
            exit_code="timeout"
            err=(e.stderr or b"").decode("utf-8","replace")
        (out/f"{mode}_stderr.log").write_text(err,"utf-8")
        try:data=read_marker(appdata,"ads_alignment")
        except Exception as e:data={"error":"marker: "+str(e),"samples":[]}
        data["player_exit"]=exit_code
        (out/f"{mode}.json").write_text(json.dumps(data,indent=2),"utf-8")
        return data
if __name__=="__main__":
    mode=sys.argv[2] if len(sys.argv)>2 else "ads"
    data=run(sys.argv[1],mode)
    for sample in data.get("samples",[]):
        print(json.dumps({"mode":mode,"frame":sample["frame"],
          "rear":sample.get("rearMetrics"),"front":sample.get("frontMetrics"),
          "muzzle":sample.get("muzzleMetrics")}))
    print("PLAYER_EXIT",data.get("player_exit"),"ERROR",data.get("error"))
    raise SystemExit(0 if data.get("samples") and not data.get("error")
        and data.get("player_exit")==0 else 1)
