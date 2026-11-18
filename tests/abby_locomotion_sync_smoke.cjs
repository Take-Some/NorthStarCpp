"use strict";
// Regression for script-selected locomotion clips and playback rate.
// Runs the real project script without launching a renderer or modifying game files.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

for (const relative of [
    "Projects/AbbyPistolDemo/assets/scripts/main.js",
    "tools/templates/abby_pistol_demo/scripts/main.js"
]) {
    const source = fs.readFileSync(relative, "utf8")
        .replace(/^import\s+\{[^\n]+\}\s+from\s+"@northstar\/engine";\r?\n/m, "");
    const pawn = {id: 1, position: {x: 0, y: 0, z: 0}};
    const actor = {alive: true, index: 44,
        setRotation() {}, setPosition() {}};
    const calls = [];
    let visual = null;
    const names = ["idle", "walk", "run", "sprint"];
    const cfg = {
        prefab: "prefabs/characters/abby.nprefab",
        resources: {animations: names.map(name => ({
            name, source: "characters/abby/animations/mm-explore.ycd",
            clip: "abby-mm-explore-" + name
        }))},
        firstPersonViewmodel: {forward: 0.52, drop: 0.24, right: 0.12},
        locomotion: {
            walkSpeed: 1.48, runSpeed: 2.95, sprintSpeed: 4.44,
            walkToRun: 2.215, runToSprint: 3.695, movementThreshold: .2,
            hysteresis: .18, smoothingSeconds: .12,
            minPlaybackRate: .65, maxPlaybackRate: 1.5
        }
    };
    const context = {
        Script: class {},
        Player: {enabled: true, paused: false, index: 1,
            config: {character: cfg, weapon: {enabled: false}}},
        Scene: {
            findPawn: () => pawn,
            findByName: () => ({parent: "1"}),
            handleByName: () => actor,
            setVisualMode() {},
            playAnimation: (index, ref, rate) => calls.push({index, ref, rate})
        },
        Camera: {mode: () => "first_person", angles: () => ({yaw: 0})},
        Input: {mouseDown: () => false},
        Logger: {info() {}},
        Factory: {
            script(Class) {visual = new Class(); return visual;},
            weapon() {return {install() {}};}
        },
        API: {sky: {configure() {}}},
        Sky: {configure() {}}
    };
    vm.runInNewContext(source, context, {filename: relative});
    assert.ok(visual, relative + ": CharacterVisual not installed");
    visual.start();
    const step = (metresPerSecond, frames = 18) => {
        for(let n=0;n<frames;n++) {
            pawn.position.z += metresPerSecond * 0.1;
            visual.update(0.1);
        }
    };
    assert.match(calls.at(-1).ref, /idle$/, relative + " idle");
    step(1.48);
    assert.match(calls.at(-1).ref, /walk$/, relative + " walk");
    step(2.95);
    assert.match(calls.at(-1).ref, /run$/, relative + " run");
    step(4.44);
    assert.match(calls.at(-1).ref, /sprint$/, relative + " sprint");
    step(4.88);
    assert.ok(calls.some(v => v.ref.endsWith("sprint") &&
              v.rate > 1.05), relative + " adaptive playback rate");
    step(0, 25);
    assert.match(calls.at(-1).ref, /idle$/, relative + " stop to idle");
    assert.ok(calls.every(v => v.index === 44 && v.rate >= 0.1 &&
              v.rate <= 4), relative + " per-entity bounded rate");
    visual.stop();
    console.log("PASS:", relative,
        "idle -> walk -> run -> sprint -> idle; adaptive playback");
}
