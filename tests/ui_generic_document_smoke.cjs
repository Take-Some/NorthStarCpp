// Contract regression: engine.ui.apply receives only generic UI documents.
// No gameplay concepts, mission state, health, or advertising logic in C++.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const bootstrap=fs.readFileSync(
    path.join(__dirname,"..","engine","northstarcpp","resources","sharedAssets","api","northstar","bootstrap.js"),
    "utf8");
const start=bootstrap.indexOf("    class UI {");
const end=bootstrap.indexOf("    class Logger {",start);
assert.ok(start>=0&&end>start,"UI SDK class missing");

const documents=[];
const engine={
    ui:{
        apply:document=>{documents.push(document);return null;},
        setTitle:()=>null
    }
};
const UI=vm.runInNewContext(bootstrap.slice(start,end)+"\nUI;",{engine});

UI.mount("mission",[
    {id:"panel",type:"panel",props:{
        width:400,height:200,background:"#121e29e5"
    }},
    {id:"heading",type:"text",parent:"panel",props:{
        text:"Reach the beacon",height:32,font_scale:2,color:"#ffcc44ff"
    }}
]);
let document=documents.at(-1);
assert.equal(document.target,"screen");
assert.equal(document.nodes.length,2);
assert.equal(document.nodes[1].id,"mission.heading");
assert.equal(document.nodes[1].parent,"mission.panel");
assert.equal(document.nodes[1].props.text.type,"text");
assert.equal(document.nodes[1].props.text.value,"Reach the beacon");
assert.equal(document.nodes[0].props.background.type,"color");
assert.equal(document.nodes[0].props.width.type,"number");

UI.mount("health",[
    {id:"panel",type:"panel",props:{
        width:160,height:50,anchor_right:true,background:"#af2638dd"
    }},
    {id:"value",type:"text",parent:"panel",props:{
        text:"HP 100/100",height:24,color:"#ffffffff"
    }}
]);
document=documents.at(-1);
assert.equal(document.nodes.length,4);
assert.equal(document.nodes[2].props.anchor_right.type,"bool");
assert.equal(document.nodes[2].props.anchor_right.value,true);

UI.mount("health",[
    {id:"panel",type:"panel",props:{
        width:160,height:50,anchor_right:true,background:"#af2638dd"
    }},
    {id:"value",type:"text",parent:"panel",props:{
        text:"HP 74/100",height:24,color:"#ffffffff"
    }}
]);
document=documents.at(-1);
assert.equal(document.nodes.length,4);
assert.equal(document.nodes[3].props.text.value,"HP 74/100");
assert.equal(document.nodes[1].props.text.value,"Reach the beacon");

UI.unmount("mission");
assert.equal(documents.at(-1).nodes.length,2);
assert.equal(documents.at(-1).nodes[0].id,"health.panel");
UI.clear();
assert.equal(documents.at(-1).nodes.length,0);
assert.throws(()=>UI.mount("invalid layer!",[]),/UI.mount requires/);
assert.throws(()=>UI.mount("hud",[{id:"a",type:"unknown"}]),/UI node descriptor/);

console.log("engine.ui generic document: PASS (typed props, layers, replacement, cleanup)");
