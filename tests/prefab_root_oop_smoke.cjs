// Native-independent prefab authoring graph regression.
"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const BASE=path.join(__dirname,"..","engine","northstarcpp","resources","sharedAssets","api");
const created=[],destroyed=[],live=[];
let definitions=[],failAfter=Infinity;
const engine={
    scene:{entities:{
        objects:(start=0,count=256)=>live.slice(start,start+count),
        objectCount:()=>live.length,
        spawn:(name,x,y,z,size,mass,color,asset,layer,parent,collision)=>{
            if(created.length>=failAfter)throw new Error("provider spawn failed");
            const index=live.length;
            const id=String(index+1);
            live.push({name,index,id,position:{x,y,z},mass});
            created.push({name,index,id,parent,mass,collision:collision??true});
            return {id,index};
        },
        destroy:id=>{
            const object=live.find(e=>e&&e.id===id);
            if(!object)throw new Error("destroyed entity missing");
            live[object.index]=null;
            destroyed.push(id);
        }
    },prefabs:{read:()=>({objects:definitions})}}
};
const context=vm.createContext({engine});
for(const file of ["northstar/bootstrap.js","scene/Entity.js","scene/Prefab.js"]) {
    const source=fs.readFileSync(path.join(BASE,file),"utf8");
    vm.runInContext(source,context,{filename:file});
}
const Prefab=vm.runInContext("NorthStar.modules.engine.Prefab",context);
const entity=(id,parent,name,x)=>({
    id,parent,name,position:{x,y:0,z:0},
    size:1,mass:0,color:0xaabbcc,asset:"",layer:"Default"
});
definitions=[
    entity("3","2","Grandchild",3),
    entity("2","1","Child",2),
    entity("1","0","Root",1)
];
const instance=Prefab.instantiate("prefabs/testing.nprefab",{
    position:{x:10,y:0,z:0}
});
assert.deepEqual(created.map(e=>e.name),["Root","Child","Grandchild"]);
assert.deepEqual(live.filter(Boolean).map(e=>e.position.x),[11,12,13]);
assert.equal(created[1].parent,created[0].id);
assert.equal(created[2].parent,created[1].id);
assert.equal(instance.alive,true);
assert.equal(instance.destroy(),3);
assert.deepEqual(destroyed,["3","2","1"]);
assert.equal(instance.alive,false);
// One canonical prefab can be spawned free (default physical collision)
// or equipped (no collision) without a pistol_attached duplicate.
const restoredDefinitions=definitions;
definitions=[entity("1","0","Pistol",0)];
definitions[0].mass=0.65;
definitions[0].collision=true;
const physical=Prefab.instantiate("prefabs/weapon/pistol.nprefab");
assert.equal(created.at(-1).mass,.65,"world gun retains authored mass");
assert.equal(created.at(-1).collision,true);
assert.equal(physical.destroy(),1);
const equipped=Prefab.instantiate("prefabs/weapon/pistol.nprefab",{
    position:{x:0,y:1.6,z:0},physics:"visual"
});
assert.equal(created.at(-1).mass,0,"attached instance must not be a dynamic body");
assert.equal(created.at(-1).collision,false);
assert.equal(equipped.destroy(),1);
assert.throws(()=>Prefab.instantiate("prefabs/weapon/pistol.nprefab",{
    physics:"visual",collision:true
}),/valid anchor and physics options/);
definitions=restoredDefinitions;

created.length=0;
destroyed.length=0;
failAfter=2;
assert.throws(()=>Prefab.instantiate("prefabs/testing.nprefab"),/provider spawn failed/);
assert.equal(live.filter(Boolean).length,0,
    "failed child creation must clean up all created parents");
assert.equal(destroyed.length,2,"rollback destroys all created entities");
failAfter=Infinity;
definitions=[entity("1","2","First",0),entity("2","1","Second",0)];
const old=created.length;
assert.throws(()=>Prefab.instantiate("prefabs/testing.nprefab"),/hierarchy cycle/);
assert.equal(created.length,old,"cycle cannot create partial world objects");
definitions=[entity("1","888","Orphan",0)];
assert.throws(()=>Prefab.instantiate("prefabs/testing.nprefab"),/missing parent/);
assert.equal(created.length,old);
console.log("Prefab root OOP: PASS (linear dependency ordering, cycles, rollback)");
