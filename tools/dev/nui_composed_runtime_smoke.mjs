// Source-level integration: XML v3 reusable components + declarative actions
// + native event routing + breakpoint recomposition + animation timelines.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import {createRequire} from "node:module";
const require=createRequire(import.meta.url);
const bridge=require("./nui_test_bridge.cjs");
const root=process.cwd();
const listeners=new Map();
const emitted=[],drawn=[];
let viewportWidth=1280;
const source={
    on(name,handler) {
        const group=listeners.get(name)??new Set();
        group.add(handler);listeners.set(name,group);
        return ()=>group.delete(handler);
    },
    emit(name,payload) {
        emitted.push({name,payload});
        for(const fn of [...(listeners.get(name)??[])])fn(payload);
        return true;
    },
    ui:{
        readAsset(name){
            return bridge.load(path.join(root,"sharedAssets",name));
        },
        viewportSize(){return {width:viewportWidth,height:720};},
        hotReloadEnabled(){return false;},
        apply(doc){drawn.push(doc);return true;},
        setTitle(){}
    }
};
const context=vm.createContext({engine:source,console});
const sdk=process.env.NUI_SDK_MIRROR==="1"?
    "engine/northstarcpp/resources/sharedAssets/api":"sharedAssets/api";
for(const script of [
    sdk+"/northstar/bootstrap.js",
    sdk+"/ui/UI.js"
])vm.runInContext(fs.readFileSync(path.join(root,script),"utf8"),context,
    {filename:script});

const UI=context.NorthStar.modules.engine.UI;
const wide=UI.loadAsset("ui/component_gallery.nui",{layer:"gallerywide"});
let items=wide.snapshot();
assert.equal(items.length,4);
assert.equal(items.find(x=>x.id==="accept").props.text,"ACCEPT");
assert.equal(items.find(x=>x.id==="accept").props.width,450);
assert.equal(items.find(x=>x.id==="accept").props.color,"#f5c86dff");
wide.mount();
UI.advanceAnimations(0.1);
const toolbar=wide.snapshot().find(x=>x.id==="toolbar");
assert.ok(toolbar.props.y>-320&&toolbar.props.y<150,
    "Mount-triggered timeline should interpolate its numeric property");
source.emit("ui.event",{id:"gallerywide.accept",name:"click",kind:1});
assert.equal(emitted.filter(e=>e.name==="ui.gallery.accept").length,1);
UI.advanceAnimations(0.1);
assert.ok(wide.snapshot().find(x=>x.id==="accept").props.x>0);
assert.ok(wide.snapshot().find(x=>x.id==="accept").props.x<16);
UI.advanceAnimations(0.1);
assert.equal(wide.snapshot().find(x=>x.id==="accept").props.x,16);
source.emit("ui.event",{id:"gallerywide.cancel",name:"click",kind:1});
assert.equal(emitted.filter(e=>e.name==="ui.gallery.cancel").length,1);
wide.dispose();

viewportWidth=500;
const compact=UI.loadAsset("ui/component_gallery.nui",{layer:"gallerysmall"});
items=compact.snapshot();
assert.equal(items.find(x=>x.id==="toolbar").props.width,320);
assert.equal(items.find(x=>x.id==="accept").props.width,280);
compact.mount();
UI.advanceAnimations(0.1);
viewportWidth=1280;
// The responsive engine observes viewport size on its 500-ms cadence.
for(let n=0;n<6;n++)source.emit("ui.update",0.1);
assert.equal(compact.snapshot().find(x=>x.id==="toolbar").props.width,520);
assert.equal(compact.snapshot().find(x=>x.id==="accept").props.width,450);
// Rebinding on resize must not stack duplicate declarative callbacks.
source.emit("ui.event",{id:"gallerysmall.accept",name:"click",kind:1});
assert.equal(emitted.filter(e=>e.name==="ui.gallery.accept").length,2);
const readAsset=source.ui.readAsset;
const broken=JSON.parse(readAsset("ui/component_gallery.nui"));
broken.actions[0].animation="unknown_clip";
source.ui.readAsset=name=>name==="ui/component_gallery.nui"?
    JSON.stringify(broken):readAsset(name);
assert.equal(UI.reloadChangedAssets(),0,"Invalid hot reload must be rejected");
source.emit("ui.event",{id:"gallerysmall.accept",name:"click",kind:1});
assert.equal(emitted.filter(e=>e.name==="ui.gallery.accept").length,3,
    "Invalid hot reload must preserve the original handler");
source.ui.readAsset=readAsset;
compact.dispose();
console.log("NUI_COMPOSED_RUNTIME_PASS: components, actions, animations, responsive resize");
