// Project-independent event schemas, registered once per scripting context.
// Native engine.on/off/emit remains the only event dispatcher.
(() => {
    "use strict";
    const {Events} = NorthStar.modules.engine;

    class DefaultEventContracts {
        static #surfaces = new Set([
            "dirt", "grass", "metal", "mud",
            "snow", "stone", "water", "wood"
        ]);
        static #gaits = new Set(["walk", "run", "sprint"]);

        static isFootstep(data) {
            return Boolean(data && typeof data === "object" &&
                Number.isSafeInteger(data.actor) && data.actor >= 0 &&
                Number.isSafeInteger(data.sequence) && data.sequence > 0 &&
                this.#surfaces.has(data.surface) &&
                this.#gaits.has(data.gait) &&
                Number.isFinite(data.speed) && data.speed >= 0 &&
                data.position && [data.position.x, data.position.y,
                                  data.position.z].every(Number.isFinite));
        }

        static install() {
            Events.define("player.footstep", event => this.isFootstep(event));
        }
    }

    DefaultEventContracts.install();
})();
