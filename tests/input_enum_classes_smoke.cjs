// Checks the actual SDK classes and the input provider's public enum values.
// The companion Python CTest verifies generated TypeScript declarations.
"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const base = path.resolve(__dirname, "../engine/northstarcpp/resources/sharedAssets/api");
const context = vm.createContext({});
for (const file of [
    "northstar/bootstrap.js",
    "input/Keys.js",
    "input/GamepadButtons.js"
]) {
    const source = fs.readFileSync(path.join(base, file), "utf8");
    vm.runInContext(source, context, {filename: file, timeout: 2000});
}

const sdk = vm.runInContext("NorthStar", context);
const {Keys, GamepadButtons, Input} = sdk.modules.engine;
assert.equal(Keys, Input.Keys);
assert.equal(GamepadButtons, Input.GamepadButtons);

const expected = {
    A: 0x41, Z: 0x5A, Digit0: 0x30, Digit9: 0x39,
    F1: 0x70, F12: 0x7B, F24: 0x87, Space: 0x20,
    Escape: 0x1B, ArrowLeft: 0x25, ArrowUp: 0x26,
    LeftShift: 0xA0, RightShift: 0xA1,
    LeftControl: 0xA2, RightControl: 0xA3,
    LeftAlt: 0xA4, RightAlt: 0xA5
};
for (const [name, value] of Object.entries(expected))
    assert.equal(Keys[name], value, name);

assert.ok(Object.isFrozen(Keys));
assert.ok(Object.isFrozen(GamepadButtons));
assert.ok(Object.keys(Keys).length > 190, "The public Keys class must be complete");
assert.equal(Object.keys(GamepadButtons).length, 16);
for (const [name, value] of Object.entries(Keys)) {
    assert.ok(Number.isInteger(value) && value > 0 && value < 256, name);
}
assert.equal(Keys.GamepadA, undefined, "Keyboard Keys must not expose gamepad VKs");
assert.equal(Keys.MouseLeft, undefined, "Mouse button indices have a separate API");

const buttons = {
    DPadUp: 0x0001, DPadDown: 0x0002, DPadLeft: 0x0004,
    DPadRight: 0x0008, Start: 0x0010, Back: 0x0020,
    LeftThumb: 0x0040, RightThumb: 0x0080,
    LeftShoulder: 0x0100, RightShoulder: 0x0200,
    A: 0x1000, B: 0x2000, X: 0x4000, Y: 0x8000,
    Jump: 0x1000, Interact: 0x4000
};
for (const [name, value] of Object.entries(buttons))
    assert.equal(GamepadButtons[name], value, name);
assert.throws(() => vm.runInContext(
    "NorthStar.inputTypes.registerKeys(class OtherKeys {})", context),
    /already registered/);

console.log("PASS: independent, immutable keyboard and gamepad enum classes");
