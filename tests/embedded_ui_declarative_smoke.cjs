// Declarative authoring is a pure, engine-owned JS contract.
"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const api=path.join(__dirname,"..","engine","northstarcpp",
                    "resources","sharedAssets","api");
const sent=[], subscriptions=new Map();
const engine={
    ui:{apply:doc=>sent.push(doc),setTitle:()=>null},
    on:(name,cb)=>{
        const set=subscriptions.get(name)??new Set();
        set.add(cb); subscriptions.set(name,set);
        return ()=>set.delete(cb);
    },
    off:(name,cb)=>subscriptions.get(name)?.delete(cb)||false,
    emit:(name,payload)=>{
        for(const cb of [...(subscriptions.get(name)??[])]) cb(payload);
    },
    logger:{warn:()=>{}}
};
const context=vm.createContext({engine});
for(const name of ["northstar/bootstrap.js","ui/UI.js"])
    vm.runInContext(fs.readFileSync(path.join(api,name),"utf8"),context);
const {UI,Events}=vm.runInContext("NorthStar.modules.engine",context);

// Reusable UI component: pure descriptor, no native/renderer dependence.
function action(id,caption,callback) {
    return {id,type:"button",text:caption,props:{width:170,height:35},
            events:{click:callback}};
}
function card(heading,cb) {
    return {id:"card",type:"panel",props:{width:220,padding:10},
        children:[
            {id:"title",type:"text",text:heading,props:{height:28}},
            action("play","PLAY",cb),
            {id:"icon",type:"image",source:"textures/ui.ytd@mark",
             props:{width:18,height:18}}
        ]};
}
let a=0,b=0,imperative=0;
const menu=UI.createRoot("menu");
menu.render(card("SCREEN",()=>a++)).mount();
assert.equal(menu.size,4);
assert.equal(sent.length,1);
assert.equal(sent[0].nodes[2].id,"menu.play");
assert.equal(sent[0].nodes[2].parent,"menu.card");
assert.equal(sent[0].nodes[2].props.text.value,"PLAY");
assert.equal(sent[0].nodes[3].props.source.value,"textures/ui.ytd@mark");
menu.onClick("play",()=>imperative++);
const click=()=>Events.emit("ui.event",{id:"menu.play",name:"click",value:true});
click();
assert.equal(a,1);assert.equal(imperative,1);

menu.render(card("SCREEN",()=>b++));
assert.equal(sent.length,1,"identical tree must not request native submission");
click();assert.equal(a,1);assert.equal(b,1);
assert.equal(imperative,2,"stable imperative click handler must survive render");

menu.render(card("SECOND",()=>b++));
assert.equal(sent.length,2);
click();assert.equal(b,2);assert.equal(imperative,3);

let before=JSON.stringify(menu.snapshot());
for(const invalid of [
    {id:"root",type:"panel",children:[
        action("duplicate","a",()=>{}),action("duplicate","b",()=>{})]},
    {id:"root",type:"panel",children:[
        {id:"child",type:"text",text:"a",parent:"wrong"}]},
    {id:"root",type:"panel",children:[
        {id:"child",type:"button",text:"a",events:{invalid:()=>{}}}]},
    {id:"root",type:"panel",props:{padding:Infinity}},
    {id:"root",type:"panel",extra:"invalid"}
]) {
    const previous=sent.length;
    assert.throws(()=>menu.render(invalid));
    assert.equal(JSON.stringify(menu.snapshot()),before,
                 "invalid declaration must not touch old tree");
    assert.equal(sent.length,previous,
                 "invalid declaration must not touch native");
    const oldB=b,oldImperative=imperative;
    click();
    assert.equal(b,oldB+1);
    assert.equal(imperative,oldImperative+1);
}

assert.equal(menu.snapshot()[1].props.text,"SECOND",
             "validation errors must not mutate the existing UI");

menu.setText("title","DYNAMIC");
const revision=sent.length;
menu.reloadFromAsset([
    {id:"card",type:"panel",props:{width:220,padding:10}},
    {id:"title",type:"text",parent:"card",props:{text:"ASSET",height:28}},
    {id:"play",type:"button",parent:"card",props:{text:"PLAY",width:170,height:35}}
]);
assert.equal(menu.snapshot()[1].props.text,"DYNAMIC",
             "runtime override wins over hot-reloaded asset");
assert.equal(sent.length,revision+1);
click();assert.equal(imperative,9,
             "stable imperative handler persists across asset reload");

const oldSnapshot=JSON.stringify(menu.snapshot());
const oldCalls=sent.length;
assert.throws(()=>menu.batch(root=>{
    root.setText("title","BAD");
    root.set("play",{width:200});
    root.panel("broken",{width:NaN});
}));
assert.equal(JSON.stringify(menu.snapshot()),oldSnapshot,
             "failed batch restores tree and runtime overrides");
assert.equal(sent.length,oldCalls);
menu.reloadFromAsset([
    {id:"card",type:"panel",props:{width:220,padding:10}},
    {id:"title",type:"text",parent:"card",props:{text:"NEW",height:28}},
    {id:"play",type:"button",parent:"card",props:{text:"PLAY",width:170,height:35}}
]);
assert.equal(menu.snapshot()[1].props.text,"DYNAMIC",
             "rollback preserved previous runtime override");

menu.render({id:"replacement",type:"panel"});
const beforeClick=imperative;
click();
assert.equal(imperative,beforeClick,
             "orphan handlers must not fire for removed nodes");
menu.dispose();
assert.equal(subscriptions.get("ui.event")?.size,0);
console.log("Declarative UI: PASS (atomicity, re-render, event preservation, overrides, cleanup)");
