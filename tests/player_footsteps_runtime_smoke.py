"""Native Player/physics/NEF8/NAC/engine.audio gameplay end-to-end QA."""
from pathlib import Path
import os
import shutil
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.v8_provider.player_e2e import run

MOVEMENT='''import { Script, Player } from "@northstar/engine";
class Autowalk extends Script {
    update() { if (Player.enabled) Player.move(0, 1); }
}
new Autowalk().install();
'''
SESSION='''import { Events, Player, Audio, Scene } from "@northstar/engine";
let ticks=0,voice=0,step=null;
const save=(name,data)=>engine.storage.saves.write(name,JSON.stringify(data));
Events.on("start",()=>{
 if(!Player.enabled || Player.index<0)throw Error("player not activated");
 save("scripting_start",{phase:"start",objects:Scene.objectCount()});
});
Events.on("player.footstep",e=>{
 if(step!==null)return;
 step=e;
 voice=Audio.playNac("audio/footsteps/"+e.surface+".nac",e.gait,e.sequence,0.75);
 if(!Number.isInteger(voice)||voice<=0)throw Error("NAC voice not created");
});
Events.on("update",dt=>{
 if(++ticks===140){
   save("scripting_update",{phase:"update",frames:ticks,
      hadFootstep:step!==null,voice,dt});
   engine.session.lifecycle.quit();
 }
});
Events.on("stop",()=>save("scripting_stop",{phase:"stop",ticks}));
'''

def verify(player):
    player=Path(player).resolve(strict=True)
    provider=player.parent/"z_audio_northstar.dll"
    with tempfile.TemporaryDirectory(prefix="northstar-footsteps-e2e-") as temp:
        project=Path(temp)/"project"
        shutil.copytree(ROOT/"Projects/NorthStarFirstPersonPlatform",project)
        (project/"assets/scripts/main.js").unlink()
        (project/"assets/scripts/playerConfig.js").unlink()
        for target,content in [
            ("player/playerMovement.js",MOVEMENT),
            ("player/playerSession.js",SESSION)]:
            dest=project/"assets/scripts"/target
            dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_text(content,encoding="utf-8")
        previous=os.environ.get("NORTHSTARCPP_AUDIO_PLUGIN")
        os.environ["NORTHSTARCPP_AUDIO_PLUGIN"]=str(provider)
        try:
            result=run(player,None,project,seconds=55)
        finally:
            if previous is None:os.environ.pop("NORTHSTARCPP_AUDIO_PLUGIN",None)
            else:os.environ["NORTHSTARCPP_AUDIO_PLUGIN"]=previous
        state=result["markers"]["scripting_update"]
        assert state["hadFootstep"] and state["voice"]>0,state
        print("PASS: native Player -> physics -> player.footstep -> NAC audio voice")

if __name__=="__main__":
    verify(sys.argv[1])
