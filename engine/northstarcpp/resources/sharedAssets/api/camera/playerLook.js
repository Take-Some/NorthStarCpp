// Project-replaceable camera input root. Tuning is validated once per start.
import { PlayerScript, Input, Player, Camera } from "@northstar/engine";

class PlayerLookScript extends PlayerScript {
    #yaw = 0;
    #pitch = 0;
    #sensitivity = 0.0024;
    #adsSensitivityMultiplier = 1;
    #maxPitch = 1.4;
    #gamepadYawSpeed = 2.5;
    #gamepadPitchSpeed = 2.1;
    #aimOnRightMouse = true;
    #previousZoom = null;

    start() {
        this.#previousZoom = null;
        if (!this.enabled) return;
        const initial = Camera.angles();
        this.#yaw = initial.yaw;
        this.#pitch = initial.pitch;
        const options = Player.config.look || {};
        this.#sensitivity = options.mouseSensitivity ?? 0.0024;
        this.#adsSensitivityMultiplier = options.adsSensitivityMultiplier ?? 1;
        this.#maxPitch = options.maxPitch ?? 1.4;
        this.#gamepadYawSpeed = options.gamepadYawSpeed ?? 2.5;
        this.#gamepadPitchSpeed = options.gamepadPitchSpeed ?? 2.1;
        this.#aimOnRightMouse = options.aimOnRightMouse !== false;
        if (![this.#sensitivity, this.#adsSensitivityMultiplier, this.#maxPitch,
              this.#gamepadYawSpeed, this.#gamepadPitchSpeed].every(Number.isFinite) ||
            this.#sensitivity <= 0 || this.#sensitivity > 0.1 ||
            this.#adsSensitivityMultiplier < 0.1 || this.#adsSensitivityMultiplier > 1 ||
            this.#maxPitch <= 0 || this.#maxPitch > 1.55 ||
            this.#gamepadYawSpeed < 0 || this.#gamepadPitchSpeed < 0)
            throw new RangeError("Invalid player look configuration");
    }

    update(dt) {
        if (!this.playing) return;
        if (!Number.isFinite(dt) || dt < 0 || dt > 0.25) return;
        const pointer = Input.pointerDelta();
        const pad = Input.firstGamepad();
        // Slower look input in first-person ADS lets the player make small,
        // precise sight corrections without changing regular hip movement.
        const ads=Camera.mode()==="first_person" && Input.mouseDown(1);
        const precision=ads?this.#adsSensitivityMultiplier:1;
        this.#yaw += precision*(pointer.x * this.#sensitivity +
            (pad ? pad.rightX * this.#gamepadYawSpeed * dt : 0));
        this.#pitch = Math.max(-this.#maxPitch, Math.min(this.#maxPitch,
            this.#pitch - precision*(pointer.y * this.#sensitivity +
            (pad ? pad.rightY * this.#gamepadPitchSpeed * dt : 0))));
        if (Math.abs(this.#yaw) > Math.PI)
            this.#yaw %= 2 * Math.PI;
        Camera.setAngles(this.#yaw, this.#pitch);
        if (this.#aimOnRightMouse) {
            const zoom = Input.mouseDown(1) ? 1 : 0;
            if (zoom !== this.#previousZoom) {
                Camera.setZoom(zoom);
                this.#previousZoom = zoom;
            }
        }
    }

    stop() { this.#previousZoom = null; }
}

new PlayerLookScript().install();
