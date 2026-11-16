// UI event callbacks are game-owned, scoped, disposable and failure-isolated.
// The underlying provider emits neutral IDs, types and values only.
"use strict";
const assert=require("node:assert/strict");
const path=require("node:path");
const fs=require("node:fs");
const vm=require("node:vm");
const api=path.join(__dirname,"..","engine","northstarcpp","resources","sharedAssets","api");
const listeners=new Map();
const outbound=[];
const engine={
    on:(name,fn)=>{
        const set=listeners.get(name)||new Set();
        set.add(fn);listeners.set(name,set);
        return ()=>set.delete(fn);
    },
    off:(name,fn)=>listeners.get(name)?.delete(fn)||false,
    emit:(name,value)=>{
        for(const callback of [...(listeners.get(name)||[])])callback(value);
        return listeners.get(name)?.size||0;
    },
    ui:{apply:doc=>outbound.push(doc),setTitle:()=>{}},
    logger:{warn:()=>{}}
};
const context=vm.createContext({engine});
for(const file of ["northstar/bootstrap.js","ui/UI.js"]){
    vm.runInContext(fs.readFileSync(path.join(api,file),"utf8"),context);
}
const SDK=vm.runInContext("NorthStar.modules.engine",context);
const {UI,Events}=SDK;
let clicks=0, changes=[];
const hud=UI.createRoot("hud")
    .button("start","START",{width:140,height:35})
    .input("name",{text:"Kayla",width:140,height:35})
    .onClick("start",event=>{assert.equal(event.value,true);clicks++;})
    .onChange("name",event=>{changes.push(event.value);})
    .mount();
assert.equal(listeners.get("ui.event").size,1);
Events.emit("ui.event",{id:"other.start",name:"click",value:true});
assert.equal(clicks,0);
Events.emit("ui.event",{id:"hud.start",name:"click",value:true});
Events.emit("ui.event",{id:"hud.name",name:"change",value:"Player"});
assert.equal(clicks,1);
assert.deepEqual(changes,["Player"]);
const before=outbound.length;
hud.setText("name","Player");
assert.equal(outbound.length,before+1);
hud.unmount();
assert.equal(listeners.get("ui.event").size,0);
Events.emit("ui.event",{id:"hud.start",name:"click",value:true});
assert.equal(clicks,1);
hud.mount();
assert.equal(listeners.get("ui.event").size,1);
hud.dispose();
assert.equal(listeners.get("ui.event").size,0);
assert.throws(()=>UI.createRoot("other").onClick("missing",()=>{}),/does not exist/);
console.log("Embedded UI events: PASS (click/change routing, scoped lifecycle, event isolation)");
