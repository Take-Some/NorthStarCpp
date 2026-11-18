// Source-level @northstar/engine.Vehicle adapter to native engine.vehicle.
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const root=path.resolve(__dirname,"../engine/northstarcpp/resources/sharedAssets/api");
const text=fs.readFileSync(path.join(root,"northstar/bootstrap.js"),"utf8");
const calls=[];
const engine={vehicle:new Proxy({},{
  get(_target,key){return (...args)=>{calls.push({key,args});return key==="powertrainState"?
    {gear:2,rpm:2450,torqueNm:280,wheelForceN:3500,shiftingSeconds:0}:true;};}
})};
const context=vm.createContext({engine});
vm.runInContext(text,context,{filename:"northstar/bootstrap.js"});
const Vehicle=vm.runInContext("NorthStar.modules.engine.API.vehicle",context);
assert.equal(Vehicle,vm.runInContext("NorthStar.modules.engine.Vehicle",context));
assert.deepEqual(Vehicle.powertrainState(42),
  {gear:2,rpm:2450,torqueNm:280,wheelForceN:3500,shiftingSeconds:0});
assert.equal(Vehicle.selectGear(42,3),true);
assert.equal(Vehicle.configureRig(42,"vehicles/example.nvehicle"),true);
assert.equal(Vehicle.waypoint(42,3,8,10),true);
assert.equal(calls[0].key,"powertrainState");
assert.deepEqual(calls[1],{key:"selectGear",args:[42,3]});
assert.deepEqual(calls[3],{key:"waypoint",args:[42,3,8,10,2]});
const typings=fs.readFileSync(path.join(root,"types/northstar-engine.d.ts"),"utf8");
assert.match(typings,/export class Vehicle/);
assert.match(typings,/export interface VehiclePowertrainState/);
console.log("@northstar/engine.Vehicle powertrain and typed facade: PASS");
