"use strict";
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const assert = require("node:assert/strict");

const ROOT = path.resolve(__dirname, "..");
const assets = path.join(ROOT, "engine/northstarcpp/resources/sharedAssets");
const prefabPath = path.join(assets, "prefabs/characters/ellie.nprefab");
const manifestPath = path.join(assets, "characters/ellie/ellie.character.json");
const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
const prefab = fs.readFileSync(prefabPath, "utf8");

assert.equal(manifest.schema, "northstar.character.v1");
assert.equal(manifest.id, "characters/ellie");
assert.equal(manifest.model, "characters/ellie/models/ellie.ydd");
assert.equal(manifest.skeleton, "characters/ellie/models/ellie.ymt");
assert.equal(manifest.appearance.material, "characters/ellie/materials/ellie.ymat");
assert.equal(manifest.appearance.textures, "characters/ellie/textures/ellie.ytd");
assert(Array.isArray(manifest.animations));
assert.match(prefab, /^NorthStarCpp\.Scene 3\r?\n/);
const entities = [...prefab.matchAll(/^entity (\d+) (\d+) "([^"]*)" "([^"]*)" "" "" "([^"]*)"/gm)];
assert.equal(entities.length, 1);
assert.equal(entities[0][1], "1");
assert.equal(entities[0][2], "0");
assert.equal(entities[0][3], "Ellie");
assert.equal(entities[0][5], manifest.model);
assert.doesNotMatch(prefab, /northstar\.gameplay\.pawn|player_controller|camera_rig|pistol\.ydd/);
const components = [...prefab.matchAll(/^component 1 "northstar\.animation\.default" 1 "([a-f0-9]+)"$/gm)];
assert(components.length <= 1);
if (components.length) {
  const clip = Buffer.from(components[0][1], "hex").toString("utf8");
  assert(manifest.animations.some(a => a.state === "idle" && a.source + "@" + a.clip === clip),
      "Prefab YCD must be backed by the character manifest");
} else {
  assert(!manifest.animations.some(a => a.state === "idle"),
      "An idle animation has been declared without a default component");
}
const modelReady = [
  manifest.model, manifest.skeleton, manifest.appearance.material,
  manifest.appearance.textures
].every(p => fs.existsSync(path.join(assets, p)));
const forced = process.argv.includes("--require-ready");
if(forced) assert(modelReady, "Ellie source assets must be converted before runtime acceptance");

// Exercise the genuine generic prefab JavaScript API with the on-disk descriptor.
// This validates composition, but not native mesh decoding or GPU rendering.
const script = fs.readFileSync(path.join(assets, "api/scene/Prefab.js"), "utf8");
const spawned = [], played = [], destroyed = [];
let Prefab;
const scene = {
  spawn(config) {
    spawned.push(config);
    const index = spawned.length;
    return {index, alive: true, destroy() {
      destroyed.push(index);
      this.alive = false; return true;
    }};
  },
  playAnimation(index, ref) { played.push({index, ref}); }
};
const runtime = {
  engine: {prefab: {read(ref) {
    assert.equal(ref, "prefabs/characters/ellie.nprefab");
    const fields = entities[0];
    return {
      objects: [{id: fields[1], parent:fields[2], name:fields[3],
        layer:fields[4], asset:fields[5],
        position:{x:0,y:0,z:0}, size:1, color:0x67a0c0,mass:0,collision:false}],
      animations: components.map(c => ({
        entity:"1", clip:Buffer.from(c[1],"hex").toString("utf8")
      }))
    };
  }}},
  NorthStar: {
    modules: {engine: {Scene: scene}},
    sceneTypes: {registerPrefab(ctor) {Prefab = ctor;}}
  }
};
vm.runInNewContext(script, runtime, {filename:"Prefab.js"});
assert.equal(typeof Prefab, "function");
const actor = Prefab.instantiate("prefabs/characters/ellie.nprefab", {
  position:{x:4,y:0,z:9}
});
assert.equal(actor.alive,true);
assert.equal(spawned.length, 1);
assert.equal(spawned[0].asset, manifest.model);
assert.equal(spawned[0].position.x, 4);
assert.equal(played.length, components.length);
actor.destroy();
assert.equal(destroyed.length,1);
console.log("PASS: Ellie manifest, Scene V3 prefab and independent Prefab.instantiate contract; native content "+(modelReady?"present":"PENDING"));
