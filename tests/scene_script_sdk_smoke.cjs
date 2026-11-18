// Scene SDK contract checks without a GPU, input device or installed runtime.
"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const original = [
    {index: 0, name: "Road", mass: 0,
        position: {x: 0, y: 0, z: 0}},
    {index: 1, name: "Explorer", mass: 2,
        position: {x: 2, y: 0, z: 0}},
    {index: 2, name: "Crate", player: false, mass: 0,
        position: {x: 4, y: 0, z: 0}},
    {index: 3, name: "Road", player: false, mass: 0,
        position: {x: 20, y: 0, z: 0}}
];
const data = original.map(item => structuredClone(item));
const colors = new Map();
const animations = new Map();
const cameraActions = [];
const engine = {
    player: {controlledIndex: () => 1},
    camera: {
        cut() { cameraActions.push(["cut"]); },
        shake(amount, seconds) {
            cameraActions.push(["shake", amount, seconds]);
        },
        pushShot(...values) { cameraActions.push(["pushShot", ...values]); },
        clearShot(source) { cameraActions.push(["clearShot", source]); },
        clearShots() { cameraActions.push(["clearShots"]); },
        activeShot() { return "cinematic.intro"; },
        applyPreset(id) { cameraActions.push(["applyPreset", id]); }
    },
    scene: {
        objects(start = 0, count = 256) { return data.slice(start, start + count); },
        objectCount() { return data.length; },
        moveStatic(index, x, y, z) {
            assert.ok(index < data.length && data[index].mass === 0 && index !== 1);
            data[index].position = {x, y, z};
        },
        setColor(index, value) {
            assert.ok(index < data.length);
            colors.set(index, value);
        },
        setAnimation(index, value) {
            assert.ok(index < data.length);
            if (value) animations.set(index, value);
            else animations.delete(index);
        }
    }
};
// The fixtures expose the canonical nested API, never flat game bindings.
const flatScene=engine.scene,flatCamera=engine.camera,flatPlayer=engine.player;
engine.scene={
    entities:{objects:flatScene.objects,objectCount:flatScene.objectCount},
    transforms:{moveStatic:flatScene.moveStatic,setColor:flatScene.setColor},
    animation:{setAnimation:flatScene.setAnimation}
};
engine.camera={
    views:{available:()=>true},
    shots:{cut:flatCamera.cut,pushShot:flatCamera.pushShot,
        clearShot:flatCamera.clearShot,clearShots:flatCamera.clearShots,
        activeShot:flatCamera.activeShot,applyPreset:flatCamera.applyPreset},
    effects:{shake:flatCamera.shake}
};
engine.player={controller:{controlledIndex:flatPlayer.controlledIndex}};
const sandbox = vm.createContext({engine});
const bootstrap = path.resolve(__dirname,
    "../engine/northstarcpp/resources/sharedAssets/api/northstar/bootstrap.js");
vm.runInContext(fs.readFileSync(bootstrap, "utf8"), sandbox,
    {filename: bootstrap, timeout: 1500});
const Scene = vm.runInContext("NorthStar.modules.engine.Scene", sandbox);
const Camera = vm.runInContext("NorthStar.modules.engine.Camera", sandbox);
Camera.cut();
Camera.shake(0.15, 0.4);
assert.deepEqual(cameraActions, [["cut"], ["shake", 0.15, 0.4]]);
Camera.pushShot("cinematic.intro", {
    position:{x:10,y:3,z:-8}, yaw:.25,pitch:-.1,fovY:62
},80,.25);
Camera.applyPreset("cinematic.wide");
assert.equal(Camera.activeShot(), "cinematic.intro");
Camera.clearShot("cinematic.intro");
Camera.clearShots();
assert.deepEqual(cameraActions.slice(2), [
    ["pushShot","cinematic.intro",10,3,-8,.25,-.1,0,62,80,.25],
    ["applyPreset","cinematic.wide"],
    ["clearShot","cinematic.intro"],
    ["clearShots"]
]);
assert.throws(() => Camera.pushShot("invalid/path", {
    position:{x:0,y:0,z:0},yaw:0,pitch:0
}),/source/);
assert.throws(() => Camera.pushShot("cinematic.intro", {
    position:{x:NaN,y:0,z:0},yaw:0,pitch:0
}),/transform/);
assert.throws(() => Camera.pushShot("cinematic.intro", {
    position:{x:0,y:0,z:0},yaw:0,pitch:0
},101),/priority/);
assert.equal(Scene.objectCount(), 4);
assert.equal(Scene.get(1).name, "Explorer");
assert.equal(Scene.get(999), null);
assert.equal(Scene.findByName("Road").index, 0);
assert.equal(Scene.findByName("Missing"), null);
assert.equal(Scene.findPawn().index, 1);
assert.equal(Scene.findPlayer().index, 1);
assert.deepEqual(Array.from(Scene.iterate(2), x => x.index), [0,1,2,3]);
assert.deepEqual(Array.from(Scene.nearby({x:0,y:0,z:0}, 5),
    x => x.index), [0,1,2]);
assert.deepEqual(Array.from(Scene.nearby({x:0,y:0,z:0}, 5, 2),
    x => x.index), [0,1]);

Scene.moveStatic(2, {x: 3, y: 1, z: -4});
assert.equal(Scene.get(2).position.z, -4);
Scene.setColor(2, 0xFFAA22);
assert.equal(colors.get(2), 0xFFAA22);
Scene.playAnimation(2, "animations/weapon/pistol/military-pistol.ycd@p-fire");
assert.equal(animations.get(2),
    "animations/weapon/pistol/military-pistol.ycd@p-fire");
Scene.stopAnimation(2);
assert.equal(animations.has(2), false);
assert.throws(() => Scene.playAnimation(2, "../hidden.ycd@fire"), /YCD/);
assert.throws(() => Scene.playAnimation(2, "bad.obj@fire"), /YCD/);
assert.throws(() => Scene.stopAnimation(-1), /index/);
assert.throws(() => Scene.objects(-1, 256), /Scene page/);
assert.throws(() => Scene.objects(0, 513), /Scene page/);
assert.throws(() => Scene.get(-3), /index/);
assert.throws(() => Scene.findByName(""), /name/);
assert.throws(() => Scene.nearby({x: NaN, y: 0, z: 0}, 10), /sphere/);
assert.throws(() => Scene.moveStatic(1, {x: Infinity, y: 0, z: 0}), /position/);
assert.throws(() => Scene.setColor(2, 0x1000000), /RGB/);
console.log("PASS: scene SDK queries, camera cut/shake and static edits");
