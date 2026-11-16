// Deterministic complete course progression, UI state and audio regression.
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const assert=require("node:assert/strict");
const source=fs.readFileSync(path.join(__dirname,"..","Projects",
    "NorthStarFirstPersonPlatform","scripts","main.js"),"utf8")
    .replace(/^import \{[^\n]*\} from "@northstar\/engine";\s*/m,"")
    .replace("new PistolWeapon().install();","");
const events={start:[],update:[],stop:[]};
const objects=[{index:0,id:"1",name:"Pawn",position:{x:0,y:0,z:0}}];
const hud=[];
const cues=[];
let restart=false;
let restartDown=false;
let cleared=false;
const Scene={
    findPawn:()=>objects[0],
    get:index=>objects[index]||null,
    objectCount:()=>objects.length,
    spawn:params=>{
        const obj={index:objects.length,id:String(objects.length+1),
            name:params.name,position:{...params.position},
            mass:params.mass,size:params.size,color:params.color};
        objects.push(obj);
        return obj;
    },
    setColor:(index,color)=>objects[index].color=color,
    findByName:name=>objects.find(e=>e.name===name)||null
};
const context={
    Script:class {},
    Events:{on:(name,fn)=>events[name].push(fn)},
    Scene,
    Player:{enabled:true,index:0,entityId:"1",paused:false},
    Input:{isDown:()=>restartDown},
    Keys:{T:84},
    UI:{
        setTitle:()=>{},
        mount:(layer,nodes)=>hud.push({layer,nodes}),
        unmount:(layer)=>{assert.equal(layer,"mission");cleared=true;}
    },
    Audio:{playWav:src=>cues.push(src)},
    Session:{restart:()=>{restart=true;}},
    Logger:{info:()=>{},warn:()=>{}}
};
vm.runInNewContext(source,context,{filename:"FirstPersonProtocol/main.js"});
const fire=(event,arg)=>events[event].forEach(fn=>fn(arg));
const assertHud=(stage,label,finished=false)=>{
    const last=hud.at(-1);
    assert.equal(last.layer,"mission");
    const heading=last.nodes.find(node=>node.id==="stage");
    assert.ok(heading && heading.props.text.includes(label));
    assert.ok(heading.props.text.includes(String(stage)+" / 4"));
    assert.equal(finished,last.nodes.find(node=>node.id==="controls").props.text.includes("RESTART"));
};
fire("start");
assert.equal(objects.length,17,"Pawn + 6 beacon parts + 9 decorations + crate");
assertHud(1,"SIGNAL");
assert.deepEqual(cues,["audio/protocol_start.wav"]);
objects[0].position={x:8,y:0.75,z:1};
fire("update",1/60);
assertHud(2,"PHYSICS");
const crate=Scene.findByName("Course Pushable Crate");
assert.ok(crate&&crate.mass>0);
crate.position.x+=1.2;
fire("update",1/60);
assertHud(3,"TRANSIT");
objects[0].position={x:13,y:0.75,z:12};
fire("update",1/60);
assertHud(4,"EXTRACTION");
objects[0].position={x:-7,y:0.75,z:16};
fire("update",1/60);
assertHud(4,"SUCCESS",true);
assert.deepEqual(cues,[
    "audio/protocol_start.wav",
    "audio/protocol_checkpoint.wav",
    "audio/protocol_checkpoint.wav",
    "audio/protocol_checkpoint.wav",
    "audio/protocol_complete.wav"
]);
restartDown=true;
fire("update",1/60);
assert.equal(restart,true);
fire("stop");
assert.equal(cleared,true);
console.log("First Person Protocol mission: PASS (4 objectives, HUD, cues, restart, cleanup)");
