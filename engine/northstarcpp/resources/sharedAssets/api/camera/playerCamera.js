// Default replaceable camera root. All lifecycle registration is inherited
// from the same Script base used by every other NorthStar root module.
(() => {
    "use strict";
    const {Script} = NorthStar.modules.engine;

    class PlayerCameraScript extends Script {
        #view;
        #active = false;

        constructor(options = {}) {
            super();
            const authored = NorthStar.player.config.camera || {};
            this.#view = NorthStar.camera.createViewController({
                initialMode: engine.camera.views.available() ? engine.camera.views.mode() : "third_person",
                toggleKey: 86,
                transitionSeconds: 0.45,
                minDistance: 0.5,
                maxDistance: 10.0,
                wheelStep: 0.7,
                ...authored,
                ...options
            });
        }

        start() {
            this.#active = engine.camera.views.available();
            if (this.#active) this.#view.start();
        }

        update() {
            if (this.#active && !NorthStar.player.paused)
                this.#view.update();
        }

        stop() {
            this.#active = false;
            this.#view.stop();
        }
    }

    NorthStar.camera.installDefaultScript(PlayerCameraScript);
})();
