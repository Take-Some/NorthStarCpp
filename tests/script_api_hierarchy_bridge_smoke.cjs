"use strict";
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const assert=require("node:assert/strict");
const root=path.resolve(__dirname,"..");
const nativeSource=fs.readFileSync(path.join(root,
    "engine/northstarcpp/modules/scripting/v8/v8.cpp"),"utf8");
const match=nativeSource.match(
    /static char const bootstrap\[\] = R"NSJS\(([\s\S]*?)\)NSJS";/);
assert.ok(match,"Native V8 engine bootstrap not found");
const sidecarSource=fs.readFileSync(path.join(root,
    "tools/v8_provider_sidecar/v8_worker.js"),"utf8");
const beginning=sidecarSource.indexOf("  function initRealmApi() {");
const ending=sidecarSource.indexOf("\n  const initRealmSource",beginning);
assert.ok(beginning>0 && ending>beginning,"Sidecar bridge function not found");

function verify(name,source,box) {
    const records=[];
    const native=(namespace,method,args)=>{
        records.push([namespace,method,...args]);
        return namespace+"."+method;
    };
    const context=box(native);
    vm.runInNewContext(source,context,{filename:name});
    const api=context.engine;
    assert.equal(api.audio.playback.playNac("a.nac",3),
        "engine.audio.playback.playNac");
    assert.equal(api.audio.voices.stopVoice(1),
        "engine.audio.voices.stopVoice");
    assert.equal(api.audio.playNac("a.nac"),
        "engine.audio.playNac","Original flat API must remain callable");
    assert.equal(api.physics.queries.raycast(1,2,3),
        "engine.physics.queries.raycast");
    assert.equal(api.physics.bodies.applyImpulse(3,1,0,0),
        "engine.physics.bodies.applyImpulse");
    assert.equal(api.input.actions.rebindAction("jump","space",32),
        "engine.input.actions.rebindAction");
    assert.equal(api.scene.attachments.attachBone("a","b"),
        "engine.scene.attachments.attachBone");
    assert.equal(api.camera.cinematics.playCutscene("scene"),
        "engine.camera.cinematics.playCutscene");
    assert.equal(api.player.controller.possess(12),
        "engine.player.controller.possess");
    // engine.vehicle was already organized into domain-specific layers.
    assert.equal(api.vehicle.drive.controls(3,0.5,0,0),
        "engine.vehicle.drive.controls");
    assert.equal(api.vehicle.seats.occupy(3,1,42),
        "engine.vehicle.seats.occupy");
    assert.equal(api.ecs.queries.queryMany("npc"),
        "engine.ecs.queries.queryMany");
    assert.equal(api.ui.documents.readAsset("ui/hud.nui"),
        "engine.ui.documents.readAsset");
    assert.equal(api.render.shadows.state(),
        "engine.render.shadows.state");
    assert.equal(api.render.sky.configure({timeOfDay:10}),
        "engine.render.sky.configure");
    assert.equal(api.render.shaders.catalog(),
        "engine.render.shaders.catalog");
    assert.equal(api.api("engine.audio.voices").stopVoice(1),
        "engine.audio.voices.stopVoice");
    assert.strictEqual(api.audio.voices.stopVoice,api.audio.voices.stopVoice,
        "Native method identity must be cached");
    assert.equal(api.audio.playback.then,undefined,
        "Nested namespaces cannot behave as promises");
    assert.deepEqual([...records[0]],["engine.audio.playback","playNac","a.nac",3]);
    assert.ok(records.every(record=>record[0].startsWith("engine.")));
    console.log("PASS",name,records.length,"native calls across nested subsystems");
}

verify("embedded-V8",match[1],
    native=>({__northstar_native:native}));
verify("V8-sidecar",sidecarSource.slice(beginning,ending)+"\ninitRealmApi();",
    native=>({Realm:{shared:{call:native}}}));
