// NorthStar FP Protocol — input is project-owned, motion is Jolt-owned.
import { Factory, Script, Player, Input, Camera, Keys, GamepadButtons } from "@northstar/engine";

class FirstPersonMovement extends Script {
    #wasJump = false;
    start() { this.#wasJump = false; }
    update() {
        if (!Player.enabled || Player.index < 0) return;
        if (Player.paused) {
            Player.move(0, 0);
            Player.sprint(false);
            return;
        }
        const pad = Input.firstGamepad();
        let strafe = Number(Input.isDown(Keys.D)) - Number(Input.isDown(Keys.A))
            + (pad ? pad.leftX + Number(Input.hasButton(pad, GamepadButtons.DPadRight))
                          - Number(Input.hasButton(pad, GamepadButtons.DPadLeft)) : 0);
        let forward = Number(Input.isDown(Keys.W)) - Number(Input.isDown(Keys.S))
            + (pad ? pad.leftY + Number(Input.hasButton(pad, GamepadButtons.DPadUp))
                          - Number(Input.hasButton(pad, GamepadButtons.DPadDown)) : 0);
        const length = Math.hypot(strafe, forward);
        if (length > 1) { strafe /= length; forward /= length; }
        const yaw = Camera.angles().yaw;
        Player.move(strafe * Math.cos(yaw) + forward * Math.sin(yaw),
                    forward * Math.cos(yaw) - strafe * Math.sin(yaw));

        const moving = Math.hypot(strafe, forward) > 0.12;
        const sprintKey = Player.config.movement?.sprintKey ?? Keys.Shift;
        Player.sprint(moving && Input.isDown(sprintKey));
        const jump = Input.isDown(Player.config.movement?.jumpKey ?? Keys.Space)
            || Input.hasButton(pad, GamepadButtons.Jump);
        if (jump && !this.#wasJump) Player.jump();
        this.#wasJump = jump;
    }
    stop() { this.#wasJump = false; }
}
Factory.script(FirstPersonMovement);
