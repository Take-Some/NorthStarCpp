"use strict";
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const assert=require("node:assert/strict");

const bundle=fs.readFileSync(path.join(__dirname,"..","engine",
    "northstarcpp","resources","sharedAssets","api","northstar","bootstrap.js"),"utf8");
const source="layout(set=0,binding=0) uniform sampler2D albedo;";
const rootShader="shaders/sky/atmosphere.glsl";
const compiled=[];
const context={engine:{render:{
    provider:{
        engineVersion(){return "0.7.0";},
        name(){return "vulkan";},
        version(){return "1.0";},
        setDetailLevel(level){return level;},
        detailLevel(){return "high";}
    },
    shaders:{
    catalog(){return {
        provider:"vulkan",hotReload:true,builtin:[
            {path:rootShader,language:"glsl",system:"sky"},
            {path:"shaders/sky/sun_optics.glsl",language:"glsl",system:"sky"}
        ]};},
    source(p){if(p!==rootShader)throw Error("Missing shader");return source;},
    queueCompiled(a,b){compiled.push([a,b]);return 1;},
    poll(){return {pending:false,completed:["installed revision 2"]};}
}}}};
vm.runInNewContext(bundle,context,{filename:"northstar-render-sdk.js"});
const registry=context.NorthStar.modules.engine;
const API=registry.API;
assert.equal(API.render,registry.Render);
assert.equal(API.render.shaders,registry.RenderShaders);
const catalog=API.render.shaders.catalog();
assert.equal(catalog.provider,"vulkan");
assert.equal(catalog.hotReload,true);
assert.ok(Object.isFrozen(catalog));
assert.ok(Object.isFrozen(catalog.builtin));
assert.ok(Object.isFrozen(catalog.builtin[0]));
assert.equal(API.render.shaders.source(rootShader),source);
const asset=API.render.shaders.load(rootShader);
assert.equal(asset.path,rootShader);
assert.equal(asset.language,"glsl");
assert.equal(asset.source,source);
assert.ok(Object.isFrozen(asset));
assert.equal(API.render.shaders.queueCompiled("shaders/water.frag",
    "shaders/water.spv"),1);
assert.equal(compiled[0][0],"shaders/water.frag");
assert.equal(compiled[0][1],"shaders/water.spv");
const poll=API.render.shaders.poll();
assert.equal(poll.pending,false);
assert.ok(Object.isFrozen(poll.completed));
assert.throws(()=>API.render.shaders.source("../secret.glsl"),{name:"TypeError"});
assert.throws(()=>API.render.shaders.source("shaders/../secret.frag"),{name:"TypeError"});
assert.throws(()=>API.render.shaders.source("textures/sky.frag"),{name:"TypeError"});
assert.throws(()=>API.render.shaders.source("shaders/file.txt"),{name:"TypeError"});
assert.throws(()=>API.render.shaders.queueCompiled("shaders/hi.hlsl",
    "shaders/hi.spv"),{name:"TypeError"});
assert.throws(()=>API.render.shaders.queueCompiled("shaders/a.frag",
    "shaders/a.dxil"),{name:"TypeError"});
assert.throws(()=>API.render.shaders.queueCompiled("shaders/a.frag",
    "shaders/../evil.spv"),{name:"TypeError"});
assert.equal(typeof API.render.setDetailLevel,"function");
assert.equal(API.render.provider.name(),"vulkan");
assert.equal(API.render.provider.version(),"0.7.0");
assert.equal(API.render.shadows,registry.RenderShadows);
console.log("PASS engine.render.shaders: catalog, source, load, provider, compiled reload, poll, VFS guards");
