// Optional interaction action with authorable range, radius and impulse.
import { Script, Player, Input, Camera, Keys, GamepadButtons, Scene, Physics } from "@northstar/engine";

class PlayerInteractionScript extends Script {
    #wasUse = false;

    start() { this.#wasUse = false; }

    update() {
        if (!Player.enabled || Player.paused ||
            Player.config.interaction?.enabled !== true) return;
        const options = Player.config.interaction;
        const gamepad = Input.firstGamepad();
        const use = Input.isDown(options.useKey ?? Keys.E) ||
            Input.hasButton(gamepad, GamepadButtons.Interact);
        if (use && !this.#wasUse) {
            const eye = Player.eye();
            const {yaw, pitch} = Camera.angles();
            const dx = Math.sin(yaw) * Math.cos(pitch);
            const dy = Math.sin(pitch);
            const dz = Math.cos(yaw) * Math.cos(pitch);
            const range = options.range ?? 3.6;
            const radius = options.radius ?? 0.28;
            const impulse = options.impulse ?? 9;
            if (![range, radius, impulse].every(Number.isFinite) ||
                range <= 0 || radius <= 0 || impulse < 0)
                throw new RangeError("Invalid player interaction distances/impulse");
            const targetPosition = {
                x: eye.x + dx * range,
                y: eye.y + dy * range,
                z: eye.z + dz * range
            };
            const hit = Physics.sweepSphere(eye, targetPosition, radius, Player.index);
            if (hit) {
                const target = Scene.get(hit.index);
                if (target && target.mass > 0 && !target.player) {
                    Physics.applyImpulse(hit.index, {
                        x: dx * impulse,
                        y: dy * impulse + 1.2,
                        z: dz * impulse
                    });
                }
            }
        }
        this.#wasUse = use;
    }

    stop() { this.#wasUse = false; }
}

new PlayerInteractionScript().install();
