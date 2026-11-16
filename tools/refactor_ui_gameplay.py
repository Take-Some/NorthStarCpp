from pathlib import Path
p=Path(__file__).resolve().parents[1]/"Projects/NorthStarFirstPersonPlatform/scripts/main.js"
s=p.read_text(encoding="utf-8-sig")
a=s.index("        UI.setMission(")
b=s.index("\n    }\n\n    #announce",a)
s=s[:a]+'''        // Script-owned UI composition: engine.ui only sees elements/properties.
        const stage=this.#completed?OBJECTIVES.length:this.#phase+1;
        const status=this.#completed?"SUCCESS":OBJECTIVES[this.#phase].name;
        const progress=Math.floor(stage*100/OBJECTIVES.length);
        const elapsed=Math.floor(this.#elapsed);
        const clock=String(Math.floor(elapsed/60))+":"+String(elapsed%60).padStart(2,"0");
        const text=(id,value,height,scale=2,color="#e1e9ffff")=>({
            id,type:"text",parent:"panel",props:{
                text:value,height,font_scale:scale,color
            }
        });
        UI.mount("mission",[
            {id:"panel",type:"panel",props:{
                width:540,height:219,x:12,y:12,padding:12,gap:4,
                background:"#0b1423d9"
            }},
            text("brand","NORTHSTAR / FIRST PERSON PROTOCOL",23,2,"#f6c753ff"),
            text("stage",String(stage)+" / "+OBJECTIVES.length+"  "+status,32,3),
            text("objective",objective,36,2),
            text("progress","PROGRESS  "+progress+"%",25,2,"#f6c753ff"),
            text("timer","TIME "+clock+"      TARGET "+Math.round(distance)+" M",26,2),
            text("controls",
                 this.#completed?"R RESTART     F10 EXIT":
                 "WASD MOVE   SHIFT RUN   SPACE JUMP   E USE",
                 25,1,"#82cbdcff")
        ]);'''+s[b:]
assert s.count("UI.clearMission();")==1
s=s.replace("UI.clearMission();",'UI.unmount("mission");')
assert "setMission" not in s and "clearMission" not in s
p.write_text(s,encoding="utf-8",newline="")
print("First-person mission UI is now project-owned JavaScript.")
