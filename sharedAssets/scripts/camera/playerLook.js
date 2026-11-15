// Independent mouse / gamepad view control with per-instance state.
import { Script, Player, Input, Camera } from "@northstar/engine";

class PlayerLookScript extends Script {
    #yaw = 0;
    #pitch = 0;

    start() {
        if (!Player.enabled) return;
        const initial = Camera.angles();
        this.#yaw = initial.yaw;
        this.#pitch = initial.pitch;
    }

    update(dt) {
        if (!Player.enabled || Player.paused) return;
        const options = Player.config.look || {};
        const pointer = Input.pointerDelta();
        const pad = Input.firstGamepad();
        const sensitivity = options.mouseSensitivity ?? 0.0024;
        const maxPitch = options.maxPitch ?? 1.4;
        if (!Number.isFinite(sensitivity) || sensitivity <= 0 ||
            sensitivity > 0.1 || !Number.isFinite(maxPitch) ||
            maxPitch <= 0 || maxPitch > 1.55)
            throw new RangeError("Invalid player look configuration");
        this.#yaw += pointer.x * sensitivity +
            (pad ? pad.rightX * (options.gamepadYawSpeed ?? 2.5) * dt : 0);
        this.#pitch = Math.max(-maxPitch, Math.min(maxPitch,
            this.#pitch - pointer.y * sensitivity +
            (pad ? pad.rightY * (options.gamepadPitchSpeed ?? 2.1) * dt : 0)));
        if (Math.abs(this.#yaw) > Math.PI) this.#yaw %= 2 * Math.PI;
        Camera.setAngles(this.#yaw, this.#pitch);
        if (options.aimOnRightMouse !== false)
            Camera.setZoom(Input.mouseDown(1) ? 1 : 0);
    }
}

new PlayerLookScript().install();
