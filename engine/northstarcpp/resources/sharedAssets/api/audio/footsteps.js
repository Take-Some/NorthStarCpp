// Root sound policy for the standard player.footstep gameplay event.
// One NAC pack per surface; the host handles caching and voice playback.
import { Script, Events, Audio, Logger } from "@northstar/engine";

class PlayerFootstepAudio extends Script {
    #subscription = null;
    #disabled = false;

    start() {
        this.#disabled = false;
        this.#subscription?.dispose();
        this.#subscription = Events.scope();
        this.#subscription.on("player.footstep", event => {
            if (this.#disabled) return;
            const archive = "audio/footsteps/" + event.surface + ".nac";
            try {
                // Deterministic round-robin variant based on event sequence.
                // No legacy YSCD cue graph; metadata comes from NEF8/NAC.
                Audio.playNac(archive, event.gait, event.sequence, 0.85);
            } catch (error) {
                // Missing provider/asset must not suspend all project scripts.
                Logger.warn("Footsteps unavailable (" + archive + "): " +
                    String(error?.message || error));
                this.#disabled = true;
            }
        });
    }

    stop() {
        this.#subscription?.dispose();
        this.#subscription = null;
        this.#disabled = false;
    }
}
new PlayerFootstepAudio().install();
