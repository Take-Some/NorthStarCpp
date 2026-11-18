// Optional project gameplay example. Include this script as a root asset
// or copy it into main.js; the native resolver currently imports only
// @northstar/engine. Core Script owns lifecycle and cleanup.
import { Factory, Script } from "@northstar/engine";

class PistolSpawner extends Script {
    /** @type {import("@northstar/engine").PrefabInstance | null} */
    #instance = null;
    #position;

    /** @param {{position:import("@northstar/engine").Vector3}} options */
    constructor({position}) {
        super();
        this.#position = {...position};
    }

    start() {
        this.#instance = this.own(Factory.prefab(
            "prefabs/weapon/pistol.nprefab",
            {position:this.#position}
        ));
    }

    get weapon() {
        return this.#instance?.alive ? this.#instance.root : null;
    }

    onDestroy() {
        this.#instance = null; // ScriptContext owns destruction.
    }
}

Factory.script(PistolSpawner, {
    position:{x:2,y:0.75,z:2}
});
