// Camera-relative input movement; no player-model or animation knowledge.
import { Script, Player, Input, Camera, Keys, GamepadButtons } from "@northstar/engine";

class PlayerMovementScript extends Script {
    #wasJump = false;

    start() { this.#wasJump = false; }

    update() {
        if (!Player.enabled) return;
        if (Player.paused) {
            Player.move(0, 0);
            return;
        }
        const gamepad = Input.firstGamepad();
        let strafe = Number(Input.isDown(Keys.D)) - Number(Input.isDown(Keys.A)) +
            (gamepad ? gamepad.leftX +
                Number(Input.hasButton(gamepad, GamepadButtons.DPadRight)) -
                Number(Input.hasButton(gamepad, GamepadButtons.DPadLeft)) : 0);
        let forward = Number(Input.isDown(Keys.W)) - Number(Input.isDown(Keys.S)) +
            (gamepad ? gamepad.leftY +
                Number(Input.hasButton(gamepad, GamepadButtons.DPadUp)) -
                Number(Input.hasButton(gamepad, GamepadButtons.DPadDown)) : 0);
        const length = Math.hypot(strafe, forward);
        if (length > 1) { strafe /= length; forward /= length; }
        const yaw = Camera.angles().yaw;
        Player.move(
            strafe * Math.cos(yaw) + forward * Math.sin(yaw),
            forward * Math.cos(yaw) - strafe * Math.sin(yaw));

        const jump = Input.isDown(Player.config.movement?.jumpKey ?? Keys.Space) ||
            Input.hasButton(gamepad, GamepadButtons.Jump);
        if (jump && !this.#wasJump) Player.jump();
        this.#wasJump = jump;
    }

    stop() { this.#wasJump = false; }
}

new PlayerMovementScript().install();
