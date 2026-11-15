// Resolves the authored scene player without engine-level avatar assumptions.
import { Script, Player, Scene } from "@northstar/engine";

class PlayerActorScript extends Script {
    start() {
        if (!Player.enabled) return;
        const actor = Scene.findPlayer();
        if (!actor)
            throw new Error("The scene needs an entity marked player: true");
        Player.setIndex(actor.index);
        const height = Player.config.eyeHeight ?? 1.45;
        if (!Number.isFinite(height) || height < 0.5 || height > 3.5)
            throw new RangeError("Invalid player eyeHeight in playerConfig.js");
        Player.setEyeHeight(height);
    }

    stop() { NorthStar.player.reset(); }
}

new PlayerActorScript().install();
