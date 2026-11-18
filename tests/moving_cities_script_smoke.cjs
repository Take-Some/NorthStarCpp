const fs=require("node:fs"),path=require("node:path"),vm=require("node:vm");
const assert=require("node:assert/strict");
const ROOT=path.resolve(__dirname,"..");
const source=fs.readFileSync(path.join(ROOT,"Projects/MovingCitiesPrototype/assets/scripts/main.js"),"utf8");
const Keys={E:69,T:84,I:73,K:75};
const down=new Set(),events={},moves=new Map(),colors=new Map(),objects=[];
function add(name,position){const o={name,position:{...position},index:objects.length};objects.push(o);return o;}
const pawn=add("Prototype Pawn",{x:0,y:6.4,z:8});
add("BASTION Helm Console",{x:0,y:9,z:8});
add("CITYROOT Bastion",{x:0,y:0,z:0});
add("CITYROOT Rust Wren",{x:0,y:0,z:135});
// TerrainProvider streams world-fixed heightfield chunks. Do not
// reintroduce the obsolete finite 289-cell mesh/rock workaround.
for(let i=0;i<50;i++)
    add("RUSTWREN tile "+i,{x:i%10,y:5.7,z:135+Math.floor(i/10)});
for(const side of ["L","R"]){
 for(let i=0;i<48;i++)add("BASTION TREAD "+side+" "+String(i).padStart(2,"0"),
     {x:0,y:2,z:i});
 for(let i=0;i<7;i++)add("BASTION ROADWHEEL "+side+" "+String(i).padStart(2,"0"),
     {x:0,y:2,z:i});
}
for(let i=0;i<24;i++)add("BASTION DRIVE DUST "+String(i).padStart(2,"0"),
    {x:0,y:-20,z:0});
const vehicles=new Map(),angles=new Map();
const Vehicle={
 available:()=>true,
 attach(index,base,speed){
  const p=objects[index].position;
  vehicles.set(index,{x:p.x,z:p.z,heading:0,speed:0,target:0,maxSpeed:speed,cruise:null});
  return true;
 },
 configureRig:()=>true,
 state:index=>({...vehicles.get(index)}),
 drive(index,{throttle=0,brake=0}){
  const v=vehicles.get(index);v.target=Math.max(0,Math.min(v.maxSpeed,
      v.speed+throttle*1.5-brake*1.5));return true;
 },
 navigate(index,{speed}){vehicles.get(index).cruise=speed;return true;},
 detach:index=>vehicles.delete(index),
 provider:()=>"mock_native_vehicle"
};
class Script {
 install(){
  events.start=()=>this.start();
  events.update=dt=>{
   this.update(dt);
   for(const v of vehicles.values()) {
    v.speed=v.cruise??v.target;
    v.z+=v.speed*dt;
   }
  };
  events.stop=()=>this.stop();
  return this;
 }
}
const Factory={script:Type=>new Type().install()};
const views=[],state={logs:[],restart:0};
const engine={
 Factory,Script,Vehicle,
 Events:{on:(k,fn)=>events[k]=fn},
 Scene:{
     iterate:function*(){yield* objects;},
     findPawn:()=>pawn,
     findByName:n=>objects.find(o=>o.name===n)||null,
     moveStatic:(i,p)=>{assert([p.x,p.y,p.z].every(Number.isFinite));moves.set(i,{...p});},
     setColor:(i,c)=>{assert(Number.isInteger(c));colors.set(i,c);},
     rotateStatic:(i,p)=>{angles.set(i,p);}
 },
 Player:{enabled:true,index:0,paused:false},
 Input:{isDown:k=>down.has(k)},Keys,
 UI:{
     loadAsset:()=>{const view={fields:{},batch:fn=>fn(),
         setText:function(k,t){this.fields[k]=t;},
         mount:()=>{},dispose:()=>{}};views.push(view);return view;},
     setTitle:()=>{}
 },
 Camera:{available:()=>false},
 Session:{restart:()=>state.restart++},
 Logger:{info:t=>state.logs.push(t),warn:t=>state.logs.push(t)}
};
const transformed=source.replace(/^import\s*\{[^}]+\}\s*from\s*"@northstar\/engine";/m,
    "const {Factory,Script,Vehicle,Scene,Player,Input,Keys,UI,Session,Camera,Logger}=globalThis.__mock;");
assert.notEqual(transformed,source);
vm.runInNewContext(transformed,{__mock:engine,Math,Number,Object,String,Error});
events.start();
assert.equal(views.length,1);
assert.equal(colors.size,0,"Terrain should not be repainted when cities move");
assert.match(views[0].fields.stage,/BOARD THE BRIDGE/);
down.add(Keys.E);events.update(.05);down.delete(Keys.E);events.update(.05);
assert.match(views[0].fields.stage,/CITY PURSUIT/);
down.add(Keys.I);for(let i=0;i<600;i++)events.update(.05);down.delete(Keys.I);
assert(moves.size>100,"World structures and moving tracks require native transforms");
assert(angles.size>50,"Treads must animate via real object rotations");
assert.match(views[0].fields.stage,/INTERCEPT/);
assert.match(views[0].fields.timer,/TRAVEL /);
const gap=Number(views[0].fields.timer.match(/GAP ([0-9.]+)/)[1]);
assert(gap>=0&&gap<60,"Pursuit mock should converge to target gap: "+gap);
// Native Jolt carries the Pawn on the moving city deck. Emulate that
// physical transport before approaching the helm to finish the capture.
pawn.position.z=Vehicle.state(2).z+8;
pawn.position.y=5.7;
down.add(Keys.E);events.update(.05);down.delete(Keys.E);
assert.match(views[0].fields.stage,/CAPTURE COMPLETE/);
down.add(Keys.T);events.update(.05);
assert.equal(state.restart,1);
events.stop();
assert(state.logs.some(x=>x.includes("TerrainProvider heightfield")));
console.log(JSON.stringify({pass:true,terrainCells:0,
    updatedEntities:moves.size,animatedRotations:angles.size,gap,restarts:state.restart}));
