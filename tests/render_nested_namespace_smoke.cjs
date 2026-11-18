"use strict";
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const assert=require("node:assert/strict");

const bundle=fs.readFileSync(path.join(__dirname,"..","engine",
    "northstarcpp","resources","sharedAssets","api",
    "northstar","bootstrap.js"),"utf8");
let nativeCalls=0;
let daytime=9;
const sky={
    state(){nativeCalls++;return {
        enabled:true,paused:false,sun:true,moon:true,stars:true,clouds:true,
        timeOfDay:daytime,dayLengthSeconds:2400,latitudeDegrees:32,
        dayOfYear:250,moonPhase:.5,cloudCoverage:.4,cloudSpeed:.01,haze:.2,
        fogDensity:.012,sunIntensity:1.5,moonIntensity:.2,starDensity:.4,
        sunDirection:{x:0,y:1,z:0},moonDirection:{x:0,y:-1,z:0},
        sunElevationDegrees:75,moonElevationDegrees:-35,
        daylight:.9,cloudOffset:0,revision:nativeCalls
    }},
    configure(json){const next=JSON.parse(json);if(next.timeOfDay!==undefined)daytime=next.timeOfDay;return true},
    setTimeOfDay(hours){daytime=hours;return true},
    reset(){return true},
    advance(){return true}
};
const engine={
    sky,
    render:{
        provider:{name(){return "D3D12"},engineVersion(){return "1.0"},
                  detailLevel(){return "high"},setDetailLevel(){return "high"}},
        // Canonical sky and old engine.sky delegate to one service.
        sky,
        shaders:{
            catalog(){return {provider:"D3D12",builtin:[],hotReload:false}},
            source(){return "ok"},
            queueCompiled(){return 1},
            poll(){return {pending:false,completed:[]}}
        },
        shadows:{
            state(){return {provider:"D3D12",requested:true,filter:"pcss",
                           cascades:3,verifiedOnGpu:false,requiresProviderRestart:true}},
            capabilities(){return {csm:true,pcf:true,pcss:true,runtimeToggle:false}}
        },
        shaderCatalog(){throw new Error("Legacy shader method should not be used")}
    }
};
const context={engine};
vm.runInNewContext(bundle,context,{filename:"northstar-render-namespaces.js"});
const API=context.NorthStar.modules.engine.API;
assert.equal(typeof API.render.sky,"function");
assert.equal(typeof API.render.shadows,"function");
assert.equal(typeof API.render.shaders,"function");
assert.equal(API.render.sky.state().timeOfDay,9);
API.render.sky.setTimeOfDay(10,30);
assert.equal(API.render.sky.timeOfDay,10.5);
assert.equal(API.render.provider.name(),"D3D12");
assert.equal(API.render.shadows.state().filter,"pcss");
assert.equal(API.render.shadows.capabilities().pcss,true);
assert.ok(Object.isFrozen(API.render.shadows.state()));
assert.ok(Object.isFrozen(API.render.shadows.capabilities()));
console.log("PASS nested engine.render.sky/shadows/shaders API and root sky alias");
