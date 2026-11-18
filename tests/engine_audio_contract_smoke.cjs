"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const api=path.resolve(__dirname,"../engine/northstarcpp/resources/sharedAssets/api");
const calls=[];
const engine={
  audio:{
    play:asset=>{calls.push({kind:"wav",asset});return 3;},
    playNac:(asset,clip,variation,gain)=>{
      calls.push({kind:"nac",asset,clip,variation,gain});return 7;
    }
  }
};
const context=vm.createContext({engine});
vm.runInContext(fs.readFileSync(path.join(api,"northstar/bootstrap.js"),"utf8"),context);
const Audio=vm.runInContext("NorthStar.modules.engine.Audio",context);
assert.equal(Audio.play("audio/ambient/wind.wav"),3);
assert.equal(Audio.play("audio/vehicle/engine.nac",{clip:"idle",variation:2,gain:.6}),7);
assert.equal(Audio.play({asset:"audio/robot/voice.nac",clip:"greeting"}),7);
assert.equal(Audio.playWav("audio/ui/confirm.wav"),3);
assert.equal(Audio.playNac("audio/weapon/machine.nac","fire"),7);
assert.equal(calls.length,5);
assert.equal(calls[0].asset,"audio/ambient/wind.wav");
assert.equal(calls[1].clip,"idle");
assert.equal(calls[1].gain,.6);
assert.throws(()=>Audio.play("audio/vehicle/engine.nac"),/clip/);
assert.throws(()=>Audio.play("audio/ambient/wind.wav",{gain:.5}),/WAV provider/);
assert.throws(()=>Audio.play("audio/other/file.mp3"),/WAV and NAC/);
assert.throws(()=>Audio.play("audio/../system/evil.wav"),/VFS/);
assert.throws(()=>Audio.play({asset:"audio/ui/click.wav",foo:"bar"}),/option/);
assert.equal(calls.length,5,"invalid requests must never reach provider");
console.log("Engine Audio: PASS (generic WAV/NAC, validated options, no gameplay sounds)");
