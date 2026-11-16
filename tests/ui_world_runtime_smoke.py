"""Real V8/Jolt/D3D12 smoke: script UI rasterizes to an Entity quad."""
from pathlib import Path
import os
import shutil
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tests"))
from first_person_protocol_runtime_smoke import run_direct

SOURCE = '''
import { UI, Events, Player, Session, Scene } from "@northstar/engine";
let frames=0;
let surface=null;
Events.on("start",()=>{
    const entityId=Player.entityId;
    if(!entityId || !Scene.findPawn())
        throw new Error("No stable Pawn Entity for world UI test");
    surface=UI.createSurface(entityId,{
        layer:"advert",width:256,height:128,worldWidth:1.8,worldHeight:0.9
    });
    surface.group("screen",{
        width:256,height:128,padding:8,background:"#13283bff"
    },panel=>{
        panel.text("title","NORTHSTAR",{height:30,color:"#ffd34dff"});
        panel.text("label","ENGINE.UI / WORLD",{height:29});
    }).mount();
    engine.save.write("scripting_start",JSON.stringify({
        entityId, mounted:surface.mounted,target:surface.target,
        pixels:256*128
    }));
});
Events.on("update",dt=>{
    ++frames;
    if(frames===3)surface.setText("label","UPDATED IN WORLD");
    if(frames===7) {
        engine.save.write("scripting_update",JSON.stringify({
            frames,active:UI.hasSurface(Player.entityId),
            target:surface.target
        }));
        surface.dispose();
        Session.quit();
    }
});
Events.on("stop",()=>{
    engine.save.write("scripting_stop",JSON.stringify({
        frames,disposed:!surface.mounted
    }));
});
'''
def verify(player: Path):
    previous={k:os.environ.get(k) for k in (
        "NORTHSTARCPP_PHYSICS_PROVIDER","NORTHSTARCPP_RENDER_PROVIDER")}
    os.environ["NORTHSTARCPP_PHYSICS_PROVIDER"]="jolt"
    os.environ["NORTHSTARCPP_RENDER_PROVIDER"]="d3d12"
    try:
        with tempfile.TemporaryDirectory(prefix="northstar-world-ui-") as temp:
            project=Path(temp)/"Project"
            shutil.copytree(ROOT/"Projects/NorthStarFirstPersonPlatform",project)
            (project/"scripts/main.js").write_text(SOURCE,encoding="utf-8")
            result=run_direct(player,project,seconds=30)
            start=result["markers"]["scripting_start"]
            update=result["markers"]["scripting_update"]
            stop=result["markers"]["scripting_stop"]
            assert start["mounted"] and start["target"]=="world",start
            assert update["active"] and update["frames"]==7,update
            assert stop["disposed"],stop
            print("World UI native V8/Jolt/D3D12: PASS (7 frames, GPU material submission)")
    finally:
        for key,value in previous.items():
            if value is None:os.environ.pop(key,None)
            else:os.environ[key]=value
if __name__=="__main__":
    verify(Path(sys.argv[1]))
