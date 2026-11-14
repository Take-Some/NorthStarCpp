# Project Save Games — engine.save

Copyright (C) 2026 Kayla's Systems — Kayla Verner.

## Scope

NorthStarCpp contains a native C++ gameplay-state persistence service:

- Public SDK: include/northstarcpp/save_game.hpp, northstarcpp::SaveGameStore.
- Runtime: engine/northstarcpp/modules/host/save_game.cpp.
- JavaScript Player methods: engine.save.write/read/exists/remove.
- Registered test: northstarcpp_save_game (tests/save_game_smoke.cpp).

This first iteration persists project-defined JSON game state, not the entire
ECS world, physics simulation, AI, animation, streamed sectors or entity scripts.
Projects own their versioning and restoration logic.

## JavaScript example

~~~js
const state = {
    schema: 1, level: "industrial", health: 75,
    inventory: ["keycard", "ammo"],
    checkpoint: { x: 12, y: 1, z: 9 }
};
engine.save.write("manual_1", JSON.stringify(state)); // true
const saved = engine.save.read("manual_1");             // string or null
if (saved !== null) {
    const data = JSON.parse(saved);
    if (data.schema === 1) {
        // Restore the game's variables and ECS/physics state via engine APIs.
    }
}
engine.save.exists("manual_1"); // boolean
engine.save.remove("manual_1"); // boolean
~~~

A slot is 1-48 ASCII letters/digits, hyphens or underscores; its first
character must be alphanumeric. Paths and traversal are rejected.
The JSON payload is at most 256 KiB and must be a valid object or array.
Pass a JSON *string*, not a JavaScript object, to write.

User saves are placed under:
%LOCALAPPDATA%\Kaylas Systems\NorthStarCpp\Saves\<project-name-hash>\

The namespace uses the development project manifest's name or the exported
game manifest's game_name so normally both share the same save directory.
Renaming a game changes the namespace. A future stable project UUID and
save migration facility are required for production upgrades.

The file format stores a versioned signature, byte length, FNV-1a 64 hash and
unaltered JSON bytes. Write uses a unique same-directory temporary file
followed by atomic replacement on Windows; corrupt/truncated files fail
validation rather than being silently loaded. This checksum detects accidental
corruption; it does not provide authentication or encryption.

Script methods throw on invalid arguments or disk errors through the V8
host-call bridge. A missing slot yields null (read) or false (exists/remove).
Scripts cannot pass native filesystem paths.

## Verification

- GCC C++17 native SaveGameStore + smoke compilation: PASS.
- Native smoke: PASS, including CRUD, invalid slots, oversize, JSON syntax and
  corruption handling.
- Player C++ source GCC syntax check: PASS.
- Fresh Release CMake configuration and northstarcpp_save_game_smoke CMake
  target build (50 steps): PASS.
- Registered CTest: northstarcpp_save_game 1/1 PASS (0.03 sec).
- Existing Node project gameplay smoke: PASS.
- Fully linked Player, real in-game V8 load/save and exported game:
  NOT YET VERIFIED.

### Next stage

Stable game UUID, save migrations, ECS/physics/animation snapshot and restore,
checkpoint UI, async save queue and packaged-game end-to-end verification.
