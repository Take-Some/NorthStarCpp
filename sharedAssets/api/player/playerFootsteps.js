// Motion -> semantic footstep events. No audio or renderer dependencies.
// Configuration is parsed once; one grounded contact query per moving frame.
import { PlayerScript, Player, Scene, Events } from "@northstar/engine";

class PlayerFootstepController extends PlayerScript {
    #last = null;
    #metres = 0;
    #sequence = 0;
    #tuning = null;

    #reset(position = null) {
        this.#last = position;
        this.#metres = 0;
    }

    start() {
        this.#reset();
        this.#sequence = 0;
        const options = this.options?.footsteps || {};
        const tuning = {
            enabled: options.enabled !== false,
            walkStride: options.walkStride ?? 1.25,
            runStride: options.runStride ?? 1.65,
            sprintStride: options.sprintStride ?? 1.9,
            runSpeed: options.runSpeed ?? 3.2,
            sprintSpeed: options.sprintSpeed ?? 5.6
        };
        for (const key of ["walkStride", "runStride", "sprintStride"]) {
            const value = tuning[key];
            if (!Number.isFinite(value) || value < 0.35 || value > 4)
                throw new RangeError("Invalid footstep stride: " + key);
        }
        if (!Number.isFinite(tuning.runSpeed) || tuning.runSpeed <= 0 ||
            !Number.isFinite(tuning.sprintSpeed) ||
            tuning.sprintSpeed < tuning.runSpeed)
            throw new RangeError("Invalid footstep speed thresholds");
        this.#tuning = tuning;
    }

    update(dt) {
        if (!this.playing || this.#tuning?.enabled === false ||
            !Number.isFinite(dt) || dt <= 0 || dt > 0.25) {
            this.#reset();
            return;
        }
        const actor = Scene.get(Player.index);
        const now = actor?.position;
        if (!now || ![now.x, now.y, now.z].every(Number.isFinite)) {
            this.#reset();
            return;
        }
        if (this.#last === null) {
            this.#reset({...now});
            return;
        }
        const previous = this.#last;
        this.#last = {...now};
        const dx = now.x - previous.x;
        const dz = now.z - previous.z;
        const vertical = Math.abs(now.y - previous.y);
        const distance = Math.hypot(dx, dz);
        const input = Player.motion();
        if (!input || Math.hypot(input.x, input.z) < 0.12 ||
            !Number.isFinite(distance) || distance > 12 * dt + 0.25 ||
            vertical > 0.12) {
            this.#metres = 0;
            return;
        }
        const ground = Player.ground();
        if (!ground?.grounded) {
            this.#metres = 0;
            return;
        }
        const speed = distance / dt;
        if (speed < 0.2) {
            this.#metres = 0;
            return;
        }
        const gait = speed >= this.#tuning.sprintSpeed ? "sprint" :
            speed >= this.#tuning.runSpeed ? "run" : "walk";
        const stride = gait === "sprint" ? this.#tuning.sprintStride :
            gait === "run" ? this.#tuning.runStride : this.#tuning.walkStride;
        this.#metres += distance;
        if (this.#metres < stride) return;
        // Never burst footsteps after a stutter or delayed physics update.
        this.#metres = Math.min(this.#metres - stride, stride);
        Events.emit("player.footstep", {
            actor: Player.index,
            sequence: ++this.#sequence,
            surface: ground.surface,
            materialId: ground.materialId ?? 0,
            triangleIndex: ground.triangleIndex ?? 4294967295,
            gait, speed, position: {...now}
        });
    }

    stop() {
        this.#reset();
        this.#tuning = null;
    }
}

new PlayerFootstepController().install();
