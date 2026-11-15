// NorthStarCpp default player camera script (class-based).
// VFS path: scripts/camera/playerCamera.js
// Projects override scripts/camera/playerCamera.js (legacy scripts/playerCamera.js also works).
(() => {
    "use strict";

    class PlayerCameraScript {
        #view;
        #active = false;
        #installed = false;

        constructor(options = {}) {
            if (typeof NorthStar !== "object" ||
                typeof NorthStar.camera.createViewController !== "function")
                throw new Error("playerCamera.js requires scripts/northstar/bootstrap.js");

            // Preserve existing default camera controls; gameplay can replace
            // this entire class through the per-project VFS overlay.
            const authored = NorthStar.player.config.camera || {};
            this.#view = NorthStar.camera.createViewController(Object.assign({
                initialMode: "third_person",
                toggleKey: 86,                // V
                transitionSeconds: 0.45,
                minDistance: 0.5,             // metres
                maxDistance: 10.0,
                wheelStep: 0.7                // metres per wheel notch
            }, authored, options));
            Object.freeze(this);
        }

        start() {
            // Non-camera scenes can run shared project scripts safely.
            this.#active = engine.camera.available();
            if (this.#active) this.#view.start();
        }

        update() {
            if (this.#active && !NorthStar.player.paused) this.#view.update();
        }

        stop() {
            this.#active = false;
            this.#view.stop();
        }

        install() {
            if (this.#installed)
                throw new Error("PlayerCameraScript already installed");
            this.#installed = true;
            engine.on("start", () => this.start());
            engine.on("update", () => this.update());
            engine.on("stop", () => this.stop());
            return this;
        }
    }

    NorthStar.camera.installDefaultScript(PlayerCameraScript);
})();
