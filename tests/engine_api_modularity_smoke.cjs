"use strict";
// Regression gate: authoritative module boundaries must match the shipped V8
// bootstrap. Old compatibility fragments must never be bundled twice.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const api = path.resolve(__dirname,
    "../engine/northstarcpp/resources/sharedAssets/api");
const dir = api;
const manifest = JSON.parse(fs.readFileSync(path.join(dir, "manifest.json"), "utf8"));
assert.equal(manifest.schemaVersion, 1);
assert(Array.isArray(manifest.modules) && manifest.modules.length > 0,
    "An ordered, nonempty SDK manifest is required");

const names = manifest.modules;
assert.equal(new Set(names).size, names.length, "Duplicate SDK module in manifest");
const MAX_MODULE_BYTES = 32 * 1024;
const declarations = new Map();
const buffers = [];
for (const name of names) {
    assert.match(name, /^[a-z][a-z_]*[/][a-z][a-z_]*[.]js$/,
        "Only grouped, named OOP class modules are allowed");
    const fullPath = path.join(dir, name);
    const data = fs.readFileSync(fullPath);
    assert(data.length > 0 && data.length <= MAX_MODULE_BYTES,
        name + " exceeds the engine SDK module size budget");
    const text = data.toString("utf8");
    buffers.push(data);
    for (const match of text.matchAll(/^    class\s+([A-Za-z_]\w*)\s*\{/gm)) {
        const previous = declarations.get(match[1]);
        assert(!previous, "Duplicate SDK class " + match[1] +
            " in " + previous + " and " + name);
        declarations.set(match[1], name);
    }
    // JS silently lets a later method shadow an earlier declaration.
    // Flag accidental duplicate methods in each engine-owned class.
    for (const block of text.split(/(?=^    class\s+[A-Za-z_]\w*\b)/gm)) {
        const owner = block.match(/^    class\s+([A-Za-z_]\w*)\b/);
        if (!owner) continue;
        const methods = new Set();
        for (const line of block.split("\n")) {
            const method = line.match(/^        (static\s+)?(?:(get|set|async)\s+)?(#[A-Za-z_]\w*|[A-Za-z_]\w*)\s*\(/);
            if (!method) continue;
            const key = (method[1] ? "static:" : "instance:") +
                (method[2] === "get" || method[2] === "set" ? method[2] + ":" : "") +
                method[3];
            assert(!methods.has(key),
                "Duplicate method " + owner[1] + "." + key + " in " + name);
            methods.add(key);
        }
    }
}
const composed = Buffer.concat(buffers);
const bootstrap = fs.readFileSync(path.join(api, "northstar", "bootstrap.js"));
assert(composed.equals(bootstrap),
    "Stale SDK bootstrap: run tools/dev/compose_northstar_bootstrap.py");
assert(bootstrap.toString("utf8").startsWith("// NorthStarCpp"),
    "Invalid SDK bootstrap prologue");
new vm.Script(bootstrap.toString("utf8"), {filename:"northstar/bootstrap.js"});

for (const name of ["EngineAPI", "Factory", "Scene", "Camera", "Vehicle", "Physics",
                    "WeaponBallistics", "WeaponEffects", "Particles", "Session"]) {
    assert(declarations.has(name), "Missing canonical SDK class: " + name);
}
console.log("PASS engine SDK modularity: " + names.length +
    " fragments, one bootstrap, unique class owners, <=32 KiB/module");
