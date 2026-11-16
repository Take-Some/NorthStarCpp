// Project-neutral adapter from the possessed Pawn to the visual character.
// Models and animations are authored by the project, not hardcoded by engine.
import { PlayerScript, Player, Character } from "@northstar/engine";

class PlayerCharacterScript extends PlayerScript {
    #controller = null;

    start() {
        if (!this.active || !this.options.character ||
            this.options.character.renderedByProject === true) return;
        const definition = this.options.character;
        const controller = Character.createController({
            resources: definition.resources,
            states: definition.states,
            tuning: definition.tuning,
            initialHeading: definition.initialHeading,
            hideInFirstPerson: definition.hideInFirstPerson,
            firstPersonArms: definition.firstPersonArms
        });
        controller.start(Player.index);
        this.#controller = controller;
    }

    update(dt) {
        if (this.#controller && this.playing)
            this.#controller.update(dt);
    }

    stop() {
        if (!this.#controller) return;
        try { this.#controller.stop(); }
        finally { this.#controller = null; }
    }
}

new PlayerCharacterScript().install();
