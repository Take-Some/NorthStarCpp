"use strict";
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const assert=require("node:assert/strict");

const bundle=fs.readFileSync(path.join(__dirname,"..","engine",
    "northstarcpp","resources","sharedAssets","api",
    "northstar","bootstrap.js"),"utf8");
let native={
    enabled:false,paused:false,sun:true,moon:true,stars:true,clouds:true,
    timeOfDay:12,dayLengthSeconds:1200,latitudeDegrees:42,dayOfYear:172,
    moonPhase:.5,cloudCoverage:.3,cloudSpeed:.015,haze:.2,
    sunIntensity:1,moonIntensity:.25,starDensity:.55,
    sunDirection:{x:0,y:1,z:0},moonDirection:{x:0,y:-1,z:0},
    sunElevationDegrees:90,moonElevationDegrees:-90,
    daylight:1,cloudOffset:0,revision:0
};
const records=[];
const context={engine:{render:{sky:{
    configure(json) {
        records.push(["configure",json]);
        native={...native,...JSON.parse(json),revision:native.revision+1};
        return true;
    },
    setTimeOfDay(hour) {
        records.push(["time",hour]);
        native.timeOfDay=hour;return true;
    },
    reset() {native.enabled=false;native.revision++;return true;},
    advance(seconds) {
        records.push(["advance",seconds]);
        if(!native.paused)
            native.timeOfDay=(native.timeOfDay+seconds*24/native.dayLengthSeconds)%24;
        return true;
    },
    state() {return structuredClone(native);}
}}}};
vm.runInNewContext(bundle,context,{filename:"engine-sky-bootstrap.js"});
const Sky=context.NorthStar.modules.engine.RenderSky;
const API=context.NorthStar.modules.engine.API;
assert.equal(typeof Sky,"function");
assert.strictEqual(API.render.sky,Sky,"Nested API.render.sky must reuse canonical Sky module");
const configured=Sky.configure({
    enabled:true,timeOfDay:17.5,cloudCoverage:.66,
    clouds:true,stars:true,latitudeDegrees:52,dayOfYear:120
});
assert.equal(configured.enabled,true);
assert.equal(configured.timeOfDay,17.5);
assert.equal(configured.cloudCoverage,.66);
assert.ok(Object.isFrozen(configured));
assert.ok(Object.isFrozen(configured.sunDirection));
assert.equal(records[0][0],"configure");
Sky.setTimeOfDay(6,30);
assert.equal(Sky.timeOfDay,6.5);
API.render.sky.setCloudCoverage(.8);
assert.equal(Sky.state().cloudCoverage,.8);
API.render.sky.setClouds({enabled:true,coverage:.65,speed:.02});
assert.equal(Sky.state().cloudCoverage,.65);
assert.equal(Sky.state().cloudSpeed,.02);
API.render.sky.setClouds(.23);
assert.equal(Sky.state().cloudCoverage,.23);
API.render.sky.setSun({intensity:1.5});
assert.equal(Sky.state().sunIntensity,1.5);
API.render.sky.setMoon({phase:.4,intensity:.7});
assert.equal(Sky.state().moonPhase,.4);
assert.equal(Sky.state().moonIntensity,.7);
API.render.sky.setStars({density:.68});
assert.equal(Sky.state().starDensity,.68);
Sky.pause();
Sky.advance(.5);
assert.equal(Sky.timeOfDay,6.5);
Sky.pause(false);
Sky.advance(.5);
assert.ok(Sky.timeOfDay>6.5);
assert.throws(()=>Sky.configure({cloudCoverage:1.5}),{name:"RangeError"});
assert.throws(()=>Sky.configure({timeOfDay:24}),{name:"RangeError"});
assert.throws(()=>Sky.configure({invalidSetting:1}),{name:"RangeError"});
assert.throws(()=>API.render.sky.setClouds({wrong:1}),{name:"RangeError"});
assert.throws(()=>API.render.sky.setClouds({coverage:5}),{name:"RangeError"});
assert.throws(()=>API.render.sky.setSun({intensity:NaN}),{name:"RangeError"});
assert.throws(()=>API.render.sky.setMoon({phase:-1}),{name:"RangeError"});
assert.throws(()=>API.render.sky.setStars({density:3}),{name:"RangeError"});
assert.throws(()=>Sky.setTimeOfDay(24),{name:"RangeError"});
const skySource=Sky.configure({mode:"hybrid",drawable:"worlds/night.ydd@clouds"});
assert.equal(skySource.mode,"hybrid");
assert.equal(skySource.drawable,"worlds/night.ydd@clouds");
assert.throws(()=>Sky.configure({mode:"invalid"}),{name:"RangeError"});
assert.throws(()=>Sky.configure({drawable:123}),{name:"TypeError"});
assert.throws(()=>Sky.configure({preset:123}),{name:"TypeError"});
Sky.reset();
assert.equal(Sky.state().enabled,false);
console.log("PASS engine.render.sky JS: registry, native bridge, clock, cloud, pause, immutable state, validation");
