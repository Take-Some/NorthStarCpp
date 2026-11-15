// Optional project gameplay example; paste into scripts/main.js to activate.
import { Events, Prefab } from "@northstar/engine";

class PistolSpawner {
    #instance = null;
    #position;

    constructor(position) {
        this.#position = { ...position };
        Events.on("start", () => this.spawn());
        Events.on("stop", () => this.cleanup());
    }

    spawn() {
        if (this.#instance?.alive) return this.#instance.root;
        this.#instance = Prefab.instantiate("prefabs/pistol.nprefab", {
            position: this.#position
        });
        return this.#instance.root;
    }

    cleanup() {
        if (!this.#instance) return;
        this.#instance.destroy();
        this.#instance = null;
    }
}

new PistolSpawner({x: 2, y: 0.75, z: 2});
