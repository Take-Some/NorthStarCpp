// Scene SDK contract checks without a GPU, input device or installed runtime.
"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const original = [
    {index: 0, name: "Road", player: false, mass: 0,
        position: {x: 0, y: 0, z: 0}},
    {index: 1, name: "Explorer", player: true, mass: 2,
        position: {x: 2, y: 0, z: 0}},
    {index: 2, name: "Crate", player: false, mass: 0,
        position: {x: 4, y: 0, z: 0}},
    {index: 3, name: "Road", player: false, mass: 0,
        position: {x: 20, y: 0, z: 0}}
];
const data = original.map(item => structuredClone(item));
const colors = new Map();
const animations = new Map();
const engine = {
    scene: {
        objects(start = 0, count = 256) { return data.slice(start, start + count); },
        objectCount() { return data.length; },
        moveStatic(index, x, y, z) {
            assert.ok(index < data.length && data[index].mass === 0 && !data[index].player);
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
const sandbox = vm.createContext({engine});
const bootstrap = path.resolve(__dirname,
    "../sharedAssets/scripts/northstar/bootstrap.js");
vm.runInContext(fs.readFileSync(bootstrap, "utf8"), sandbox,
    {filename: bootstrap, timeout: 1500});
const Scene = vm.runInContext("NorthStar.modules.engine.Scene", sandbox);
assert.equal(Scene.objectCount(), 4);
assert.equal(Scene.get(1).name, "Explorer");
assert.equal(Scene.get(999), null);
assert.equal(Scene.findByName("Road").index, 0);
assert.equal(Scene.findByName("Missing"), null);
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
console.log("PASS: scene SDK search, paging, nearby and static edits");
