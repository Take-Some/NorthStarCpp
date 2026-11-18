"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const base=path.resolve(__dirname,"../engine/northstarcpp/resources/sharedAssets/api");
const modules=base;
const descriptor=JSON.parse(fs.readFileSync(path.join(base,"manifest.json"),"utf8"));
assert.equal(descriptor.schemaVersion,1);
const names=descriptor.modules;
assert.ok(names.length>=18,"Expected decomposed engine services");
assert.equal(new Set(names).size,names.length);
for(const name of names){
  assert.match(name,/^[a-z][a-z_]*[/][a-z][a-z_]*[.]js$/,
    "Each OOP source is in a named domain with no numeric prefixes");
  assert.ok(fs.statSync(path.join(modules,name)).isFile());
}
assert.ok(!fs.existsSync(path.join(base,"northstar","modules")),
          "Obsolete flat module directory must be removed");
for(const category of ["core","audio","camera","character","scene",
    "input","events","physics","weapon","vehicle","ui","session",
    "player","particles","logging"])
  assert.ok(fs.statSync(path.join(base,category)).isDirectory(),
    "Missing grouped SDK category: "+category);
const bootstrap=fs.readFileSync(path.join(base,"northstar","bootstrap.js"));
const composed=Buffer.concat(names.map(name=>fs.readFileSync(path.join(modules,name))));
assert.ok(bootstrap.equals(composed),"Single bootstrap must match manifest exactly");
const allFiles=[];
function visit(folder){
 for(const ent of fs.readdirSync(folder,{withFileTypes:true})){
   const file=path.join(folder,ent.name);
   if(ent.isDirectory())visit(file);else allFiles.push(path.relative(base,file).split(path.sep).join("/"));
 }
}
visit(base);
assert.equal(allFiles.filter(n=>n.includes(".bak-")).length,0,
            "No backup files in active SDK");
assert.ok(!allFiles.includes("audio/footsteps.js"));
assert.ok(!allFiles.includes("player/playerFootsteps.js"));
assert.ok(allFiles.includes("character/footsteps.js"));
assert.ok(allFiles.includes("audio/audio.js"));
assert.ok(!allFiles.includes("northstar/events.js"),
          "Footstep-specific event schema must not be a core fixed script");
const native=fs.readFileSync(path.resolve(__dirname,
 "../engine/northstarcpp/modules/host/engine_script_bootstrap.cpp"),"utf8");
assert.ok(native.includes("scripts/character/footsteps.js"));
assert.ok(!native.includes("scripts/audio/footsteps.js"));
assert.ok(!native.includes("scripts/northstar/events.js"));
console.log("Engine API layout: PASS (semantic files, single manifest, no active duplicates)");
