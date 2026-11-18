"use strict";
// Test the actual native V8 bootstrap source without requiring a GPU or d8.
// Any future subsystem depth must pass through the same native dispatcher.
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const assert=require("node:assert/strict");
const root=path.resolve(__dirname,"..");
const cpp=fs.readFileSync(path.join(root,
    "engine/northstarcpp/modules/scripting/v8/v8.cpp"),"utf8");
const start='static char const bootstrap[] = R"NSJS(';
const finish=')NSJS";';
const begin=cpp.indexOf(start);
assert.ok(begin>=0,"V8 bootstrap not found");
const end=cpp.indexOf(finish,begin+start.length);
assert.ok(end>begin,"V8 bootstrap truncated");
const bootstrap=cpp.slice(begin+start.length,end);
const calls=[];
const context={__northstar_native(namespace,method,args) {
    calls.push({namespace,method,args});
    return {namespace,method,args};
}};
vm.runInNewContext(bootstrap,context,{filename:"v8-engine-bootstrap.js"});
const engine=context.engine;
const operations=[
    ["render.sky","configure",[{timeOfDay:14.5}]],
    ["render.shadows","state",[]],
    ["render.shaders","catalog",[]],
    ["render.provider","name",[]],
    ["audio.playback","playNac",["audio/voice.nac","dialogue"]],
    ["audio.voices","stopVoice",[42]],
    ["audio.spatial","setVoicePosition",[42,1,2,3]],
    ["physics.queries","sweepSphere",[0,1,0,0,0,2,.4]],
    ["physics.contacts","pollContacts",[10]],
    ["physics.bodies","applyImpulse",[2,0,0,1]],
    ["input.keyboard","isDown",[87]],
    ["scene.entities","spawn",["Pawn"]],
    ["scene.transforms","moveRuntime",["3",1,2,3]],
    ["scene.animation","setAnimation",[3,"anim.ycd@walk",1]],
    ["camera.cinematics","playCutscene",["cinematic.nscene"]],
    ["player.motion","move",[0,1]],
    ["ecs.entities","query",["Position"]],
    ["vehicle.wheels","wheelCount",[4]],
    ["ui.documents","apply",[{target:"screen",nodes:[]}]],
    ["session.lifecycle","pause",[true]],
    ["storage.saves","write",["slot0","{}"]]
];
for(const [name,method,params] of operations) {
    const pathParts=name.split(".");
    let service=engine;
    for(const part of pathParts)service=service[part];
    assert.strictEqual(typeof service[method],"function",name+"."+method);
    const reply=service[method](...params);
    assert.equal(reply.namespace,"engine."+name);
    assert.equal(reply.method,method);
    assert.deepEqual(Array.from(reply.args),params);
}
assert.equal(calls.length,operations.length);
assert.strictEqual(engine.render.sky,engine.render.sky,
    "Provider namespace identity must be stable");
assert.strictEqual(engine.physics.queries.sweepSphere,
    engine.physics.queries.sweepSphere,
    "Method identity must be stable across reads");
assert.equal(engine.api("engine.render.sky").state().namespace,"engine.render.sky");
assert.equal(engine.call("engine.audio.voices","stopVoice",8).method,"stopVoice");
assert.equal(engine.render.then,undefined,"V8 native proxies are not promises");
assert.equal(typeof engine.on,"function","Engine event bus remains available");
console.log("PASS nested engine namespace bridge:",operations.length,
    "provider-neutral routes; stable identities and direct-scoped access");
