// NorthStarCpp shared scripting SDK: class-based, per-context services.
// Loads before scripts/character/Character.js, scripts/camera/playerCamera.js and project scripts.
// Only thin engine API contracts are required; gameplay remains project-owned.
(() => {
    "use strict";

    if (Object.prototype.hasOwnProperty.call(globalThis, "NorthStar"))
        throw new Error("NorthStar shared script API already registered");
