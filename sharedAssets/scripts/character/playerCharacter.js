// Player-specific adapter for the model-agnostic CharacterController.
import { Script, Player, Character } from "@northstar/engine";

class PlayerCharacterScript extends Script {
    #controller = null;

    start() {
        if (!Player.enabled || !Player.config.character) return;
        const definition = Player.config.character;
        const controller = Character.createController({
            resources: definition.resources,
            states: definition.states,
            tuning: definition.tuning,
            initialHeading: definition.initialHeading,
            hideInFirstPerson: definition.hideInFirstPerson
        });
        controller.start(Player.index);
        this.#controller = controller;
    }

    update(dt) {
        if (this.#controller && !Player.paused) this.#controller.update(dt);
    }

    stop() {
        if (!this.#controller) return;
        try { this.#controller.stop(); }
        finally { this.#controller = null; }
    }
}

new PlayerCharacterScript().install();
