"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const source=fs.readFileSync(path.join(__dirname,
  "../engine/northstarcpp/resources/sharedAssets/api/camera/playerLook.js"),"utf8")
  .replace(/import \{[^\n]*\} from "@northstar\/engine";\s*/,"");
let ads=false,mode="first_person",yaw=0,pitch=0;
const context={
  PlayerScript:class {get enabled(){return true;} get playing(){return true;}
    install(){return this;}},
  Player:{config:{look:{mouseSensitivity:.0024,
    adsSensitivityMultiplier:.52,maxPitch:1.4}}},
  Camera:{angles:()=>({yaw,pitch}),setAngles:(y,p)=>{yaw=y;pitch=p;},
    mode:()=>mode,setZoom:()=>{}},
  Input:{pointerDelta:()=>({x:10,y:-5}),
    firstGamepad:()=>null,mouseDown:()=>ads}
};
vm.runInNewContext(source+"\nthis.testLook=new PlayerLookScript();",context);
context.testLook.start();
context.testLook.update(.016);
assert.ok(Math.abs(yaw-.024)<1e-9);
assert.ok(Math.abs(pitch-.012)<1e-9);
ads=true;
context.testLook.update(.016);
assert.ok(Math.abs(yaw-(.024+.024*.52))<1e-9);
assert.ok(Math.abs(pitch-(.012+.012*.52))<1e-9);
mode="third_person";
context.testLook.update(.016);
assert.ok(Math.abs(yaw-(.024+.024*.52+.024))<1e-9);
assert.ok(Math.abs(pitch-(.012+.012*.52+.012))<1e-9);
console.log("PASS player look: precise ADS sensitivity, normal TPS input");
