"use strict";
// Regression: three calibrated humanoid gait speeds from the generic engine
// PlayerMovement controller. This tests the input->motor interface; physics
// authority stays in the native CharacterMotor/Jolt provider.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const source = fs.readFileSync(
    "engine/northstarcpp/resources/sharedAssets/api/player/playerMovement.js", "utf8"
).replace(/^import\s+\{[^\n]+\}\s+from\s+"@northstar\/engine";\r?\n/m,"");
const Keys = {Shift:16, Control:17, Space:32, W:87, A:65, S:83, D:68};
const GamepadButtons = {
    Jump:0, DPadRight:1, DPadLeft:2, DPadUp:3, DPadDown:4
};
const held = new Set();
const output = [];
let controller;
const context = {
    PlayerScript: class {
        active = true;
        options = {movement:{walkScale:1.48/2.95, walkKey:Keys.Control,
                             sprintKey:Keys.Shift}};
        install() {controller=this;}
    },
    Keys, GamepadButtons,
    Input: {
        firstGamepad: () => null,
        createAction: () => ({pressed:()=>false, reset(){}}),
        axis(plus,minus,analog) {
            if(held.has(plus))return 1;
            if(held.has(minus))return -1;
            return analog;
        },
        normalized2D(x,y) {
            const n=Math.max(1,Math.hypot(x,y));
            return {x:x/n,y:y/n};
        },
        isDown(key) {return held.has(key);}
    },
    Player: {
        paused: false,
        move(x,z) {output.push({kind:"move",x,z});},
        sprint(value) {output.push({kind:"sprint",value});},
        jump() {}
    },
    Camera: {angles:()=>({yaw:0})}
};
vm.runInNewContext(source,context,{filename:"playerMovement.js"});
assert.ok(controller);
controller.start();
function frame(keys,speed,sprint) {
    held.clear();keys.forEach(k=>held.add(k));output.length=0;
    controller.update();
    const move=output.find(e=>e.kind==="move");
    const state=output.find(e=>e.kind==="sprint");
    assert.ok(move && state);
    assert.equal(state.value,sprint);
    assert.ok(Math.abs(move.z*(sprint?4.44:2.95)-speed)<.00001,
        "Expected "+speed+" m/s, got "+
        (move.z*(sprint?4.44:2.95)));
}
frame([Keys.W,Keys.Control],1.48,false);
frame([Keys.W],2.95,false);
frame([Keys.W,Keys.Shift],4.44,true);
frame([Keys.W,Keys.Control,Keys.Shift],1.48,false);
context.Player.paused=true;
frame([Keys.W,Keys.Shift],0,false);
context.Player.paused=false;
controller.stop();
console.log("PASS generic locomotion: Ctrl 1.48 m/s; WASD 2.95; Shift 4.44; pause/sprint precedence");
