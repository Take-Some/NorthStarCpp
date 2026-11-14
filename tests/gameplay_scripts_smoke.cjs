// Node smoke tests for project-owned gameplay. Native V8 integration is tested separately.
const fs = require("fs");
const path = require("path");
const assert = require("node:assert/strict");
const vm = require("node:vm");
const ROOT = path.resolve(__dirname, "..");

function harness(source, objects) {
    const events = new Map();
    const logs = [], moves = [], impulses = [], colors = [], transforms = [], titles = [];
    const held = new Set(), mouse = new Set();
    const cameras = [], animations = [], animationStops = [];
    let pointer = { x: 0, y: 0 }, jumped = 0, paused = false;
    const engine = {
        on(event, fn) {
            const handlers = events.get(event) || [];
            handlers.push(fn);
            events.set(event, handlers);
        },
        logger: {
            debug: s => logs.push(["debug", s]),
            info: s => logs.push(["info", s]),
            warn: s => logs.push(["warn", s]),
            error: s => logs.push(["error", s])
        },
        scene: {
            objects(start = 0, count = 256) { return objects.slice(start, start + count); },
            moveStatic(index, x, y, z) { transforms.push({index, x, y, z}); },
            setColor(index, color) { colors.push({index, color}); }
        },
        input: {
            isDown(code) { return held.has(code); },
            mouseDown(code) { return mouse.has(code); },
            pointerDelta() { return pointer; },
            gamepad() { return null; }
        },
        player: {
            setEyeHeight(value) {assert.ok(value > 0);},
            eye() {return {x:0,y:1.45,z:0};},
            move(x,z) {moves.push({x,z});},
            jump() {jumped++;}
        },
        camera: {
            setAngles(yaw,pitch) {cameras.push({yaw,pitch});},
            setZoom(level) { assert.ok(level>=0 && level<=1);},
            shake() {}
        },
        physics: {
            sweepSphere() {return {index:1,fraction:0.1};},
            applyImpulse(index,x,y,z) {impulses.push({index,x,y,z});}
        },
        session: {
            pause(value) {paused=!!value;},
            quit() {logs.push(["quit"]);},
            restart() {logs.push(["restart"]);}
        },
        audio: {play() {}},
        animation: {
            play(id, file, loop, speed) { animations.push({id,file,loop,speed}); },
            state() { return {finished:false,time:0}; },
            stop(id) { animationStops.push(id); }
        },
        ui: {setTitle(value) {titles.push(value);}}
    };
    const sourceCode = fs.readFileSync(source, "utf8");
    vm.runInNewContext(sourceCode, {engine, Math, Object, Number, Map, Set, Boolean, Error},
        {filename:source, timeout:1500});
    function dispatch(event,payload) {
        for (const callback of events.get(event) || []) callback(payload);
    }
    return {
        dispatch, held, mouse, moves, impulses, transforms, colors,
        titles, logs, cameras, animations, animationStops,
        setPointer(delta) {pointer=delta;},
        get jumped() {return jumped;},
        get paused() {return paused;}
    };
}
function platform() {
    const game = harness(path.join(ROOT,"Projects/NorthStarFirstPersonPlatform/scripts/main.js"),[
        {index:0,name:"Explorer",player:true,mass:2,position:{x:0,y:0,z:0}},
        {index:1,name:"Physics Crate",player:false,mass:2,position:{x:0,y:0,z:3}}
    ]);
    game.dispatch("start");
    game.held.add(87); // W
    game.setPointer({x:4,y:0});
    game.dispatch("update",0.016);
    assert.ok(game.cameras.length && game.moves.some(v => v.z>0.9));
    game.held.add(32); // Space
    game.held.add(69); // Interact
    game.dispatch("update",0.016);
    assert.equal(game.jumped,1,"jump must occur on key rising edge");
    assert.equal(game.impulses.length,1,"interaction must call physics API");
    game.dispatch("update",0.016);
    assert.equal(game.jumped,1,"holding jump must not repeat");
    game.held.add(27); // Pause
    game.dispatch("update",0.016);
    assert.equal(game.paused,true);
    game.dispatch("stop");
    return game;
}
function arena() {
    const game = harness(path.join(ROOT,"tools/templates/northstar_fps_arena.js"),[
        {index:0,name:"Player",player:true,mass:2,size:1.5,position:{x:0,y:0,z:-8}},
        {index:1,name:"Target Moving Alpha",player:false,mass:0,size:1.2,
         position:{x:0,y:1,z:7}},
        {index:2,name:"Hazard A",player:false,mass:0,size:1,
         position:{x:8,y:0,z:8}}
    ]);
    game.dispatch("start");
    game.dispatch("update",0.05);
    assert.equal(game.transforms.length,1,"moving targets must be script-driven");
    game.mouse.add(0); // Fire
    game.dispatch("update",0.17);
    assert.deepEqual(game.colors[0],{index:1,color:0x3cffb5});
    assert.ok(game.titles.some(text=>text.includes("1/1 hits")));
    game.held.add(13); // Restart after win
    game.dispatch("update",0.016);
    assert.ok(game.logs.some(([type])=>type==="restart"));
    game.dispatch("stop");
    return game;
}
function animationLab() {
    const project=path.join(ROOT,"Projects/NorthStarAnimationLab");
    const clip=JSON.parse(fs.readFileSync(
        path.join(project,"animations/lift.anim.json"),"utf8"));
    assert.equal(clip.schema,1);
    assert.equal(clip.type,"scene-transform");
    assert.ok(clip.keyframes.length>=2);
    const game=harness(path.join(project,"scripts/main.js"),[
        {index:0,id:"1",name:"Player",player:true,position:{x:0,y:0,z:-8}},
        {index:1,id:"3",name:"Animated Cube",player:false,
            position:{x:0,y:1,z:4}}
    ]);
    game.dispatch("start");
    assert.equal(game.animations.length,1);
    assert.equal(game.animations[0].id,"3");
    assert.equal(game.animations[0].file,"animations/lift.anim.json");
    assert.equal(game.animations[0].loop,true);
    game.dispatch("update",0.016);
    game.dispatch("stop");
    assert.deepEqual(game.animationStops,["3"]);
}
platform();
arena();
animationLab();
console.log("PASS: project gameplay and scene animation JavaScript contracts");
