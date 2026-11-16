// Camera-relative movement. Input binding remains script-owned, and
// physical character motion stays inside the selected physics provider.
import { PlayerScript, Player, Input, Camera, Keys, GamepadButtons } from "@northstar/engine";

class PlayerMovementScript extends PlayerScript {
    #jump = null;
    #sprintKey = Keys.Shift;

    start() {
        const config = this.options?.movement || {};
        this.#jump = Input.createAction(config.jumpKey ?? Keys.Space,
                                        GamepadButtons.Jump);
        this.#sprintKey = config.sprintKey ?? Keys.Shift;
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
        Player.move(
            strafe * Math.cos(yaw) + forward * Math.sin(yaw),
            forward * Math.cos(yaw) - strafe * Math.sin(yaw));
        Player.sprint(Math.hypot(strafe, forward) > 0.12 &&
                      Input.isDown(this.#sprintKey));
        if (this.#jump?.pressed(pad)) Player.jump();
    }

    stop() {
        this.#jump?.reset();
        this.#jump = null;
    }
}

new PlayerMovementScript().install();
