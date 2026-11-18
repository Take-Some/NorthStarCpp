"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const api=path.resolve(__dirname,
    "../engine/northstarcpp/resources/sharedAssets/api");
const context=vm.createContext({engine:{}});
vm.runInContext(fs.readFileSync(path.join(api,"northstar/bootstrap.js"),"utf8"),
    context,{filename:"northstar/bootstrap.js"});
const sdk=vm.runInContext("NorthStar",context);
const modules=sdk.modules.engine;
const {API,EngineAPI,Factory}=modules;
assert.equal(API.constructor,EngineAPI,"Public root must be an EngineAPI instance");
assert.equal(Object.getPrototypeOf(modules).constructor.name,"EngineModuleRegistry");
assert.equal(Object.getPrototypeOf(sdk.modules).constructor.name,"SDKModuleCatalog");
assert.equal(sdk.constructor.name,"NorthStarSDK");
assert.ok(Object.isFrozen(API));
assert.ok(Object.isFrozen(modules));
assert.ok(Object.isFrozen(sdk.modules));
assert.throws(()=>new EngineAPI(),/singleton/);
for(const domain of ["Factory","Camera","Input","Events","Audio","Character",
    "Player","Vehicle","Script","PlayerScript","InputAction","Scene",
    "Physics","Collision","Particles","WeaponBallistics","WeaponEffects",
    "WeaponController","Session","UI","Logger"]) {
    assert.equal(typeof modules[domain],"function",domain+" must be a class");
}
for(const property of ["entity","vehicle","weapon","ballistics","weaponEffects",
    "camera","physics","collision","particles","character","player","events",
    "input","audio","ui","session","logger"]) {
    assert.equal(typeof API[property],"function",property+" must reference a class");
}
assert.equal(API.Factory,Factory);
assert.equal(API.audio,modules.Audio);
assert.equal(API.physics,modules.Physics);
assert.equal(API.weapon,modules.WeaponController);
const handle=Factory.register("project.unit",()=>({ok:true}));
assert.equal(handle.constructor.name,"FactoryRegistration");
assert.equal(typeof handle.dispose,"function");
assert.ok(Object.isFrozen(handle));
assert.equal(Factory.create("project.unit").ok,true);
assert.equal(handle.dispose(),true);
assert.equal(handle.dispose(),false);
assert.throws(()=>Factory.create("project.unit"),/Unknown factory recipe/);
assert.ok(!("staticApi" in API));
console.log("Engine API OOP: PASS (class singleton, immutable registry, class services, factory registration lifecycle)");
