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
    // One VFS fixture resolver for scripts, prefabs and UI assets.
    const scriptDir = path.dirname(source);
    const projectAssets = path.basename(scriptDir) === "scripts" &&
        path.basename(path.dirname(scriptDir)) === "assets"
        ? path.dirname(scriptDir) : path.resolve(scriptDir, "..", "assets");
    const resolveAsset = virtualPath => {
        const candidates = [
            path.join(projectAssets, virtualPath),
            path.join(ROOT, "engine/northstarcpp/resources/sharedAssets", virtualPath)
        ];
        const file = candidates.find(candidate => fs.existsSync(candidate));
        assert.ok(file, "Missing project/shared VFS asset: " + virtualPath);
        return file;
    };
    // The current Scene/Prefab SDK creates real entity handles. Mirror its
    // neutral host interface rather than bypassing project weapon scripts.
    const resolvePrefab = resource => {
        assert.equal(resource, "prefabs/weapon/pistol.nprefab");
        const text = fs.readFileSync(resolveAsset(resource), "utf8");
        assert.match(text, /^NorthStarCpp\.Scene 3/m);
        const records = [...text.matchAll(/^entity\s+(\d+)\s+(\d+)\s+"([^"]+)"\s+"([^"]+)"\s+"([^"]*)"\s+"([^"]*)"\s+"([^"]+)"\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)/gm)];
        assert.ok(records.length, "prefab fixture must contain entities");
        return {objects:records.map(match => ({
            id:match[1], parent:match[2], name:match[3],
            layer:match[4], asset:match[7],
            position:{x:Number(match[8]),y:Number(match[9]),z:Number(match[10])},
            size:1, mass:0, color:0x6c86a4, collision:false
        }))};
    };
    const entityById = id => objects.find(o => o && o.id === String(id));
    for(const [index,object] of objects.entries())
        if(object) { object.id ??= String(index+1); object.index ??= index; }

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
        prefab: {read:resolvePrefab},
        scene: {
            objects(start = 0, count = 256) { return objects.slice(start, start + count); },
            objectCount() { return objects.filter(Boolean).length; },
            spawn(name,x,y,z,size,mass,color,asset,layer,parentId,collision) {
                const index=objects.length, id=String(index+1);
                objects.push({index,id,name,mass,size,color,asset,layer,
                    parentId,collision:collision!==false,position:{x,y,z}});
                return {index,id};
            },
            destroy(id) {
                const object=entityById(id);
                if(!object)throw new Error("Unknown runtime entity");
                objects[object.index]=null;
                return true;
            },
            moveRuntime(id,x,y,z) {
                const object=entityById(id);
                if(!object)throw new Error("Unknown runtime entity");
                object.position={x,y,z};
            },
            rotateRuntime(id,x,y,z) {
                const object=entityById(id);
                if(!object)throw new Error("Unknown runtime entity");
                object.rotation={x,y,z};
            },
            setAnimation(index,clip) {assert.ok(objects[index]); return clip;},
            moveStatic(index, x, y, z) {
                transforms.push({index,x,y,z});
                if(objects[index])objects[index].position={x,y,z};
            },
            setColor(index, color) {
                colors.push({index, color});
                if(objects[index])objects[index].color=color;
            }
        },
        input: {
            isDown(code) { return held.has(code); },
            mouseDown(code) { return mouse.has(code); },
            pointerDelta() { return pointer; },
            wheelDelta() { return wheel; },
            gamepad() { return null; }
        },
        player: {
            controlledIndex() { return objects.findIndex(object => object?.player); },
            controlledEntity() { const i = objects.findIndex(object => object?.player); return i < 0 ? null : String(i + 1); },
            setEyeHeight(value) {assert.ok(value > 0);},
            eye() {return {x:0,y:1.45,z:0};},
            move(x,z) {moves.push({x,z});},
            sprint(active) { return Boolean(active); },
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
        ui: {
            readAsset(vpath) {
                return require('../tools/dev/nui_test_bridge.cjs').load(resolveAsset(vpath));
            },
            apply(document) {
                assert.equal(document.target,"screen");
                assert.ok(Array.isArray(document.nodes));
                titles.push(...document.nodes.filter(n=>n.type==="text")
                    .map(n=>String(n.props?.text?.value ?? n.props?.text ?? "")));
                return true;
            },
            setTitle(value) {titles.push(value);}
        }
    };
    // Test the actual public grouped runtime contract, not old flat mocks.
    const flat = {scene:engine.scene, input:engine.input, player:engine.player,
        camera:engine.camera, character:engine.character,physics:engine.physics,
        session:engine.session,audio:engine.audio,animation:engine.animation,
        ui:engine.ui,prefab:engine.prefab};
    engine.scene={
        entities:{objects:flat.scene.objects,objectCount:flat.scene.objectCount,
            spawn:flat.scene.spawn,destroy:flat.scene.destroy},
        transforms:{moveRuntime:flat.scene.moveRuntime,
            rotateRuntime:flat.scene.rotateRuntime,
            moveStatic:flat.scene.moveStatic,setColor:flat.scene.setColor},
        animation:{setAnimation:flat.scene.setAnimation},
        prefabs:flat.prefab
    };
    delete engine.prefab;
    engine.input={keyboard:{isDown:flat.input.isDown},
        pointer:{mouseDown:flat.input.mouseDown,
            pointerDelta:flat.input.pointerDelta,wheelDelta:flat.input.wheelDelta},
        gamepads:{gamepad:flat.input.gamepad}};
    engine.player={controller:{
        controlledIndex:flat.player.controlledIndex,
        controlledEntity:flat.player.controlledEntity},
        motion:{setEyeHeight:flat.player.setEyeHeight,eye:flat.player.eye,
            move:flat.player.move,sprint:flat.player.sprint,jump:flat.player.jump}};
    engine.camera={views:{
        available:flat.camera.available,angles:flat.camera.angles,
        setMode:flat.camera.setMode,mode:flat.camera.mode,
        followDistance:flat.camera.followDistance,
        setFollowDistance:flat.camera.setFollowDistance,
        setAngles:flat.camera.setAngles,setZoom:flat.camera.setZoom},
        effects:{shake:flat.camera.shake}};
    engine.character={visual:flat.character};
    engine.physics={queries:{sweepSphere:flat.physics.sweepSphere},
        bodies:{applyImpulse:flat.physics.applyImpulse}};
    engine.session={lifecycle:flat.session};
    engine.audio={playback:flat.audio};
    engine.animation={clips:flat.animation};
    engine.ui={documents:{readAsset:flat.ui.readAsset,apply:flat.ui.apply},
        viewport:{setTitle:flat.ui.setTitle}};
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
    const shared = path.join(ROOT, "engine/northstarcpp/resources/sharedAssets/api/northstar/bootstrap.js");
    vm.runInContext(fs.readFileSync(shared, "utf8"), context,
        {filename:shared, timeout:1500});
    // Runtime loads the engine-owned UIRoot implementation before any project
    // script uses UI.loadAsset(); this test must mirror the same sequence.
    const uiRootScript=path.join(ROOT,
        "engine/northstarcpp/resources/sharedAssets/api/ui/UI.js");
    vm.runInContext(fs.readFileSync(uiRootScript,"utf8"),context,
        {filename:uiRootScript,timeout:1500});
    // Input enum classes register immediately after bootstrap, before any
    // script can import Keys or GamepadButtons from @northstar/engine.
    for (const name of ["input/Keys.js", "input/GamepadButtons.js",  "scene/Entity.js", "scene/Prefab.js"]) {
        const preferred = path.join(path.dirname(source), name);
        const sharedFile = path.join(ROOT, "engine/northstarcpp/resources/sharedAssets/api", name);
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
        const shared = path.join(ROOT,"engine/northstarcpp/resources/sharedAssets/api",grouped);
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
    const game = harness(path.join(ROOT,"Projects/NorthStarFirstPersonPlatform/assets/scripts/main.js"),[
        {index:0,name:"Explorer",player:true,mass:2,position:{x:0,y:0,z:0}},
        {index:1,name:"Physics Crate",player:false,mass:2,position:{x:0,y:0,z:3}}
    ]);
    game.dispatch("start");
    assert.equal(game.currentView, "first_person", "project-authored first-person camera initializes");
    assert.ok(game.characterSpec?.animations.some(clip => clip.name === "idle"));
    assert.ok(game.characterCalls.some(call => call.event === "hide"),
        "first-person initialization hides third-person character mesh");
    // Exercise third-person zoom after switching from the project-authored first-person default.
    game.held.add(86);
    game.dispatch("update",0.016);
    game.held.delete(86);
    game.dispatch("update",0.016);
    assert.equal(game.currentView, "third_person");
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
    assert.equal(game.viewChanges.length, 3, "holding V must not retrigger after initial third-person toggle");
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
    const source=path.join(ROOT, "Projects/NorthStarFirstPersonPlatform/assets/scripts/main.js");
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
    const project=path.join(ROOT,"tests/fixtures/animation_lab/assets");
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
