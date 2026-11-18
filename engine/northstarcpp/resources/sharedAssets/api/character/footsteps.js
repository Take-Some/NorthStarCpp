// Character locomotion emits contact events and optionally requests authored sounds.
 // Audio service is generic: all sound policy stays in character/project data.
// Configuration is parsed once; one grounded contact query per moving frame.
import { PlayerScript, Player, Scene, Events, Audio, Logger } from "@northstar/engine";

class CharacterFootstepController extends PlayerScript {
    #last = null;
    #metres = 0;
    #sequence = 0;
    #tuning = null;
    #sounds = null;
    #failedAssets = new Set();

    #reset(position = null) {
        this.#last = position;
        this.#metres = 0;
    }

    start() {
        this.#reset();
        this.#sequence = 0;
        const options = this.options?.character?.footsteps ??
            this.options?.footsteps ?? {};
        const tuning = {
            enabled: options.enabled !== false,
            walkStride: options.walkStride ?? 1.25,
            runStride: options.runStride ?? 1.65,
            sprintStride: options.sprintStride ?? 1.9,
            runSpeed: options.runSpeed ?? 2.215,
            sprintSpeed: options.sprintSpeed ?? 3.695
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
        const sounds = options.sounds ?? null;
        if (sounds !== null && (typeof sounds !== "object" || Array.isArray(sounds)))
            throw new TypeError("Character footstep sounds must map surfaces to assets");
        this.#sounds = sounds;
        this.#failedAssets.clear();
    }

    #playAudio(event) {
        const entry = this.#sounds?.[event.surface] ?? this.#sounds?.default;
        if (!entry) return;
        const sound = typeof entry === "string" ? {asset:entry} : entry;
        if (!sound || typeof sound !== "object" || typeof sound.asset !== "string")
            throw new TypeError("Character footstep sound needs an audio asset");
        if (this.#failedAssets.has(sound.asset)) return;
        const request = {
            asset:sound.asset,
            ...(sound.asset.endsWith(".nac") ? {
                clip:sound.clips?.[event.gait] ?? sound.clip ?? event.gait,
                variation:event.sequence,
                gain:sound.gain ?? 1
            } : {})
        };
        try { Audio.play(request); }
        catch(error) {
            // Missing content is a local policy failure, not an audio engine crash.
            this.#failedAssets.add(sound.asset);
            Logger.warn("Character sound unavailable ("+sound.asset+"): "+
                String(error?.message ?? error));
        }
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
        const event = {
            actor: Player.index,
            sequence: ++this.#sequence,
            surface: ground.surface,
            materialId: ground.materialId ?? 0,
            triangleIndex: ground.triangleIndex ?? 4294967295,
            gait, speed, position: {...now}
        };
        Events.emit("player.footstep", event);
        this.#playAudio(event);
    }

    stop() {
        this.#reset();
        this.#tuning = null;
        this.#sounds = null;
        this.#failedAssets.clear();
    }
}

// Character-owned locomotion event schema, not a global audio rule.
Events.define("player.footstep", event =>
    Boolean(event && Number.isSafeInteger(event.actor) && event.actor >= 0 &&
        Number.isSafeInteger(event.sequence) && event.sequence > 0 &&
        typeof event.surface === "string" &&
        ["walk","run","sprint"].includes(event.gait) &&
        Number.isFinite(event.speed) && event.speed >= 0 &&
        event.position && [event.position.x,event.position.y,
                           event.position.z].every(Number.isFinite))
);
new CharacterFootstepController().install();
