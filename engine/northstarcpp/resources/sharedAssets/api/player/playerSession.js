// Pause, quit and title policy. Bound keys belong to project configuration.
import { PlayerScript, Player, Input, Keys, GamepadButtons, Session, UI } from "@northstar/engine";

class PlayerSessionScript extends PlayerScript {
    #pause = null;
    #exit = null;

    start() {
        if (!this.enabled) return;
        Player.setPaused(false);
        const settings = this.options?.session || {};
        this.#pause = Input.createAction(settings.pauseKey ?? Keys.Escape,
                                         GamepadButtons.Start);
        this.#exit = Input.createAction(settings.exitKey ?? Keys.F10,
                                        GamepadButtons.Back);
        if (typeof settings.title === "string" && settings.title)
            UI.setTitle(settings.title);
    }

    update() {
        if (!this.enabled) return;
        const pad = Input.firstGamepad();
        if (this.#pause?.pressed(pad)) {
            const next = !Player.paused;
            Player.setPaused(next);
            Session.pause(next);
        }
        if (this.#exit?.pressed(pad)) Session.quit();
    }

    stop() {
        this.#pause?.reset();
        this.#exit?.reset();
        this.#pause = this.#exit = null;
    }
}

new PlayerSessionScript().install();
