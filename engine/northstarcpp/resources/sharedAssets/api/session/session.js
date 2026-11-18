    class Session {
        static pause(paused) { return engine.session.lifecycle.pause(paused); }
        static scene() { return engine.session.scenes.scene(); }
        static loadScene(path) { return engine.session.scenes.loadScene(path); }
        static quit() { return engine.session.lifecycle.quit(); }
        static restart() { return engine.session.lifecycle.restart(); }
    }

    // Game-agnostic UI renderer. Project scripts own every semantic layer:
    // quests, health, inventory, notifications, and screen advertising.
    // Only engine.ui.documents.apply(document) crosses into native C++.
