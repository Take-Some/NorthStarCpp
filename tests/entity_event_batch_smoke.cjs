// Verify the actual C++-owned batch bridge without a native V8 dependency.
// Node VM here checks event order and payload, not runtime/IPC performance.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const root = path.resolve(__dirname, "..");
const source = fs.readFileSync(
    path.join(root, "engine/northstarcpp/modules/editor/window.cpp"), "utf8");

// The actual C++ source provides a single persistent JS listener per context.
// Reconstruct the two adjacent C++ string literals, so this test cannot pass
// with a hardcoded duplicate bridge that differs from the production source.
const signature = /runtime\.evaluate\(\s*("(?:void )?engine\.on\('northstar\._entityBatch',[^\n]*"\s*"[^\n]*"\s*"\}\);void 0;")\s*,\s*"<engine-entity-batch-bridge>"\)/m;
const match = source.match(signature);
assert.ok(match, "Persistent C++ entity batch bridge is absent");
assert.ok(source.includes('runtime.emit_event("northstar._entityBatch"'),
    "C++ does not submit entity batches through the script event ABI");
const bridge = [...match[1].matchAll(/"(?:\\.|[^"\\])*"/g)]
    .map(m => JSON.parse(m[0])).join("");

function runtime() {
    const subscribers = new Map();
    const events = [];
    const engine = {
        on(event, callback) {
            const list = subscribers.get(event) ?? [];
            list.push(callback);
            subscribers.set(event, list);
        },
        emit(event, payload) {
            events.push({event, payload});
            for (const listener of subscribers.get(event) ?? [])
                listener(payload);
        }
    };
    vm.runInNewContext(bridge, {engine}, {timeout: 2000});
    return {engine, events};
}
const original = runtime();
const replacement = runtime();
const items = Array.from({length: 513}, (_, index) => ({
    id: String(10000 + index), index, script: "scripts/test.js", deltaTime: 0.016
}));
for (const block of [items.slice(0, 256), items.slice(256, 512), items.slice(512)])
    original.engine.emit("northstar._entityBatch", {event:"entity.update", items:block});
const delivered = original.events.filter(e => e.event === "entity.update");
assert.equal(delivered.length, 513, "Some entity update events were dropped");
assert.deepEqual(delivered.map(e => e.payload.index), items.map(e => e.index),
    "Batch delivery changed scene order");
assert.deepEqual(JSON.parse(JSON.stringify(delivered[256].payload)), items[256],
    "Batch delivery changed payload data");
replacement.engine.emit("northstar._entityBatch", {
    event:"entity.stop", items:[{id:"42",index:0,script:"scripts/stop.js"}]
});
assert.equal(replacement.events.filter(e=>e.event==="entity.stop").length, 1);
assert.equal(original.events.filter(e=>e.event==="entity.stop").length, 0,
    "Fresh script runtime leaked events to previous context");
console.log("V8 entity batch bridge: PASS (513 ordered entities, fresh context)");
