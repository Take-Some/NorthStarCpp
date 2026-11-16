// Executable contract test for *embedded* SDK shipped inside NorthStarCpp.
// No legacy sharedAssets/scripts and no copied implementation under test.
"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const API=path.join(__dirname,"..","engine","northstarcpp","resources","sharedAssets","api");
const sent=[];
const entities=[
    {index:0,id:"1",name:"Pawn",position:{x:0,y:1,z:0}},
    {index:1,id:"2",name:"Beacon",position:{x:1,y:0,z:3}}
];
const engine={
    ui:{
        apply:doc=>{sent.push(doc);return null;},
        setTitle:()=>null
    },
    scene:{
        objects:(start=0,count=256)=>entities.slice(start,start+count),
        objectCount:()=>entities.length
    },
    player:{controlledIndex:()=>0,controlledEntity:()=>"1"}
};
const context=vm.createContext({engine});
for(const relative of ["northstar/bootstrap.js","ui/UI.js"]){
    const text=fs.readFileSync(path.join(API,relative),"utf8");
    vm.runInContext(text,context,{filename:relative});
}
const SDK=vm.runInContext("NorthStar.modules.engine",context);
const {UI,Scene,Player,Input}=SDK;
assert.equal(Scene.requireByName("Beacon").index,1);
assert.equal(Scene.require(0).id,"1");
assert.throws(()=>Scene.requireByName("Missing"),/Missing/);
assert.throws(()=>Scene.require(100),/100/);
assert.equal(Player.hasPawn,true);
assert.equal(Player.requirePawn().name,"Pawn");
const normalized=Input.normalized2D(3,4);
assert.equal(Math.round(normalized.x*10),6);
assert.equal(Math.round(normalized.y*10),8);
assert.throws(()=>Input.normalized2D(Infinity,0),/finite/);

const root=UI.createRoot("hud");
root.group("frame",{width:320,height:140,padding:12}, group=>{
    group.text("caption","STATUS",{height:24});
    group.group("stats",{width:280,height:70}, children=>{
        children.text("health","HP 100",{height:24});
    });
    group.progress("bar",1,{width:200,height:12});
});
assert.equal(root.size,6);
assert.equal(root.has("health"),true);
assert.equal(root.has("missing"),false);
assert.equal(root.snapshot().find(n=>n.id==="health").parent,"stats");
assert.equal(root.snapshot().find(n=>n.id==="bar.fill").parent,"bar");
const copy=root.snapshot();
copy.find(n=>n.id==="health").props.text="changed outside";
assert.equal(root.snapshot().find(n=>n.id==="health").props.text,"HP 100","snapshots must be independent");
root.mount();
assert.equal(sent.length,1);
assert.equal(sent[0].nodes.length,6);
assert.equal(sent[0].nodes.find(n=>n.id==="hud.health").parent,"hud.stats");
root.mount();
root.setText("health","HP 100");
root.setProgress("bar",1);
assert.equal(sent.length,1,"unchanged state must not invoke native UI");
root.batch(hud=>{
    hud.setText("health","HP 75");
    hud.setProgress("bar",0.75);
    hud.batch(nested=>{
        nested.set("caption",{text:"HEALTH"});
    });
});
assert.equal(sent.length,2,"batch must publish once");
assert.equal(sent.at(-1).nodes.find(n=>n.id==="hud.bar.fill").props.width.value,150);
assert.equal(sent.at(-1).nodes.find(n=>n.id==="hud.health").props.text.value,"HP 75");
const previous=JSON.stringify(root.snapshot());
assert.throws(()=>root.batch(view=>{
    view.setText("health","CORRUPTED");
    view.text("bad","x",{}, "missing");
}),/invalid/);
assert.equal(JSON.stringify(root.snapshot()),previous,"failed batch must rollback");
assert.equal(sent.length,2,"failed batch must not publish");

const alerts=UI.createRoot("alerts").text("message","Hello").mount();
assert.equal(sent.length,3);
assert.equal(UI.hasLayer("alerts"),true);
const earlier=sent.length;
assert.throws(()=>UI.mount("invalid",[
    {id:"child",parent:"missing",type:"text",props:{text:"broken"}}
]),/invalid.*child|child.*parent|parent/i);
assert.equal(sent.length,earlier,"invalid low-level tree cannot reach native UI");
assert.equal(UI.hasLayer("invalid"),false);

alerts.unmount();
assert.equal(UI.hasLayer("hud"),true,"unmount may not clear another root");
UI.clear();
assert.equal(UI.hasLayer("hud"),false);
root.mount(); // must restore after explicit global clear
assert.equal(UI.hasLayer("hud"),true);
assert.equal(sent.at(-1).nodes.length,6);
root.dispose();
assert.equal(UI.hasLayer("hud"),false);
// A 3D surface is a separate document, not another screen-space layer.
const surface=UI.createSurface("2",{
    layer:"advert",width:256,height:128,worldWidth:2,worldHeight:1
});
surface.group("panel",{width:256,height:128,background:"#202020ff"},
    panel=>panel.text("name","NORTHSTAR",{height:35})).mount();
const worldDoc=sent.at(-1);
assert.equal(worldDoc.target,"world");
assert.equal(worldDoc.entityId,"2");
assert.equal(worldDoc.width,256);
assert.equal(worldDoc.height,128);
assert.equal(worldDoc.nodes[0].id,"advert.panel");
assert.equal(UI.hasSurface("2"),true);
const worldCount=sent.length;
surface.mount();
assert.equal(sent.length,worldCount,"unchanged world UI must not reupload");
surface.dispose();
assert.equal(UI.hasSurface("2"),false);
assert.equal(sent.at(-1).target,"world");
assert.equal(sent.at(-1).nodes.length,0);
console.log("Embedded SDK usability: PASS (group, atomic batch, input, 3D surface lifecycle)");
