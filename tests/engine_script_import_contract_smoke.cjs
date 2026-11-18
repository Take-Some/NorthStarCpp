"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const root=path.resolve(__dirname,"..");
const api=path.join(root,"engine/northstarcpp/resources/sharedAssets/api");
const resolver=fs.readFileSync(path.join(root,
    "engine/northstarcpp/modules/host/scripting.cpp"),"utf8");
const low=resolver.slice(resolver.indexOf("std::string lower_engine_imports"),
    resolver.indexOf("struct ScriptRuntime::State"));
assert.ok(!low.includes("static constexpr std::string_view allowed[]"),
    "C++ must not hardcode a duplicate export allowlist");
assert.match(low,/in globalThis[.]NorthStar[.]modules[.]engine/,
    "Native imports must resolve against the actual SDK registry");
const ts=fs.readFileSync(path.join(api,"types/northstar-engine.d.ts"),"utf8");
const sdk=fs.readFileSync(path.join(api,"core/sdk.js"),"utf8");
const exported=[...sdk.matchAll(/^\s*get\s+([A-Za-z_]\w*)\(\)/gm)].map(x=>x[1]);
for(const symbol of ["API","EngineAPI","Factory","Collision","Particles",
    "EntityScript","ComponentScript","ECS","Assets","Time","Scheduler",
    "ScriptContext","AudioVoice","AuthoredEntityHandle"]){
    assert.ok(exported.includes(symbol),"SDK registry must expose "+symbol);
    assert.match(ts,new RegExp("export (?:abstract )?(?:class|const) "+symbol+"\\b"),
        "Type declarations must expose "+symbol);
}
console.log("PASS SDK imports: one runtime class registry drives named exports; no stale C++ allowlist");
