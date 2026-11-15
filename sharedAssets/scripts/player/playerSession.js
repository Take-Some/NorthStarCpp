// Optional pause, exit and title policy; project configs supply the keys.
import { Script, Player, Input, Keys, GamepadButtons, Session, UI } from "@northstar/engine";

class PlayerSessionScript extends Script {
    #wasPause = false;
    #wasExit = false;

    start() {
        this.#wasPause = false;
        this.#wasExit = false;
        if (!Player.enabled) return;
        Player.setPaused(false);
        const title = Player.config.session?.title;
        if (typeof title === "string" && title) UI.setTitle(title);
    }

    update() {
        if (!Player.enabled) return;
        const config = Player.config.session || {};
        const gamepad = Input.firstGamepad();
        const pause = Input.isDown(config.pauseKey ?? Keys.Escape) ||
            Input.hasButton(gamepad, GamepadButtons.Start);
        if (pause && !this.#wasPause) {
            const next = !Player.paused;
            Player.setPaused(next);
            Session.pause(next);
        }
        this.#wasPause = pause;
        const exit = Input.isDown(config.exitKey ?? Keys.F10) ||
            Input.hasButton(gamepad, GamepadButtons.Back);
        if (exit && !this.#wasExit) Session.quit();
        this.#wasExit = exit;
    }

    stop() {
        this.#wasPause = false;
        this.#wasExit = false;
    }
}

new PlayerSessionScript().install();
