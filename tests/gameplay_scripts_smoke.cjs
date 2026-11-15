// Node smoke tests for project-owned gameplay. Native V8 integration is tested separately.
const fs = require("fs");
const path = require("path");
const assert = require("node:assert/strict");
const vm = require("node:vm");
const os = require("node:os");
const ROOT = path.resolve(__dirname, "..");

function harness(source, objects, options = {}) {
    const events = new Map();
    const logs = [], moves = [], impulses = [], colors = [], transforms = [], titles = [];
    const held = new Set(), mouse = new Set();
    const cameras = [], animations = [], animationStops = [];
    const characterCalls = [];
    let characterSpec = null;
    let pointer = { x: 0, y: 0 }, jumped = 0, paused = false;
    let currentView = "first_person", wheel = 0, followDistance = 4;
    const viewChanges = [], distances = [];
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
            wheelDelta() { return wheel; },
            gamepad() { return null; }
        },
        player: {
            setEyeHeight(value) {assert.ok(value > 0);},
            eye() {return {x:0,y:1.45,z:0};},
            move(x,z) {moves.push({x,z});},
            jump() {jumped++;}
        },
        camera: {
            available() {return options.cameraAvailable !== false;},
            angles() {return cameras.at(-1) || {yaw:0,pitch:0};},
            setMode(mode, duration) {
                assert.ok(["first_person", "third_person", "vehicle_chase",
                    "orbit", "free", "cinematic"].includes(mode));
                assert.ok(duration >= 0 && duration <= 10);
                currentView = mode;
                viewChanges.push({mode, duration});
            },
            mode() {return currentView;},
            followDistance() {return followDistance;},
            setFollowDistance(distance) {
                assert.ok(Number.isFinite(distance) && distance >= 0.5 && distance <= 100);
                followDistance = distance;
                distances.push(distance);
            },
            setAngles(yaw,pitch) {cameras.push({yaw,pitch});},
            setZoom(level) { assert.ok(level>=0 && level<=1);},
            shake() {}
        },
        character: {
            load(descriptor) {
                assert.ok(typeof descriptor.model === "string");
                assert.ok(typeof descriptor.skeleton === "string");
                assert.ok(descriptor.animations.length >= 1);
                characterSpec = descriptor;
                characterCalls.push({event:"load", descriptor});
            },
            pose(index, heading, animation) {
                assert.ok(characterSpec && Number.isFinite(heading));
                assert.ok(objects[index]);
                assert.ok(characterSpec.animations.some(clip => clip.name === animation));
                characterCalls.push({event:"pose", index, heading, animation});
            },
            hide() { characterCalls.push({event:"hide"}); },
            clear() {
                characterSpec = null;
                characterCalls.push({event:"clear"});
            }
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
    const context = vm.createContext({engine, Math, Object, Number, Map, Set,
        Boolean, Error, RangeError});
    // The native ScriptRuntime lowers named @northstar/engine imports for
    // both V8 providers. Mirror that restricted import dialect in the Node
    // harness; runtime integration is verified by Player CTest separately.
    function evaluate(sourceText, filename) {
        let sawImports = false;
        const lowered = sourceText.replace(
            /^\s*import\s*\{([^}]+)\}\s*from\s*["']@northstar\/engine["']\s*;?\s*$/gm,
            (_statement, members) => {
                sawImports = true;
                const entries = members.split(",").map(member => {
                    const parts = member.trim().split(/\s+as\s+/);
                    assert.ok(parts.length >= 1 && parts.length <= 2);
                    return "const " + (parts[1] || parts[0]) +
                        " = globalThis.NorthStar.modules.engine." + parts[0] + ";";
                });
                return entries.join("\n");
            });
        vm.runInContext(sawImports ? "(function(){'use strict';\n" +
            lowered + "\n})();" : lowered,
            context, {filename, timeout:1500});
    }
    const shared = path.join(ROOT, "sharedAssets/scripts/northstar/bootstrap.js");
    vm.runInContext(fs.readFileSync(shared, "utf8"), context,
        {filename:shared, timeout:1500});
    // Input enum classes register immediately after bootstrap, before any
    // script can import Keys or GamepadButtons from @northstar/engine.
    for (const name of ["input/Keys.js", "input/GamepadButtons.js",  "scene/Entity.js", "scene/Prefab.js"]) {
        const preferred = path.join(path.dirname(source), name);
        const sharedFile = path.join(ROOT, "sharedAssets/scripts", name);
        const selected = fs.existsSync(preferred) ? preferred : sharedFile;
        evaluate(fs.readFileSync(selected, "utf8"), selected);
    }
    // Match the engine's grouped script roots and flat legacy VFS aliases.
    const rootScripts = [
        ["character/Character.js","Character.js"],
        ["player/playerConfig.js","playerConfig.js"],
        ["player/playerActor.js","playerActor.js"],
        ["player/playerSession.js","playerSession.js"],
        ["camera/playerCamera.js","playerCamera.js"],
        ["camera/playerLook.js","playerLook.js"],
        ["player/playerMovement.js","playerMovement.js"],
        ["character/playerCharacter.js","playerCharacter.js"],
        ["player/playerInteraction.js","playerInteraction.js"]
    ];
    for (const [grouped,flat] of rootScripts) {
        const option = flat === "playerCamera.js" ? options.cameraScript :
            flat === "playerConfig.js" ? options.configScript : null;
        const preferred = path.join(path.dirname(source),grouped);
        const legacy = option || path.join(path.dirname(source),flat);
        const shared = path.join(ROOT,"sharedAssets/scripts",grouped);
        const chosen=fs.existsSync(preferred) ? preferred :
            fs.existsSync(legacy) ? legacy : shared;
        evaluate(fs.readFileSync(chosen, "utf8"), chosen);
    }
    evaluate(sourceCode, source);
    function dispatch(event,payload) {
        for (const callback of events.get(event) || []) callback(payload);
        if (event === "update") wheel = 0; // Native input samples deltas once per frame.
    }
    return {
        dispatch, held, mouse, moves, impulses, transforms, colors,
        titles, logs, cameras, viewChanges, distances, animations, animationStops, characterCalls,
        get characterSpec() {return characterSpec;},
        get hideInFirstPerson() {
            return vm.runInContext(
                "NorthStar.player.config.character?.hideInFirstPerson !== false",
                context);
        },
        get currentView() {return currentView;},
        get followDistance() {return followDistance;},
        setWheel(delta) {wheel=delta;},
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
    assert.equal(game.currentView, "third_person", "shared controller selects initial view");
    assert.ok(game.characterSpec?.animations.some(clip => clip.name === "idle"));
    assert.ok(game.characterCalls.some(call => call.event === "pose" && call.animation === "idle"));
    game.held.add(87); // W
    game.setPointer({x:4,y:0});
    game.dispatch("update",0.016);
    assert.ok(game.cameras.length && game.moves.some(v => v.z>0.9));
    assert.equal(game.followDistance, 4, "shared camera keeps native distance");
    game.setWheel(120 / 120); // Scroll up approaches the player.
    game.dispatch("update",0.016);
    assert.ok(Math.abs(game.followDistance - 3.3) < 1e-6);
    game.setWheel(-240 / 120); // Scroll down moves away.
    game.dispatch("update",0.016);
    assert.ok(Math.abs(game.followDistance - 4.7) < 1e-6);
    game.setWheel(100);
    game.dispatch("update",0.016);
    assert.equal(game.followDistance, 0.5, "wheel zoom uses the active shared camera policy");
    game.setWheel(-100);
    game.dispatch("update",0.016);
    assert.equal(game.followDistance, 10, "wheel zoom uses the active shared camera policy");
    const before = game.distances.length;
    game.setWheel(-1);
    game.dispatch("update",0.016);
    assert.equal(game.distances.length, before, "clamped zoom emits no redundant calls");

    game.held.add(116); // Legacy F5 must no longer change the view.
    game.dispatch("update",0.016);
    assert.equal(game.currentView, "third_person");
    game.held.delete(116);

    game.held.add(86); // V switches into first person with a zoom-in transition.
    game.dispatch("update",0.016);
    assert.equal(game.currentView, "first_person");
    assert.equal(game.viewChanges.at(-1).duration, 0.45);
    assert.equal(game.characterCalls.at(-1).event,
        game.hideInFirstPerson ? "hide" : "pose",
        "Character visibility follows its project-authored camera policy");
    game.dispatch("update",0.016);
    assert.equal(game.viewChanges.length, 2, "holding V must not retrigger");
    game.setWheel(2);
    game.dispatch("update",0.016);
    assert.equal(game.followDistance, 10, "first-person scroll cannot alter follow distance");
    game.held.delete(86);
    game.dispatch("update",0.016);
    game.held.add(86);
    game.dispatch("update",0.016);
    assert.equal(game.currentView, "third_person");
    assert.equal(game.viewChanges.at(-1).duration, 0.45);
    assert.equal(game.followDistance, 10, "third-person distance survives view switches");
    assert.equal(game.characterCalls.at(-1).event, "pose");
    game.held.delete(86);
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
    assert.equal(game.characterSpec, null, "Script releases character resources");
    return game;
}
function cameraOverrides() {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "northstar-camera-"));
    const source=path.join(ROOT, "Projects/NorthStarFirstPersonPlatform/scripts/main.js");
    const objects=[{index:0,name:"Explorer",player:true,mass:2,position:{x:0,y:0,z:0}}];
    try {
        const custom=path.join(dir,"playerCamera.js");
        fs.writeFileSync(custom, `// Project-owned replacement for scripts/playerCamera.js
(() => {
    const view = NorthStar.camera.createViewController({
        initialMode:"first_person", toggleKey:67,
        transitionSeconds:0.1, minDistance:2, maxDistance:7, wheelStep:1.2
    });
    engine.on("start", () => view.start());
    engine.on("update", () => view.update());
})();`, "utf8");
        const game = harness(source, objects,{cameraScript:custom});
        game.dispatch("start");
        assert.equal(game.currentView,"first_person",
            "project root script must override shared default");
        assert.equal(game.viewChanges.length,1,"default controller executed alongside override");
        game.held.add(86); // shared default V must NOT toggle in override
        game.dispatch("update",0.016);
        assert.equal(game.currentView,"first_person");
        game.held.add(67); // project C toggles to third person
        game.dispatch("update",0.016);
        assert.equal(game.currentView,"third_person");
        assert.equal(game.viewChanges.at(-1).duration,0.1);
        game.setWheel(1);
        game.dispatch("update",0.016);
        assert.ok(Math.abs(game.followDistance-2.8)<1e-6);
        game.dispatch("stop");
        // Scripts in non-player scenes must not fail because no camera exists.
        const noCamera=harness(source,objects,{cameraAvailable:false});
        noCamera.dispatch("start");
        assert.equal(noCamera.viewChanges.length,0);
        noCamera.dispatch("update",0.016);
        assert.equal(noCamera.viewChanges.length,0);
        noCamera.dispatch("stop");
    } finally {
        fs.rmSync(dir,{recursive:true,force:true});
    }
}
function declarativePlayer() {
    const directory=fs.mkdtempSync(path.join(os.tmpdir(),"northstar-player-definition-"));
    try {
        const entry=path.join(directory,"main.js");
        const config=path.join(directory,"playerConfig.js");
        fs.writeFileSync(entry, "// Empty gameplay script; player bootstraps from roots.\\n");
        fs.writeFileSync(config, `NorthStar.player.configure({
            enabled:true, eyeHeight:1.6,
            character:{resources:{
                model:"models/robot/robot.ydd",
                skeleton:"models/robot/robot.ymt",
                material:"materials/robot.ymat",
                textures:"textures/robot.ytd",
                animations:[
                    {name:"idle",source:"animations/robot.ycd",clip:"idle"},
                    {name:"walk",source:"animations/robot.ycd",clip:"walk"},
                    {name:"jump",source:"animations/robot.ycd",clip:"jump"},
                    {name:"fall",source:"animations/robot.ycd",clip:"fall"}
                ]
            }},
            interaction:{enabled:false},
            session:{title:"Test Robot"},
            camera:{initialMode:"first_person", minDistance:1.5,
                    maxDistance:8, wheelStep:0.4}
        });`);
        const game=harness(entry,[
            {index:0,name:"Robot",player:true,mass:2,position:{x:0,y:0,z:0}}
        ]);
        game.dispatch("start");
        assert.equal(game.characterSpec.model,"models/robot/robot.ydd",
            "Player root modules may not hardcode the Abby asset");
        assert.equal(game.currentView,"first_person",
            "Camera policy must come from the project config");
        game.held.add(87);
        game.dispatch("update",0.016);
        assert.ok(game.moves.some(move=>move.z>0.9));
        assert.ok(game.characterCalls.some(c=>c.event==="hide"),
            "First-person character body must be hidden");
        game.held.add(86);
        game.dispatch("update",0.016);
        assert.equal(game.currentView,"third_person");
        assert.ok(game.characterCalls.some(c=>c.event==="pose" && c.animation==="idle"),
            "Third-person must render the declared robot");
        game.dispatch("stop");
        assert.ok(game.characterCalls.some(c=>c.event==="clear"),
            "Character resources must be cleared on game shutdown");
    } finally {
        fs.rmSync(directory,{recursive:true,force:true});
    }
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
cameraOverrides();
declarativePlayer();
if (process.argv.includes("--player-only")) {
    console.log("PASS: declarative player resource and lifecycle contracts");
} else if (process.argv.includes("--camera-only")) {
    console.log("PASS: shared camera view script");
} else {
    arena();
    animationLab();
    console.log("PASS: project gameplay and scene animation JavaScript contracts");
}
