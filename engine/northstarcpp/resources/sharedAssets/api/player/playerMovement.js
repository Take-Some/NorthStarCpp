// Camera-relative movement. Input binding remains script-owned, and
// physical character motion stays inside the selected physics provider.
import { PlayerScript, Player, Input, Camera, Keys, GamepadButtons } from "@northstar/engine";

class PlayerMovementScript extends PlayerScript {
    #jump = null;
    #sprintKey = Keys.Shift;
    #walkKey = Keys.Control;
    // Stance-foot analysis of the source Abby YCD: walk=1.48, run=2.95 m/s.
    #walkScale = 1.48 / 2.95;

    start() {
        const config = this.options?.movement || {};
        this.#jump = Input.createAction(config.jumpKey ?? Keys.Space,
                                        GamepadButtons.Jump);
        this.#sprintKey = config.sprintKey ?? Keys.Shift;
        this.#walkKey = config.walkKey ?? Keys.Control;
        this.#walkScale = config.walkScale ?? 1.48 / 2.95;
        if (!Number.isFinite(this.#walkScale) ||
            this.#walkScale <= 0 || this.#walkScale > 1)
            throw new RangeError("movement.walkScale must be in (0, 1]");
    }

    update() {
        if (!this.active) return;
        const pad = Input.firstGamepad();
        if (Player.paused) {
            Player.move(0, 0);
            Player.sprint(false);
            this.#jump?.pressed(pad); // Consume held input across pause.
            return;
        }
        const {x:strafe, y:forward} = Input.normalized2D(
            Input.axis(Keys.D, Keys.A, pad?.leftX ?? 0,
                GamepadButtons.DPadRight, GamepadButtons.DPadLeft, pad),
            Input.axis(Keys.W, Keys.S, pad?.leftY ?? 0,
                GamepadButtons.DPadUp, GamepadButtons.DPadDown, pad));
        const yaw = Camera.angles().yaw;
        const moving = Math.hypot(strafe, forward) > 0.12;
        const wantsWalk = Input.isDown(this.#walkKey);
        const sprinting = moving && !wantsWalk && Input.isDown(this.#sprintKey);
        // Ctrl = source-measured 1.48 m/s walk; ordinary WASD = 2.95 m/s run;
        // Shift = 4.44 m/s sprint. Analog stick magnitude remains proportional.
        const scale = wantsWalk ? this.#walkScale : 1;
        Player.move(
            (strafe * Math.cos(yaw) + forward * Math.sin(yaw)) * scale,
            (forward * Math.cos(yaw) - strafe * Math.sin(yaw)) * scale);
        Player.sprint(sprinting);
        if (this.#jump?.pressed(pad)) Player.jump();
    }

    stop() {
        this.#jump?.reset();
        this.#jump = null;
    }
}

new PlayerMovementScript().install();
