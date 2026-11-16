// A project-configurable interaction action. The hit test and impulse
// use the current Physics provider. No legacy entity.player field exists.
import { PlayerScript, Player, Input, Camera, Keys, GamepadButtons, Scene, Physics } from "@northstar/engine";

class PlayerInteractionScript extends PlayerScript {
    #action = null;
    #range = 3.6;
    #radius = 0.28;
    #impulse = 9;
    #enabled = false;

    start() {
        const settings = this.options?.interaction;
        this.#enabled = settings?.enabled === true;
        if (!this.#enabled) return;
        this.#range = settings.range ?? 3.6;
        this.#radius = settings.radius ?? 0.28;
        this.#impulse = settings.impulse ?? 9;
        if (![this.#range, this.#radius, this.#impulse].every(Number.isFinite) ||
            this.#range <= 0 || this.#radius <= 0 || this.#impulse < 0)
            throw new RangeError("Invalid player interaction distances/impulse");
        this.#action = Input.createAction(settings.useKey ?? Keys.E,
                                          GamepadButtons.Interact);
    }

    update() {
        if (!this.#enabled || !this.active) return;
        const pad = Input.firstGamepad();
        if (Player.paused) {
            this.#action.pressed(pad);
            return;
        }
        if (!this.#action.pressed(pad)) return;

        const eye = Player.eye();
        const {yaw, pitch} = Camera.angles();
        const dx = Math.sin(yaw) * Math.cos(pitch);
        const dy = Math.sin(pitch);
        const dz = Math.cos(yaw) * Math.cos(pitch);
        const targetPosition = {
            x: eye.x + dx * this.#range,
            y: eye.y + dy * this.#range,
            z: eye.z + dz * this.#range
        };
        const hit = Physics.sweepSphere(eye, targetPosition,
                                        this.#radius, Player.index);
        if (!hit || hit.index === Player.index || hit.index < 0) return;
        const target = Scene.get(hit.index);
        if (target && target.mass > 0) {
            Physics.applyImpulse(hit.index, {
                x: dx * this.#impulse,
                y: dy * this.#impulse + 1.2,
                z: dz * this.#impulse
            });
        }
    }

    stop() {
        this.#action?.reset();
        this.#action = null;
        this.#enabled = false;
    }
}

new PlayerInteractionScript().install();
