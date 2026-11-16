"""Native V8 integration regression for the shared UI root auto-loader.

The selected project scene is real, but the mission script is replaced in a
temporary copy so this checks only the UI root + generic engine.ui provider.
"""
from pathlib import Path
import json
import os
import shutil
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tests"))
from first_person_protocol_runtime_smoke import run_direct

SOURCE = '''
import { UI, Events, Session } from "@northstar/engine";
let frames = 0;
let root;
Events.on("start", () => {
    root = UI.createRoot("qa");
    root.panel("panel",{
        x:10,y:10,width:360,height:120,padding:8,
        background:"#141d28dd"
    }).text("title","TEST ROOT",{height:35,font_scale:2},"panel")
      .progress("bar",0.25,{width:220,height:12},"panel")
      .mount();
    engine.save.write("scripting_start",JSON.stringify({
        registered:root.mounted,size:root.size,name:root.name
    }));
});
Events.on("update", dt => {
    ++frames;
    if(frames===2)root.batch(hud=>{
        hud.setText("title","UI ROOT ACTIVE");
        hud.setProgress("bar",0.75);
    });
    if(frames===4) {
        const nodesBefore=root.size;
        root.dispose();
        engine.save.write("scripting_update",JSON.stringify({
            frames,nodesBefore,disposed:!root.mounted && root.size===0
        }));
        Session.quit();
    }
});
Events.on("stop", () => {
    engine.save.write("scripting_stop",JSON.stringify({frames}));
});
'''

def verify(player:Path) -> None:
    original_physics=os.environ.get("NORTHSTARCPP_PHYSICS_PROVIDER")
    original_render=os.environ.get("NORTHSTARCPP_RENDER_PROVIDER")
    os.environ["NORTHSTARCPP_PHYSICS_PROVIDER"]="jolt"
    os.environ["NORTHSTARCPP_RENDER_PROVIDER"]="d3d12"
    try:
        with tempfile.TemporaryDirectory(prefix="northstar-ui-root-e2e-") as temp:
            project=Path(temp)/"Project"
            shutil.copytree(ROOT/"Projects/NorthStarFirstPersonPlatform",project)
            (project/"scripts/main.js").write_text(SOURCE,encoding="utf-8")
            result=run_direct(player,project,seconds=30)
            start=result["markers"]["scripting_start"]
            update=result["markers"]["scripting_update"]
            stop=result["markers"]["scripting_stop"]
            assert start["registered"] and start["size"]==4,start
            assert start["name"]=="qa",start
            assert update["frames"]==4 and update["nodesBefore"]==4,update
            assert update["disposed"] and stop["frames"]==4,(update,stop)
            print("UI root native V8/D3D12/Jolt: PASS (create, mount, update, dispose)")

            # Engine API roots are sealed. A project-local file at this path
            # must be ignored; only project gameplay scripts are replaceable.
            override=project/"scripts/ui/UI.js"
            override.parent.mkdir(parents=True,exist_ok=True)
            override.write_text("""
(() => {
    const UI=NorthStar.modules.engine.UI;
    class ProjectRoot {
        constructor(layer) { this.name=layer; this.mounted=false; }
        marker() { return "project-root"; }
        mount() {
            UI.mount(this.name,[
                {id:"panel",type:"panel",props:{width:200,height:70}},
                {id:"caption",type:"text",parent:"panel",props:{
                    text:"PROJECT UI ROOT",height:32
                }}
            ]);
            this.mounted=true;
            return this;
        }
        unmount() { UI.unmount(this.name);this.mounted=false; }
    }
    UI.registerRoot(ProjectRoot);
})();
""",encoding="utf-8")
            (project/"scripts/main.js").write_text("""
import {UI,Events,Session} from "@northstar/engine";
let ticks=0;
let root;
Events.on("start",()=>{
    root=UI.createRoot("projectOverride");
    if(typeof root.marker==="function")
        throw new Error("Project overrode the sealed engine UI root");
    root.panel("panel",{width:200,height:70})
        .text("caption","SEALED ENGINE UI",{height:32},"panel").mount();
    engine.save.write("scripting_start",JSON.stringify({
        override:false,active:root.mounted
    }));
});
Events.on("update",()=>{
    ++ticks;
    if(ticks===3) {
        root.unmount();
        engine.save.write("scripting_update",JSON.stringify({
            frames:ticks,unmounted:!root.mounted
        }));
        Session.quit();
    }
});
Events.on("stop",()=>{
    engine.save.write("scripting_stop",JSON.stringify({frames:ticks}));
});
""",encoding="utf-8")
            result=run_direct(player,project,seconds=30)
            start=result["markers"]["scripting_start"]
            update=result["markers"]["scripting_update"]
            assert start["override"] is False and start["active"],start
            assert update["frames"]==3 and update["unmounted"],update
            print("Sealed engine scripts/ui/UI.js: PASS (project copy ignored)")
    finally:
        for key,old in [
            ("NORTHSTARCPP_PHYSICS_PROVIDER",original_physics),
            ("NORTHSTARCPP_RENDER_PROVIDER",original_render)]:
            if old is None: os.environ.pop(key,None)
            else: os.environ[key]=old

if __name__=="__main__":
    verify(Path(sys.argv[1]))
