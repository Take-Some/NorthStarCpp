// Asset-to-runtime UI contract without a graphics device.
// Run from the repository root: node tools/dev/ui_asset_smoke.mjs
import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";
import path from "node:path";
import {createRequire} from "node:module";
const require=createRequire(import.meta.url);
const nui=require("./nui_test_bridge.cjs");

const root=process.cwd();
const scenes=[
    "main_menu.nui","pause_menu.nui","settings_menu.nui",
    "credits_menu.nui","confirm_exit.nui","loading.nui",
    "controls_hud.nui","weapon_hud.nui","mission_hud.nui","fps_pause.nui"
];
const project=path.join(root,"Projects/KaylaNightCity/assets");
const sharedRoot=path.join(root,"sharedAssets");
const uploaded=[];
const context=vm.createContext({
    engine:{
        ui:{
            readAsset(asset) {
                return nui.load(path.join(sharedRoot,asset));
            },
            apply(document) {
                uploaded.push(document);
            },
            setTitle() {}
        }
    }
});
for(const source of [
    "sharedAssets/api/northstar/bootstrap.js",
    "sharedAssets/api/ui/UI.js"
])
    vm.runInContext(fs.readFileSync(path.join(root,source),"utf8"),context,
                    {filename:source});
const UI=context.NorthStar.modules.engine.UI;
const world=UI.loadAsset("ui/world_terminal.nui",{layer:"terminalQA",entityId:42});
assert.equal(world.target,"world");
assert.equal(world.size,3);
world.mount();
assert.equal(uploaded.at(-1).target,"world");
assert.equal(uploaded.at(-1).entityId,"42");
assert.equal(uploaded.at(-1).worldWidth,1.8);
world.dispose();
const ids=new Set();
for(const [index,file] of scenes.entries()){
    const raw=nui.decode(fs.readFileSync(path.join(sharedRoot,"ui",file),"utf8"));
    // The project is an optional higher-priority VFS override, not a required test fixture.
    const override=path.join(project,"ui",file);
    if(fs.existsSync(override)) {
        const local=nui.decode(fs.readFileSync(override,"utf8"));
        assert.equal(local.schema,raw.schema);
        assert.equal(local.target,raw.target);
    }
    assert.ok(["northstar.ui/1","northstar.ui/2"].includes(raw.schema),file);
    assert.equal(raw.target,"screen",file);
    assert.ok(Array.isArray(raw.nodes)&&raw.nodes.length<=256,file);
    const layer="check"+index;
    const screen=UI.loadAsset("ui/"+file,{layer});
    assert.equal(screen.size,raw.nodes.length,file);
    assert.equal(screen.snapshot().length,raw.nodes.length,file);
    screen.mount();
    assert.equal(uploaded.at(-1).target,"screen",file);
    assert.equal(uploaded.at(-1).nodes.length,raw.nodes.length,file);
    for(const node of raw.nodes){
        assert.ok(!ids.has(layer+"."+node.id));
        ids.add(layer+"."+node.id);
    }
    screen.dispose();
}
const shared=nui.decode(fs.readFileSync(path.join(root,
    "sharedAssets/ui/default_menu.nui"),"utf8"));
assert.equal(shared.schema,"northstar.ui/2");
const reusable=UI.loadAsset("ui/default_menu.nui",{layer:"sharedTest"});
const startButton=reusable.snapshot().find(node=>node.id==="start");
assert.equal(startButton.props.width,505,"Named style must resolve");
assert.equal(startButton.props.color,"#ffcb64ff","Design token must resolve");
reusable.mount();
assert.equal(uploaded.at(-1).nodes.length,shared.nodes.length);
reusable.dispose();
const original=context.engine.ui.readAsset;
context.engine.ui.readAsset=()=>JSON.stringify({...shared,
    tokens:{...shared.tokens},
    styles:{...shared.styles,menu_button:{color:{token:"no_such_token"}}}});
assert.throws(()=>UI.loadAsset("ui/default_menu.nui",{layer:"broken"}),
              /Unknown UI design token/);
context.engine.ui.readAsset=original;
assert.throws(()=>UI.loadAsset("ui/../outside.nui"),/relative ui\/\*\.nui/);
assert.throws(()=>UI.loadAsset("ui/controls_hud.xml"),/relative ui\/\*\.nui/);

// Replacing the authored source hot-reloads layout in place and must preserve
// runtime HUD state. Malformed assets retain the last GPU-valid tree.
const oldRead=context.engine.ui.readAsset;
let latest=fs.readFileSync(path.join(sharedRoot,"ui/weapon_hud.nui"),"utf8");
context.engine.ui.readAsset=asset=>
    asset==="ui/weapon_hud.nui"?nui.compile(latest):oldRead(asset);
const live=UI.loadAsset("ui/weapon_hud.nui",{layer:"reloader"});
live.mount();
live.setText("ammo","77 / 160");
const revised=nui.decode(latest);
revised.nodes.find(node=>node.id==="panel").props.width=410;
latest=nui.encode(revised);
assert.equal(UI.reloadChangedAssets(),1);
assert.equal(live.snapshot().find(n=>n.id==="panel").props.width,410);
assert.equal(live.snapshot().find(n=>n.id==="ammo").props.text,"77 / 160");
const invalid=nui.decode(latest);
invalid.nodes[0].props.width={token:"missing"};
latest=nui.encode(invalid);
assert.equal(UI.reloadChangedAssets(),0,"Invalid hot reload must be rejected");
assert.equal(live.snapshot().find(n=>n.id==="panel").props.width,410);
const repaired=structuredClone(revised);
repaired.nodes[0].props.width=450;
latest=nui.encode(repaired);
assert.equal(UI.reloadChangedAssets(),1,"Valid corrected source must recover");
assert.equal(live.snapshot().find(n=>n.id==="panel").props.width,450);
assert.equal(live.snapshot().find(n=>n.id==="ammo").props.text,"77 / 160");
live.dispose();
context.engine.ui.readAsset=oldRead;

console.log("NorthStar UI asset smoke PASS:",scenes.length,
            "shared documents + reusable UI/2 template");
