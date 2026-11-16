// Local Pawn identity is owned by PlayerController (stable Entity ID).
import { PlayerScript, Player, Scene } from "@northstar/engine";

class PlayerActorScript extends PlayerScript {
    start() {
        if (!this.enabled) return;
        const actor = Scene.findPawn();
        if (!actor || !Player.entityId)
            throw new Error("No possessed Pawn: author Pawn + Auto Possess Local");
        const eyeHeight = this.options.eyeHeight;
        // Camera Rig is native; only explicit project overrides are forwarded.
        if (eyeHeight !== undefined) {
            if (!Number.isFinite(eyeHeight) || eyeHeight < 0.5 || eyeHeight > 3.5)
                throw new RangeError("Invalid player eye-height override");
            Player.setEyeHeight(eyeHeight);
        }
    }

    stop() { NorthStar.player.reset(); }
}

new PlayerActorScript().install();
